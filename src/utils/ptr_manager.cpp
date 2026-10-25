#include "ptr_manager.hpp"
#include "gdal_error.hpp"
#include "../gdal_dataset.hpp"
#include "../gdal_group.hpp"
#include "../gdal_mdarray.hpp"
#include "../gdal_dimension.hpp"
#include "../gdal_attribute.hpp"
#include "../gdal_layer.hpp"
#include "../gdal_rasterband.hpp"

#include <sstream>
#include <thread>
#include <type_traits>
#include <algorithm>

// Here used to be dragons, but now there is a shopping mall
//
// This is the object store, a singleton
//
// It serves 2 purposes:
//
// First, it keeps track of created objects so that they can be reused
// The point of this mechanism is that it returns a reference to the same object
// for two successive calls of `ds.bands.get(1)` for example
// For this use, the V8 objects are indexed with the pointer to the GDAL
// base object
// uids won't work for this use
//
// Second, it is allocated entirely outside of the V8 memory management and the GC
// Thus, it is accessible from the worker threads
// The async locks live here
// For this use, the V8 objects are indexed with numeric uids
// ptrs won't be safe for this use

// Async lock semantics:
//
// * There is one global master lock, all operations on the ObjectStore structures
//   must acquire it
// * There is one async lock per dataset and it is a semaphore because it needs
//   to support being acquired by the main thread and being unlocked in a worker
// * Sync operations can sleep on the semaphore as only the main thread can
//   delete a semaphore
// * Async operations should sleep on the master_sleep condition as semaphores
//   can be deleted by the main thread (but this would also mean that someone forgot
//   to protect his object from the GC)
//   - Failing to protect an object from the GC means that GC could potentially sleep
//   on a semaphore when disposing
//   - GC that sleeps -> event loop that does run
// * Acquiring a semaphore requires acquiring the master look otherwise the
//   semaphore may disappear
// * When waking from master_sleep, the presence of the semaphore (isAlive) must be
//   checked again
// * When unlocking a semaphore, the master_sleep condition is to be broadcasted
// * Never acquire the master lock while holding a semaphore (deadlock avoidance)
// * Multiple datasets are to be locked with .lockDataset which sorts locks (deadlock avoidance)
// * Never sleep with the master lock held (performance)
// * All objects carry the dataset uid
// * All GDAL operations on a dependant object require locking the parent dataset
// - This is best accomplished though .lockDataset
// * Dependant Datasets share a semaphore with their parent through a shared_ptr

namespace node_gdal {

// The book-keeping lives in the store itself, see ObjectStore::maps(). The store is
// per-environment, so the maps hold the JS references of one environment only and
// they are not accessible from the libuv worker threads - those use the semaphores
// which were already resolved on the JS thread by resolveLocks()/lockDatasets().

template <typename GDALPTR> ObjectStore::Maps<GDALPTR> &ObjectStore::maps() {
  type_index key(typeid(GDALPTR));
  shared_ptr<void> &slot = typed_maps[key];
  if (slot == nullptr) { slot = make_shared<Maps<GDALPTR>>(); }
  return *static_cast<Maps<GDALPTR> *>(slot.get());
}

class uv_scoped_mutex {
    public:
  inline uv_scoped_mutex(uv_mutex_t *lock) : lock(lock) {
    uv_mutex_lock(lock);
  }
  inline ~uv_scoped_mutex() {
    uv_mutex_unlock(lock);
  }

