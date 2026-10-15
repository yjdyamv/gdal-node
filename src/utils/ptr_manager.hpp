
#ifndef __OBJ_STORE_H__
#define __OBJ_STORE_H__

#include <uv.h>

// napi
#include "../napi-wrapper.h"

// gdal
#include <gdal_priv.h>

// ogr
#include <ogrsf_frmts.h>

#include <atomic>
#include <list>
#include <map>

using namespace std;

namespace node_gdal {

// The lock of a Dataset. The semaphore serializes the asynchronous operations
// on a Dataset and its closing. `alive` allows a worker thread that is waiting
// for the lock to notice that the Dataset has been destroyed in the meantime:
// the operation must fail instead of using the closed Dataset.
struct AsyncLockState {
  uv_sem_t sem;
  std::atomic<bool> alive;
  AsyncLockState() : alive(true) {
    uv_sem_init(&sem, 1);
  }
  ~AsyncLockState() {
    uv_sem_destroy(&sem);
  }
};
typedef shared_ptr<AsyncLockState> AsyncLock;

template <typename GDALPTR> struct ObjectStoreItem {
  long uid;
  Napi::Reference<Napi::Object> &obj;
  GDALPTR ptr;
  shared_ptr<ObjectStoreItem<GDALDataset *>> parent;
  ObjectStoreItem(Napi::Reference<Napi::Object> &obj);
};

template <> struct ObjectStoreItem<OGRLayer *> {
  long uid;
  Napi::Reference<Napi::Object> &obj;
  OGRLayer *ptr;
  shared_ptr<ObjectStoreItem<GDALDataset *>> parent;
  bool is_result_set;
  ObjectStoreItem(Napi::Reference<Napi::Object> &obj);
};

template <> struct ObjectStoreItem<GDALDataset *> {
  long uid;
  Napi::Reference<Napi::Object> &obj;
  GDALDataset *ptr;
  shared_ptr<ObjectStoreItem<GDALDataset *>> parent;
  list<long> children;
  AsyncLock async_lock;
  // Does the dataset need to be closed
  bool close;
  ObjectStoreItem(Napi::Reference<Napi::Object> &obj);
};

class ObjectStore {
    public:
  template <typename GDALPTR> long add(GDALPTR ptr, Napi::Reference<Napi::Object> &obj, long parent_uid);
  long add(OGRLayer *ptr, Napi::Reference<Napi::Object> &obj, long parent_uid, bool is_result_set);
  long add(GDALDataset *ptr, Napi::Reference<Napi::Object> &obj, long parent_uid, bool close);

  void dispose(long uid, bool manual = false);
  bool isAlive(long uid);
  inline void lockDataset(AsyncLock lock) {
    uv_sem_wait(&lock->sem);
  }
  inline void unlockDataset(AsyncLock lock) {
    uv_sem_post(&lock->sem);
    uv_mutex_lock(&master_lock);
    uv_cond_broadcast(&master_sleep);
    uv_mutex_unlock(&master_lock);
  }
  inline void unlockDatasets(vector<AsyncLock> locks) {
    for (const AsyncLock &l : locks) uv_sem_post(&l->sem);
    uv_mutex_lock(&master_lock);
    uv_cond_broadcast(&master_sleep);
    uv_mutex_unlock(&master_lock);
  }
  AsyncLock lockDataset(long uid);
  vector<AsyncLock> lockDatasets(vector<long> uids);
  AsyncLock tryLockDataset(long uid, bool &result);
  vector<AsyncLock> tryLockDatasets(vector<long> uids, bool &result);
  // resolveLocks() has to be called from the thread of the environment that
  // owns the datasets (the maps are thread-local), lockLocks() can then be
  // called from any other thread
  vector<AsyncLock> resolveLocks(vector<long> uids);
  void lockLocks(const vector<AsyncLock> &locks);

  template <typename GDALPTR> bool has(GDALPTR ptr);
  template <typename GDALPTR> Napi::Object get(GDALPTR ptr);
  template <typename GDALPTR> Napi::Object get(long uid);

  void cleanup();

  ObjectStore();
  ~ObjectStore();

    private:
  long uid;
  uv_mutex_t master_lock;
  uv_cond_t master_sleep;
  vector<AsyncLock> _tryLockDatasets(vector<long> uids, bool &result);
  template <typename GDALPTR> void dispose(shared_ptr<ObjectStoreItem<GDALPTR>> item, bool manual);
  void do_dispose(long uid, bool manual = false);
};

} // namespace node_gdal

#endif