    private:
  uv_mutex_t *lock;
};

ObjectStore::ObjectStore() : uid(1) {
#ifdef PTHREAD_MUTEX_DEBUG
  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
  pthread_mutex_init(&master_lock, &attr);
#else
  uv_mutex_init(&master_lock);
#endif
  uv_cond_init(&master_sleep);
}

ObjectStore::~ObjectStore() {
  uv_mutex_destroy(&master_lock);
  uv_cond_destroy(&master_sleep);
}

// The reference to the JS wrapper is weak. V8 clears it as part of the
// collection, but the entry is only removed by the napi_wrap finalizer, which
// node runs on a later turn - the old binding's weak callback ran during the GC
// itself. In between, the entry is stale: it must not be handed out (get would
// produce an empty handle) and isAlive must not claim the object still exists.
template <typename GDALPTR> static bool isDead(const shared_ptr<ObjectStoreItem<GDALPTR>> &item) {
  return item == nullptr || item->obj.Value().IsEmpty();
}

template <typename GDALPTR> static bool isAliveIn(const UidMap<GDALPTR> &m, long uid) {
  auto i = m.find(uid);
  return i != m.end() && !isDead(i->second);
}

bool ObjectStore::isAlive(long uid) {
  if (uid == 0) return true;
  return isAliveIn(maps<GDALRasterBand *>().by_uid, uid) || isAliveIn(maps<OGRLayer *>().by_uid, uid) ||
    isAliveIn(maps<GDALDataset *>().by_uid, uid) || isAliveIn(maps<GDALColorTable *>().by_uid, uid)
#if GDAL_VERSION_MAJOR > 3 || (GDAL_VERSION_MAJOR == 3 && GDAL_VERSION_MINOR >= 1)
    || isAliveIn(maps<shared_ptr<GDALGroup>>().by_uid, uid) || isAliveIn(maps<shared_ptr<GDALMDArray>>().by_uid, uid) ||
    isAliveIn(maps<shared_ptr<GDALDimension>>().by_uid, uid) || isAliveIn(maps<shared_ptr<GDALAttribute>>().by_uid, uid)
#endif
    ;
}

static inline void sortUnique(vector<long> &uids) {
  // Avoid deadlocks
  sort(uids.begin(), uids.end());
  // Eliminate dupes and 0s
  uids.erase(unique(uids.begin(), uids.end()), uids.end());
  if (uids.front() == 0) uids.erase(uids.begin());
}

static inline void addUniqueLock(vector<AsyncLock> &locks, const AsyncLock &async_lock) {
  // Dependent Datasets share the semaphore of their parent: acquiring the same
  // semaphore twice in the same operation would block forever
  if (std::find(locks.begin(), locks.end(), async_lock) == locks.end()) locks.push_back(async_lock);
}

/*
 * Lock a Dataset by uid, throws when the Dataset has been destroyed.
 * There is a single global condition which allows to avoid active spinning.
 * Every time a Dataset releases a lock it must broadcast the condition.
 */
AsyncLock ObjectStore::lockDataset(long uid) {
  if (uid == 0) return nullptr;
  uv_scoped_mutex lock(&master_lock);
  while (true) {
    auto parent = maps<GDALDataset *>().by_uid.find(uid);
    if (parent == maps<GDALDataset *>().by_uid.end()) { throw GDALError("Parent Dataset object has already been destroyed"); }
#if GDAL_VERSION_MAJOR > 3 || (GDAL_VERSION_MAJOR == 3 && GDAL_VERSION_MINOR >= 10)
    // Do not lock thread-safe datasets
    if (parent->second->ptr->IsThreadSafe(GDAL_OF_RASTER)) { return nullptr; }
#endif
    int r = uv_sem_trywait(&parent->second->async_lock->sem);
    if (r == 0) { return parent->second->async_lock; }
    uv_cond_wait(&master_sleep, &master_lock);
  }
}

/*
 * Lock several Datasets by uid avoiding deadlocks, same semantics as the previous one.
 */
vector<AsyncLock> ObjectStore::lockDatasets(vector<long> uids) {
  // There is lots of copying around here but these vectors are never longer than 3 elements
  sortUnique(uids);
  if (uids.size() == 0) return {};
  uv_scoped_mutex lock(&master_lock);
  while (true) {
    try {
      bool locked;
      vector<AsyncLock> locks = _tryLockDatasets(uids, locked);
      if (locked) { return locks; }
    } catch (const char *msg) { throw GDALError(msg); }
    uv_cond_wait(&master_sleep, &master_lock);
  }
}

/*
 * Acquire the lock only if it is free, do not block.
 */
AsyncLock ObjectStore::tryLockDataset(long uid, bool &result) {
  if (uid == 0) {
    result = true;
    return nullptr;
  }
  uv_scoped_mutex lock(&master_lock);
  auto parent = maps<GDALDataset *>().by_uid.find(uid);
  if (parent == maps<GDALDataset *>().by_uid.end()) { throw GDALError("Parent Dataset object has already been destroyed"); }
#if GDAL_VERSION_MAJOR > 3 || (GDAL_VERSION_MAJOR == 3 && GDAL_VERSION_MINOR >= 10)
  if (parent->second->ptr->IsThreadSafe(GDAL_OF_RASTER)) {
    result = true;
    return nullptr;
  }
#endif
  int r = uv_sem_trywait(&parent->second->async_lock->sem);
  if (r == 0) {
    result = true;
    return parent->second->async_lock;
  }
  result = false;
  return nullptr;
}

vector<AsyncLock> ObjectStore::_tryLockDatasets(vector<long> uids, bool &result) {
  vector<AsyncLock> locks;
  for (long uid : uids) {
    auto parent = maps<GDALDataset *>().by_uid.find(uid);
    if (parent == maps<GDALDataset *>().by_uid.end()) { throw GDALError("Parent Dataset object has already been destroyed"); }
#if GDAL_VERSION_MAJOR > 3 || (GDAL_VERSION_MAJOR == 3 && GDAL_VERSION_MINOR >= 10)
    // Skip locking thread-safe datasets
    if (parent->second->ptr->IsThreadSafe(GDAL_OF_RASTER)) continue;
#endif
    addUniqueLock(locks, parent->second->async_lock);
  }
  vector<AsyncLock> locked;
  int r = 0;
  for (AsyncLock &async_lock : locks) {
    r = uv_sem_trywait(&async_lock->sem);
    if (r == 0) {
      locked.push_back(async_lock);
    } else {
      // We failed acquiring one of the locks =>
      // free all acquired locks and start a new cycle
      for (AsyncLock &lock : locked) { uv_sem_post(&lock->sem); }
      uv_cond_broadcast(&master_sleep);
      break;
    }
  }
  if (r == 0) {
    result = true;
    return locks;
  }
  result = false;
  return {};
}

/*
 * Try to acquire several locks avoiding deadlocks without blocking.
 */
vector<AsyncLock> ObjectStore::tryLockDatasets(vector<long> uids, bool &result) {
  // There is lots of copying around here but these vectors are never longer than 3 elements
  sortUnique(uids);
  if (uids.size() == 0) return {};
  uv_scoped_mutex lock(&master_lock);
  return _tryLockDatasets(uids, result);
}

/*
 * Resolve the locks of a list of Datasets so that they can be acquired later
 * from another thread with lockLocks(). Must be called from the thread of the
 * environment that owns the Datasets. Throws when a Dataset has been destroyed.
 * The locks are returned in the order of the uids (deadlock avoidance).
 */
vector<AsyncLock> ObjectStore::resolveLocks(vector<long> uids) {
  vector<AsyncLock> locks;
  if (uids.size() == 0) return locks;
  sortUnique(uids);
  if (uids.size() == 0) return locks;
  uv_scoped_mutex lock(&master_lock);
  for (long uid : uids) {
    auto parent = maps<GDALDataset *>().by_uid.find(uid);
    if (parent == maps<GDALDataset *>().by_uid.end()) { throw GDALError("Parent Dataset object has already been destroyed"); }
#if GDAL_VERSION_MAJOR > 3 || (GDAL_VERSION_MAJOR == 3 && GDAL_VERSION_MINOR >= 10)
    // Do not lock thread-safe datasets
    if (parent->second->ptr->IsThreadSafe(GDAL_OF_RASTER)) { continue; }
#endif
    addUniqueLock(locks, parent->second->async_lock);
  }
  return locks;
}

/*
 * Acquire the locks previously resolved with resolveLocks(). Can be called from
 * any thread. The caller must keep the locks sorted by uid (resolveLocks()
 * returns them in this order) to avoid deadlocks between operations locking
 * several Datasets.
 */
void ObjectStore::lockLocks(const vector<AsyncLock> &locks) {
  if (locks.size() == 0) return;
  uv_scoped_mutex lock(&master_lock);
  while (true) {
    size_t acquired = 0;
    for (const AsyncLock &async_lock : locks) {
      // The Dataset has been destroyed while this operation was waiting for
      // its lock: the semaphore has been posted by dispose() and the operation
      // must fail instead of accessing the closed Dataset
      if (!async_lock->alive) { throw GDALError("Parent Dataset object has already been destroyed"); }
      if (uv_sem_trywait(&async_lock->sem) != 0) break;
      acquired++;
    }
    if (acquired == locks.size()) return;
    // We failed acquiring one of the locks =>
    // free all acquired locks and start a new cycle
    for (size_t i = 0; i < acquired; i++) uv_sem_post(&locks[i]->sem);
    uv_cond_broadcast(&master_sleep);
    uv_cond_wait(&master_sleep, &master_lock);
  }
}

// The basic unit of the ObjectStore is the ObjectStoreItem<GDALPTR>
// There is only one such item per GDALPTR
// There are two shared_ptr to it:
// * one in the uidMap
// * one in the ptrMap
// There is also a reference to the object in its Napi::ObjectWrap: it is weak,
// and the finalizer that ObjectWrap registers calls the C++ destructor when the
// GC collects the JS object, which triggers the dispose functions below

template <typename GDALPTR> ObjectStoreItem<GDALPTR>::ObjectStoreItem(Napi::Reference<Napi::Object> &obj) : obj(obj) {
}
ObjectStoreItem<GDALDataset *>::ObjectStoreItem(Napi::Reference<Napi::Object> &obj) : obj(obj) {
}
ObjectStoreItem<GDALRasterBand *>::ObjectStoreItem(Napi::Reference<Napi::Object> &obj) : obj(obj), owned(false) {
}
ObjectStoreItem<OGRLayer *>::ObjectStoreItem(Napi::Reference<Napi::Object> &obj) : obj(obj) {
}

template <typename GDALPTR>
long ObjectStore::add(GDALPTR ptr, Napi::Reference<Napi::Object> &obj, long parent_uid, bool owned) {
  uv_scoped_mutex lock(&master_lock);
  shared_ptr<ObjectStoreItem<GDALPTR>> item(new ObjectStoreItem<GDALPTR>(obj));
  if constexpr (std::is_same_v<GDALPTR, GDALRasterBand *>) { item->owned = owned; }
  item->uid = uid++;
  if (parent_uid) {
    shared_ptr<ObjectStoreItem<GDALDataset *>> parent = maps<GDALDataset *>().by_uid[parent_uid];
    item->parent = parent;
    parent->children.push_back(item->uid);
  } else {
    item->parent = nullptr;
  }
  item->ptr = ptr;

  maps<GDALPTR>().by_uid[item->uid] = item;
  maps<GDALPTR>().by_ptr[ptr] = item;
  LOG("ObjectStore: Add %s [%ld]<[%ld]", typeid(ptr).name(), item->uid, parent_uid);
  return item->uid;
}

// Creating a Layer object is a special case - it can contain SQL results
long ObjectStore::add(OGRLayer *ptr, Napi::Reference<Napi::Object> &obj, long parent_uid, bool is_result_set) {
  long uid = ObjectStore::add<OGRLayer *>(ptr, obj, parent_uid);
  maps<OGRLayer *>().by_uid[uid] -> is_result_set = is_result_set;
  return uid;
}

// Creating a Dataset object is a special case
// It contains a lock (unless it is a dependant Dataset that shares the lock of its parent)
long ObjectStore::add(GDALDataset *ptr, Napi::Reference<Napi::Object> &obj, long parent_uid, bool close, bool shared_lock) {
  long uid = ObjectStore::add<GDALDataset *>(ptr, obj, parent_uid);
  if (parent_uid == 0 || !shared_lock) {
    maps<GDALDataset *>().by_uid[uid] -> async_lock = make_shared<AsyncLockState>();
  } else {
    maps<GDALDataset *>().by_uid[uid] -> async_lock = maps<GDALDataset *>().by_uid[parent_uid] -> async_lock;
  }
  // Is this an automatic Dataset that should not be closed?
  maps<GDALDataset *>().by_uid[uid] -> close = close;
  return uid;
}

template <typename GDALPTR> bool ObjectStore::has(GDALPTR ptr) {
  uv_scoped_mutex lock(&master_lock);
  auto i = maps<GDALPTR>().by_ptr.find(ptr);
  return i != maps<GDALPTR>().by_ptr.end() && !isDead(i->second);
}
template <typename GDALPTR> Napi::Object ObjectStore::get(GDALPTR ptr) {
  uv_scoped_mutex lock(&master_lock);
  auto i = maps<GDALPTR>().by_ptr.find(ptr);
  if (i == maps<GDALPTR>().by_ptr.end() || isDead(i->second)) return Napi::Object();
  return i->second->obj.Value();
}
template <typename GDALPTR> Napi::Object ObjectStore::get(long uid) {
  uv_scoped_mutex lock(&master_lock);
  auto i = maps<GDALPTR>().by_uid.find(uid);
  if (i == maps<GDALPTR>().by_uid.end() || isDead(i->second)) return Napi::Object();
  return i->second->obj.Value();
}

// Explicit instantiation:
// * allows calling object_store().add without <>
// * makes sure that this class template won't be accidentally instantiated with an unsupported type
template long ObjectStore::add(GDALDriver *, Napi::Reference<Napi::Object> &, long, bool);
template long ObjectStore::add(GDALRasterBand *, Napi::Reference<Napi::Object> &, long, bool);
template long ObjectStore::add(OGRSpatialReference *, Napi::Reference<Napi::Object> &, long, bool);
template long ObjectStore::add(GDALColorTable *, Napi::Reference<Napi::Object> &, long, bool);
template bool ObjectStore::has(GDALDriver *);
template bool ObjectStore::has(GDALDataset *);
template bool ObjectStore::has(OGRLayer *);
template bool ObjectStore::has(GDALRasterBand *);
template bool ObjectStore::has(OGRSpatialReference *);
template bool ObjectStore::has(GDALColorTable *);
template Napi::Object ObjectStore::get(GDALDriver *);
template Napi::Object ObjectStore::get(GDALDataset *);
template Napi::Object ObjectStore::get(OGRLayer *);
template Napi::Object ObjectStore::get(GDALRasterBand *);
template Napi::Object ObjectStore::get(OGRSpatialReference *);
template Napi::Object ObjectStore::get(GDALColorTable *);
template Napi::Object ObjectStore::get<GDALDataset *>(long uid);
#if GDAL_VERSION_MAJOR > 3 || (GDAL_VERSION_MAJOR == 3 && GDAL_VERSION_MINOR >= 1)
template long ObjectStore::add(shared_ptr<GDALAttribute>, Napi::Reference<Napi::Object> &, long, bool);
template long ObjectStore::add(shared_ptr<GDALDimension>, Napi::Reference<Napi::Object> &, long, bool);
template long ObjectStore::add(shared_ptr<GDALGroup>, Napi::Reference<Napi::Object> &, long, bool);
template long ObjectStore::add(shared_ptr<GDALMDArray>, Napi::Reference<Napi::Object> &, long, bool);
template bool ObjectStore::has(shared_ptr<GDALAttribute>);
template bool ObjectStore::has(shared_ptr<GDALDimension>);
template bool ObjectStore::has(shared_ptr<GDALGroup>);
template bool ObjectStore::has(shared_ptr<GDALMDArray>);
template Napi::Object ObjectStore::get(shared_ptr<GDALAttribute>);
template Napi::Object ObjectStore::get(shared_ptr<GDALDimension>);
template Napi::Object ObjectStore::get(shared_ptr<GDALGroup>);
template Napi::Object ObjectStore::get(shared_ptr<GDALMDArray>);
#endif

const char warningGCBug[] =
  "Sleeping on semaphore in garbage collector, this is a bug in gdal-async, event loop blocked for ";
const char warningManualClose[] =
  "Closing a dataset while background async operations are still running, event loop blocked for ";
static inline void uv_sem_wait_with_warning(uv_sem_t *sem, const char *warning) {
  if (uv_sem_trywait(sem) != 0) { MEASURE_EXECUTION_TIME(warning, uv_sem_wait(sem)); }
}

// dispose is called by the C++ destructor, which the finalizer registered by
// Napi::ObjectWrap calls when the GC collects the JS object
// This is the same Persistent that has a reference in the ObjectStoreItem
// Removes the object and all its children for the ObjectStore

// Disposing a Dataset is a special case - it has children (called with the master lock held)
template <> void ObjectStore::dispose(shared_ptr<ObjectStoreItem<GDALDataset *>> item, bool manual) {
  uv_sem_wait_with_warning(
    &item->async_lock->sem, manual ? (eventLoopWarn ? warningManualClose : nullptr) : warningGCBug);
  maps<GDALDataset *>().by_uid.erase(item->uid);
  maps<GDALDataset *>().by_ptr.erase(item->ptr);
  if (item->parent != nullptr) item->parent->children.remove(item->uid);

  item->async_lock->alive = false;
  uv_sem_post(&item->async_lock->sem);
  uv_cond_broadcast(&master_sleep);
  // Beyond this point the Dataset is not alive anymore ->
  // anyone who was waiting for this semaphore should fail (the alive flag has been cleared)

  // All the children are removed from the ObjectStore
  // but the Node/V8 objects still exist
  // They can be deleted only by the GC
  // When this happens, they will skip this in do_dispose
  while (!item->children.empty()) { do_dispose(item->children.back()); }

  if (item->ptr) {
    if (item->close) {
      LOG("Closing GDALDataset %ld [%p]", item->uid, item->ptr);
      GDALClose(item->ptr);
    } else {
      LOG("Destroying GDALDataset without closing %ld [%p]", item->uid, item->ptr);
    }
    item->ptr = nullptr;
  }
}

const char warningSQL[] =
  "Sleeping on semaphore in garbage collector while destroying an SQL results layers, this is a known issue in gdal-async, event loop blocked for ";
// Closing a Layer is a special case - it can contain SQL results
// This is the only case where we could sleep in the GC:
//   A Dataset has multiple layers, one of them is an SQL results layer
//   An asynchronous operation is running on one of the other layers
//   The GC decides it is time to reclaim the SQL results
template <> void ObjectStore::dispose(shared_ptr<ObjectStoreItem<OGRLayer *>> item, bool) {
  maps<OGRLayer *>().by_ptr.erase(item->ptr);
  maps<OGRLayer *>().by_uid.erase(item->uid);
  if (item->parent != nullptr) { item->parent->children.remove(item->uid); }
  if (item->is_result_set) {
    LOG("Closing OGRLayer with SQL results [%ld] [%p]", uid, item->ptr);
    if (item->parent) {
      uv_sem_wait_with_warning(&item->parent->async_lock->sem, warningSQL);
      GDALDataset *parent_ds = item->parent->ptr;
      parent_ds->ReleaseResultSet(item->ptr);
      uv_sem_post(&item->parent->async_lock->sem);
      uv_cond_broadcast(&object_store().master_sleep);
    }
  }
}

// A band is owned by the Dataset it belongs to, so dropping the wrapper is all
// that is normally needed here. The bands the addon creates itself (the ones
// returned by the algebra operators) are the exception: they own a Dataset
// instead of being owned by one, and their destructor still reads the bands
// they compute from, so they have to be freed here - this runs before the
// source Dataset is closed, which is not the case for the finalizer.
template <> void ObjectStore::dispose(shared_ptr<ObjectStoreItem<GDALRasterBand *>> item, bool) {
  if (item == nullptr) return;
  maps<GDALRasterBand *>().by_ptr.erase(item->ptr);
  maps<GDALRasterBand *>().by_uid.erase(item->uid);
  if (item->parent != nullptr) { item->parent->children.remove(item->uid); }
  if (item->owned) {
    LOG("Deleting owned band [%ld] [%p]", item->uid, item->ptr);
    item->owned = false;
    delete item->ptr;
    item->ptr = nullptr;
  }
}

// Generic disposal (called with the master lock held)
template <typename GDALPTR> void ObjectStore::dispose(shared_ptr<ObjectStoreItem<GDALPTR>> item, bool) {
  if (item == nullptr) return;
  maps<GDALPTR>().by_ptr.erase(item->ptr);
  maps<GDALPTR>().by_uid.erase(item->uid);
  if (item->parent != nullptr) { item->parent->children.remove(item->uid); }
}

// Called from the C++ destructor
void ObjectStore::dispose(long uid, bool manual) {
  LOG("ObjectStore: Dispose [%ld]", uid);
  uv_scoped_mutex lock(&master_lock);
  do_dispose(uid, manual);
}

// The locked section of the above function
void ObjectStore::do_dispose(long uid, bool manual) {
  if (maps<GDALDataset *>().by_uid.count(uid))
    dispose(maps<GDALDataset *>().by_uid[uid], manual);
  else if (maps<OGRLayer *>().by_uid.count(uid))
    dispose(maps<OGRLayer *>().by_uid[uid], manual);
  else if (maps<GDALRasterBand *>().by_uid.count(uid))
    dispose(maps<GDALRasterBand *>().by_uid[uid], manual);
  else if (maps<GDALDriver *>().by_uid.count(uid))
    dispose(maps<GDALDriver *>().by_uid[uid], manual);
  else if (maps<OGRSpatialReference *>().by_uid.count(uid))
    dispose(maps<OGRSpatialReference *>().by_uid[uid], manual);
  else if (maps<GDALColorTable *>().by_uid.count(uid))
    dispose(maps<GDALColorTable *>().by_uid[uid], manual);
#if GDAL_VERSION_MAJOR > 3 || (GDAL_VERSION_MAJOR == 3 && GDAL_VERSION_MINOR >= 1)
  else if (maps<shared_ptr<GDALGroup>>().by_uid.count(uid))
    dispose(maps<shared_ptr<GDALGroup>>().by_uid[uid], manual);
  else if (maps<shared_ptr<GDALMDArray>>().by_uid.count(uid))
    dispose(maps<shared_ptr<GDALMDArray>>().by_uid[uid], manual);
  else if (maps<shared_ptr<GDALDimension>>().by_uid.count(uid))
    dispose(maps<shared_ptr<GDALDimension>>().by_uid[uid], manual);
  else if (maps<shared_ptr<GDALAttribute>>().by_uid.count(uid))
    dispose(maps<shared_ptr<GDALAttribute>>().by_uid[uid], manual);
#endif
}

// Closes still open Datasets on process exit
// Called on the main thread after the event loop has exited
void ObjectStore::cleanup() {
  // This unusual loop is needed since dispose deletes elements from the map
  while (maps<GDALDataset *>().by_uid.size() > 0) { dispose(maps<GDALDataset *>().by_uid.cbegin()->second->uid, true); }
}

} // namespace node_gdal
