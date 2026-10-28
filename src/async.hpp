#ifndef __NODE_GDAL_ASYNC_WORKER_H__
#define __NODE_GDAL_ASYNC_WORKER_H__

#include <thread>
#include <cstdint>
#include <string>
#include <functional>
#include <chrono>
#include <atomic>
#include <memory>
#include <type_traits>
#include "napi-wrapper.h"
#include "gdal_common.hpp"

namespace node_gdal {

// The id of the thread of the environment of this addon instance (the main
// thread or a worker_thread) - per-thread, the addon can be loaded in several
// V8 isolates

// Set by the env cleanup hook: from then on the JS world of this thread may not
// be touched. Per-thread, so that a worker exiting does not stop the pending
// callbacks of the main environment.

// The common form of the methods and the getters: the body is a small struct
// with a static `run()` which is a template on whether the caller wants the
// asynchronous form, given as a type instead of a run time bool, so a method can
// build the job for one of the two and the compiler still sees the argument
// checks only once. Both JS functions are then generated from the one body:
//
//   namespace {
//   struct barBody : node_gdal::AsyncMethodBody {
//     template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
//       Args args(info);
//       ...
//       return job.run(info, node_gdal::is_async<Async>);
//     }
//   };
//   } // namespace
//
//   GDAL_ASYNC_METHOD(Foo::bar, barBody)

struct AsyncMethodBody {};

template <class Async> inline constexpr bool is_async = Async::value;

template <class Impl> Napi::Value AsyncMethod(const Napi::CallbackInfo &info, std::false_type) {
  return Impl::run(info, std::false_type{});
}

template <class Impl> Napi::Value AsyncMethod(const Napi::CallbackInfo &info, std::true_type) {
  // A function as the last argument is the callback of the asynchronous method,
  // and a caller which has to be called back cannot be given a promise, so the
  // failures are thrown at it, the way the synchronous variant reports them.
  if (info.Length() > 0 && info[info.Length() - 1].IsFunction()) {
    return Impl::run(info, std::true_type{});
  }

  // Without a callback the method returns a promise, so the failures have to
  // reject it instead of being thrown at the caller
  Napi::Env env = info.Env();
  Napi::Value result;
  try {
    result = Impl::run(info, std::true_type{});
  } catch (const GDALError &error) {
    Napi::Promise::Deferred deferred(env);
    Napi::Value js_error = node_gdal::toJS(env, error).Value();
    deferred.Reject(js_error);
    return deferred.Promise();
  }
  if (!env.IsExceptionPending()) { return result; }

  napi_value exception = nullptr;
  if (napi_get_and_clear_last_exception(env, &exception) != napi_ok || exception == nullptr) { return result; }
  Napi::Promise::Deferred deferred(env);
  deferred.Reject(Napi::Value(env, exception));
  return deferred.Promise();
}

#define GDAL_ASYNC_METHOD(method, Body)                                                                                \
  NAPI_METHOD(method) {                                                                                                \
    return node_gdal::AsyncMethod<Body>(info, std::false_type{});                                                      \
  }                                                                                                                    \
  NAPI_METHOD(method##Async) {                                                                                         \
    return node_gdal::AsyncMethod<Body>(info, std::true_type{});                                                       \
  }

// This generates getter definitions for 2 getters: sync and async version
#define GDAL_ASYNC_GETTER(method, Body)                                                                                \
  NAPI_GETTER(method) {                                                                                                \
    return node_gdal::AsyncMethod<Body>(info, std::false_type{});                                                      \
  }                                                                                                                    \
  NAPI_GETTER(method##Async) {                                                                                         \
    return node_gdal::AsyncMethod<Body>(info, std::true_type{});                                                       \
  }

// This generates method definitions for 2 methods, for the classes which are
// templates themselves (the hidden common block is then a struct in the class)
#define GDAL_ASYNC_METHOD_STATIC(method, Body)                                                                         \
  static NAPI_METHOD(method) {                                                                                         \
    return node_gdal::AsyncMethod<Body>(info, std::false_type{});                                                      \
  }                                                                                                                    \
  static NAPI_METHOD(method##Async) {                                                                                  \
    return node_gdal::AsyncMethod<Body>(info, std::true_type{});                                                       \
  }

// A method which has no asynchronous variant, written in the same shape: the
// body is a struct with a static `run()`, only without the template on the
// caller. The check that `this` is still alive is `unwrapThis()`, so a body is
// the argument checks and the wrapped call itself:
//
//   namespace {
//   struct getAreaBody : node_gdal::SyncMethodBody {
//     static Napi::Value run(const Napi::CallbackInfo &info) {
//       auto geom = node_gdal::unwrapThis<GeometryCollection>(info, "GeometryCollection");
//       if (!geom) { return geom.failure; }
//       return Napi::Number::New(info.Env(), geom->get()->get_Area());
//     }
//   };
//   } // namespace
//
//   GDAL_SYNC_METHOD(GeometryCollection::getArea, getAreaBody)

struct SyncMethodBody {};

// What `unwrapThis()` returns: the object, or the value the method has to
// return when the object has already been destroyed.
//
// The two forms cannot report the failure in the same way. The synchronous one
// throws, because its caller is the JS method itself, so `failure` is only the
// undefined the body has to return after the exception has been scheduled. The
// asynchronous one has to reject, because its caller is already holding a
// promise - an exception thrown from a getter which is asked for the
// asynchronous form would reach the caller instead of rejecting that promise.
template <class T> struct UnwrapResult {
  T *obj;
  Napi::Value failure;

  operator bool() const { return obj != nullptr; }
  T *operator->() const { return obj; }
  T &operator*() const { return *obj; }
};

template <class T> UnwrapResult<T> unwrapThis(const Napi::CallbackInfo &info, const char *class_name) {
  T *obj = UnwrapWrapped<T>(info.This().As<Napi::Object>());
  if (obj != nullptr && obj->isAlive()) { return { obj, info.Env().Undefined() }; }

  Napi::Error::New(info.Env(), std::string(class_name) + " object has already been destroyed")
    .ThrowAsJavaScriptException();
  return { nullptr, info.Env().Undefined() };
}

template <class T, class Async>
UnwrapResult<T> unwrapThis(const Napi::CallbackInfo &info, const char *class_name, Async) {
  T *obj = UnwrapWrapped<T>(info.This().As<Napi::Object>());
  if (obj != nullptr && obj->isAlive()) { return { obj, info.Env().Undefined() }; }

  auto message = std::string(class_name) + " object has already been destroyed";
  // The same body also generates the synchronous form of the method, which
  // throws like every other synchronous method does
  if constexpr (Async::value) { return { nullptr, RejectPromise(info, message.c_str()) }; }
  Napi::Error::New(info.Env(), message).ThrowAsJavaScriptException();
  return { nullptr, info.Env().Undefined() };
}

template <class Impl> inline Napi::Value SyncMethod(const Napi::CallbackInfo &info) {
  return Impl::run(info);
}

#define GDAL_SYNC_METHOD(method, Body)                                                                                 \
  NAPI_METHOD(method) {                                                                                                \
    return node_gdal::SyncMethod<Body>(info);                                                                          \
  }

#define GDAL_SYNC_GETTER(method, Body)                                                                                 \
  NAPI_GETTER(method) {                                                                                                \
    return node_gdal::SyncMethod<Body>(info);                                                                          \
  }

// This generates method declarations for 2 methods: sync and async version
#define GDAL_ASYNCABLE_DECLARE(method)                                                                                 \
  static NAPI_METHOD(method);                                                                                          \
  static NAPI_METHOD(method##Async);

// This generates getter declarations for 2 getters: sync and async version
#define GDAL_ASYNCABLE_GETTER_DECLARE(method)                                                                          \
  static NAPI_GETTER(method);                                                                                          \
  static NAPI_GETTER(method##Async);

// The declarations of the methods which are not members of a class
#define GDAL_ASYNCABLE_GLOBAL(method)                                                                                  \
  Napi::Value method(const Napi::CallbackInfo &info);                                                                  \
  Napi::Value method##Async(const Napi::CallbackInfo &info);

// Handle locking (used only for sync methods)
#define GDAL_LOCK_PARENT(p)                                                                                            \
  AsyncGuard lock;                                                                                                     \
  try {                                                                                                                \
    lock.acquire((p)->parent_uid);                                                                                     \
  } catch (const char *err) {                                                                                          \
    Napi::Error::New(info.Env(), err).ThrowAsJavaScriptException();                                                    \
    return info.Env().Undefined();                                                                                     \
  } catch (const std::exception &err) {                                                                                \
    Napi::Error::New(info.Env(), err.what()).ThrowAsJavaScriptException();                                             \
    return info.Env().Undefined();                                                                                     \
  }

// GDAL_LOCK_PARENT returns a value on error, so a setter (void) needs this
#define GDAL_LOCK_PARENT_VOID(p)                                                                                       \
  AsyncGuard lock;                                                                                                     \
  try {                                                                                                                \
    lock.acquire((p)->parent_uid);                                                                                     \
  } catch (const char *err) {                                                                                          \
    Napi::Error::New(info.Env(), err).ThrowAsJavaScriptException();                                                    \
    return;                                                                                                            \
  } catch (const std::exception &err) {                                                                                \
    Napi::Error::New(info.Env(), err.what()).ThrowAsJavaScriptException();                                             \
    return;                                                                                                            \
  }

static const char eventLoopWarning[] =
  "Synchronous method called while an asynchronous operation is running in the background, check node_modules/gdal-node/ASYNCIO.md, event loop blocked for ";
// These constructors throw
// Only one use case never throws: on the main thread
// and after checking that the Dataset is alive
class AsyncGuard {
    public:
  inline AsyncGuard() : lock(nullptr), locks(nullptr) {
  }
  inline AsyncGuard(long uid) : locks(nullptr) {
    lock = object_store().lockDataset(uid);
  }
  inline AsyncGuard(vector<long> uids) : lock(nullptr), locks(nullptr) {
    if (uids.size() == 1)
      lock = object_store().lockDataset(uids[0]);
    else
      locks = make_shared<vector<AsyncLock>>(object_store().lockDatasets(uids));
  }
  inline AsyncGuard(vector<long> uids, bool warning) : lock(nullptr), locks(nullptr) {
    bool locked = true;
    if (uids.size() == 1) {
      if (uids[0] == 0) return;
      lock = warning ? object_store().tryLockDataset(uids[0], locked) : object_store().lockDataset(uids[0]);
      if (!locked) { MEASURE_EXECUTION_TIME(eventLoopWarning, lock = object_store().lockDataset(uids[0])); }
    } else {
      locks = warning ? make_shared<vector<AsyncLock>>(object_store().tryLockDatasets(uids, locked))
                      : make_shared<vector<AsyncLock>>(object_store().lockDatasets(uids));
      if (!locked) {
        MEASURE_EXECUTION_TIME(
          eventLoopWarning, locks = make_shared<vector<AsyncLock>>(object_store().lockDatasets(uids)));
      }
    }
  }
  inline void acquire(long uid) {
    if (lock != nullptr) throw GDALError("Trying to acquire multiple locks");
    lock = object_store().lockDataset(uid);
  }
  inline ~AsyncGuard() {
    if (lock != nullptr) object_store().unlockDataset(lock);
    if (locks != nullptr) object_store().unlockDatasets(*locks);
  }

    private:
  AsyncLock lock;
  shared_ptr<vector<AsyncLock>> locks;
};

// The same role as AsyncGuard, for the objects of the JS world that are needed
// by an aux thread: the locks are resolved on the JS thread before the worker
// is queued (the ObjectStore maps are thread-local) and are only acquired here.
class AsyncWorkerGuard {
    public:
  // The store is captured on the JS thread: the guard lives on the aux thread,
  // which cannot look the store up by environment (only the JS thread knows the
  // environment) - it only needs the mutexes which protect the semaphores
  inline AsyncWorkerGuard(ObjectStore &store, const vector<AsyncLock> &locks) : store(store), locks(locks) {
    store.lockLocks(locks);
  }
  inline ~AsyncWorkerGuard() {
    store.unlockDatasets(locks);
  }

    private:
  ObjectStore &store;
  const vector<AsyncLock> &locks;
};

// This is trivially copyable so that it can be queued across threads
struct GDALProgressInfo {
  double complete;
  const char *message;

  GDALProgressInfo(const GDALProgressInfo &o);
  GDALProgressInfo(double, const char *);
  GDALProgressInfo();
  ~GDALProgressInfo() = default;
};

// The abort state of an operation. This is the only mutable state shared
// between the JS thread and the aux thread: the JS thread flips it from the
// 'abort' listener of the AbortSignal, the aux thread reads it in the progress
// trampoline, so the aux thread never has to touch V8.
struct GDALAbortState {
  // set by the 'abort' listener of the AbortSignal, read by the trampoline
  std::atomic<bool> aborted{false};
  // set when GDAL was actually told to stop (or was never started), which is
  // what turns the failure that follows into an AbortError
  std::atomic<bool> interrupted{false};
};

// Owns the JS side of an AbortSignal - the signal object itself and the
// 'abort' listener which sets the flag above. Both are created, used and
// released on the JS thread.
class GDALAbortSignal {
    public:
  std::shared_ptr<GDALAbortState> state;

  // Registers the 'abort' listener. Returns false and leaves a pending JS
  // exception behind if the value is not an AbortSignal.
  bool arm(const Napi::Value &value);
  inline bool aborted() const {
    return state != nullptr && state->aborted.load();
  }
  inline bool interrupted() const {
    return state != nullptr && state->interrupted.load();
  }
  inline void markInterrupted() const {
    if (state != nullptr) state->interrupted.store(true);
  }
  // Unregisters the listener, it has to be called on the JS thread
  void disarm();
  ~GDALAbortSignal();

    private:
  Napi::ObjectReference signal;
  Napi::FunctionReference listener;
};

// The error reported when an operation was interrupted by its AbortSignal.
// It has the same shape as the AbortError of the Node.js core.
Napi::Error abortError(Napi::Env env);

class GDALSyncExecutionProgress {
  Napi::FunctionReference *progress_callback;

  GDALSyncExecutionProgress() = delete;

    public:
  GDALSyncExecutionProgress(Napi::FunctionReference *);
  ~GDALSyncExecutionProgress();
  void Send(GDALProgressInfo *) const;
};

typedef std::function<Napi::Value(const char *)> GetFromPersistentFunc;
typedef Napi::AsyncProgressQueueWorker<GDALProgressInfo> GDALAsyncProgressWorker;
typedef GDALAsyncProgressWorker::ExecutionProgress GDALAsyncExecutionProgress;

// This an ExecutionContext that works both with Node.js' async ExecutionProgress when in async mode
// and with GDALSyncExecutionContext when in sync mode
class GDALExecutionProgress {
  // Only one of these is active at any given moment
  const GDALAsyncExecutionProgress *async;
  const GDALSyncExecutionProgress *sync;
  // Shared with the JS thread, may be null when there is no AbortSignal
  const std::shared_ptr<GDALAbortState> abort;

  GDALExecutionProgress() = delete;

    public:
  GDALExecutionProgress(const GDALAsyncExecutionProgress *, const std::shared_ptr<GDALAbortState> &);
  GDALExecutionProgress(const GDALSyncExecutionProgress *, const std::shared_ptr<GDALAbortState> &);
  ~GDALExecutionProgress();
  void Send(GDALProgressInfo *info) const;
  inline bool Aborted() const {
    return abort != nullptr && abort->aborted.load();
  }
  // GDAL is being stopped because of the AbortSignal and not by its own error
  inline void MarkInterrupted() const {
    if (abort != nullptr) abort->interrupted.store(true);
  }
};

// This is the progress callback trampoline
// It can be invoked both in the main thread (in sync mode) or in auxillary thread (in async mode)
// It is essentially a gateway between the GDAL world and Node.js/V8 world
int ProgressTrampoline(double dfComplete, const char *pszMessage, void *pProgressArg);

//
// This is the common class for handling async operations
// It has two subclasses: GDALCallbackWorker and GDALPromiseWorker
// They are meant to be used by GDALAsyncableJob defined below
//
// It takes the lambdas as input
// GDALType is the type of the object that will be carried from
// the aux thread to the main thread
//
// JS-visible object creation is possible only in the main thread while
// ths JS world is not running
//
template <class GDALType> class GDALAsyncWorker : public GDALAsyncProgressWorker {
    public:
  typedef std::function<GDALType(const GDALExecutionProgress &)> GDALMainFunc;
  typedef std::function<Napi::Value(const GDALType, const GetFromPersistentFunc &)> GDALRValFunc;

    protected:
  Napi::FunctionReference *progressCallback;
  // Keeps the 'abort' listener of the AbortSignal installed for the whole
  // lifetime of the operation, it is released by the destructor on the JS thread
  std::shared_ptr<GDALAbortSignal> abort;
  const GDALMainFunc doit;
  const GDALRValFunc rval;
  const std::vector<long> ds_uids;
  // The store of the environment, captured on the JS thread: the aux thread
  // cannot reach it otherwise and needs its mutexes in Execute()
  ObjectStore &store;
  // The dataset locks, resolved in the constructor on the JS thread and
  // acquired in Execute() on the aux thread
  std::vector<AsyncLock> locks;
  std::string lockError;
  // The class and the code of the failure which happened on the aux thread:
  // only the message can be handed to Napi::AsyncWorker, so they are kept here
  // and the error is rebuilt on the JS thread
  std::string errorName;
  std::string errorCode;
  GDALType raw;

  // Napi::AsyncWorker has no inheritable persistent store of its own (the old
  // binding had SaveToPersistent/GetFromPersistent), so the worker keeps the referenced
  // objects alive itself. Both creation and release happen on the main thread.
  std::map<std::string, Napi::Reference<Napi::Object>> persistent;

  inline void SaveToPersistent(const std::string &key, const Napi::Object &value) {
    persistent.emplace(key, Napi::Persistent(value));
  }

  inline Napi::Value GetFromPersistent(const char *key) {
    auto i = persistent.find(key);
    return i != persistent.end() ? i->second.Value() : Napi::Value();
  }

  inline void RestoreObjects(const std::map<std::string, Napi::Object> &objects) {
    for (auto i = objects.begin(); i != objects.end(); i++) SaveToPersistent(i->first, i->second);
    for (auto i = ds_uids.begin(); i != ds_uids.end(); i++)
      if (*i != 0) SaveToPersistent("ds" + std::to_string(*i), object_store().get<GDALDataset *>(*i));
  }

  // The locks of the datasets of this operation have to be resolved while the
  // JS thread is still running: the ObjectStore maps are thread-local and the
  // aux thread cannot look up the datasets there. The error is kept and
  // reported from Execute(), exactly like a failing GDAL call.
  inline void resolveDatasetLocks() {
    try {
      locks = object_store().resolveLocks(ds_uids);
    } catch (const char *err) {
      lockError = err != nullptr && *err != '\0' ? err : "Operation failed";
    } catch (const std::exception &err) {
      const char *what = err.what();
      lockError = what != nullptr && *what != '\0' ? what : "Operation failed";
    }
  }

    public:
  GDALAsyncWorker(
    const Napi::Function &resultCallback,
    Napi::FunctionReference *progressCallback,
    const std::shared_ptr<GDALAbortSignal> &abort,
    const GDALMainFunc &doit,
    const GDALRValFunc &rval,
    const std::map<std::string, Napi::Object> &objects,
    const std::vector<long> &ds_uids);

  GDALAsyncWorker(
    Napi::Env env,
    Napi::FunctionReference *progressCallback,
    const std::shared_ptr<GDALAbortSignal> &abort,
    const GDALMainFunc &doit,
    const GDALRValFunc &rval,
    const std::map<std::string, Napi::Object> &objects,
    const std::vector<long> &ds_uids);

  ~GDALAsyncWorker();

  void Execute(const ExecutionProgress &progress) override;
  Napi::Value ProduceRVal();
  void OnProgress(const GDALProgressInfo *data, size_t count) override;

    protected:
  // The error to report: an AbortError if the operation was interrupted by its
  // AbortSignal, the original error otherwise. Called on the JS thread.
  Napi::Error reportError(const Napi::Error &e);
};

template <class GDALType>
GDALAsyncWorker<GDALType>::GDALAsyncWorker(
  const Napi::Function &resultCallback,
  Napi::FunctionReference *progressCallback,
  const std::shared_ptr<GDALAbortSignal> &abort,
  const GDALMainFunc &doit,
  const GDALRValFunc &rval,
  const std::map<std::string, Napi::Object> &objects,
  const std::vector<long> &ds_uids)
  : GDALAsyncProgressWorker(resultCallback, "node-gdal:GDALAsyncWorker"),
    progressCallback(progressCallback),
    abort(abort),
    // These members are not references! These functions must be copied
    // as they will be executed in async context!
    doit(doit),
    rval(rval),
    ds_uids(ds_uids),
    store(object_store()) {
  // Main thread with the JS world running
  resolveDatasetLocks();
  // Turn the JS handles collected by the job into persistent references
  RestoreObjects(objects);
}

template <class GDALType>
GDALAsyncWorker<GDALType>::GDALAsyncWorker(
  Napi::Env env,
  Napi::FunctionReference *progressCallback,
  const std::shared_ptr<GDALAbortSignal> &abort,
  const GDALMainFunc &doit,
  const GDALRValFunc &rval,
  const std::map<std::string, Napi::Object> &objects,
  const std::vector<long> &ds_uids)
  : GDALAsyncProgressWorker(env, "node-gdal:GDALAsyncWorker"),
    progressCallback(progressCallback),
    abort(abort),
    doit(doit),
    rval(rval),
    ds_uids(ds_uids),
    store(object_store()) {
  resolveDatasetLocks();
  RestoreObjects(objects);
}

template <class GDALType> Napi::Value GDALAsyncWorker<GDALType>::ProduceRVal() {
  if (env_context().shutting_down()) return Env().Undefined();
  return rval(raw, [this](const char *key) { return this->GetFromPersistent(key); });
}

template <class GDALType> void GDALAsyncWorker<GDALType>::Execute(const ExecutionProgress &progress) {
  // Aux thread with the JS world running
  // V8 objects are not acessible here
  try {
    // An error in the lock resolution that happened on the main thread
    if (!lockError.empty()) {
      this->SetError(lockError);
      return;
    }
    // The signal was already aborted before the operation was started
    if (abort != nullptr && abort->aborted()) {
      abort->markInterrupted();
      this->SetError("The operation was aborted");
      return;
    }
    GDALExecutionProgress executionProgress(&progress, abort != nullptr ? abort->state : nullptr);
    AsyncWorkerGuard lock(store, locks);
    raw = doit(executionProgress);
  } catch (const GDALError &err) {
    errorName = err.name();
    errorCode = err.code();
    this->SetError(err.message());
  } catch (const char *err) {
    // node-addon-api decides success from `_error.size() == 0`, so an empty
    // message would be taken for a success and the uninitialized `raw` would be
    // converted (the GDAL calls that fail without setting a CPL error report an
    // empty message)
    this->SetError(err != nullptr && *err != '\0' ? err : "Operation failed");
  } catch (const std::exception &err) {
    const char *what = err.what();
    this->SetError(what != nullptr && *what != '\0' ? what : "Operation failed");
  }
}

template <class GDALType> GDALAsyncWorker<GDALType>::~GDALAsyncWorker() {
  // FunctionReference has its destructor suppressed by the argument macros,
  // Reset() releases the reference explicitly
  if (progressCallback != nullptr) {
    progressCallback->Reset();
    delete progressCallback;
  }
  // Removes the 'abort' listener from the AbortSignal, the worker is destroyed
  // on the JS thread after the operation has finished
  if (abort != nullptr) {
    abort->disarm();
    abort.reset();
  }
}

template <class GDALType> Napi::Error GDALAsyncWorker<GDALType>::reportError(const Napi::Error &e) {
  // Only an error which was actually caused by the interruption becomes an
  // AbortError: when the operation fails on its own while the signal is raised
  // at about the same time, the original error is what the user needs to see
  if (abort == nullptr || !abort->interrupted()) {
    if (errorName.empty() && errorCode.empty()) return e;
    return toJS(Env(), GDALError(e.Message(), errorName, errorCode));
  }
  return abortError(Env());
}

template <class GDALType>
void GDALAsyncWorker<GDALType>::OnProgress(const GDALProgressInfo *data, size_t count) {
  if (progressCallback == nullptr) return;
  // Back to the main thread with the JS world not running
  // A mutex-protected pop in the calling function can sometimes produce a spurious call
  // with no data, handle gracefully this case -> no need to call JS if there is no data to deliver
  if (data == nullptr || count == 0) return;
  // Receiving more than one callback invocation at the same time is also possible
  // Send only the last one to JS
  const GDALProgressInfo *to_send = data + (count - 1);
  std::vector<napi_value> argv;
  argv.push_back(Napi::Number::New(Env(), to_send->complete));
  argv.push_back(SafeString::New(Env(), to_send->message));
  try {
    progressCallback->Value().Call(argv);
  } catch (const Napi::Error &) {
    this->SetError("async progress callback exception");
  }
}

// This async worker calls a callback
// The result is delivered as (null, rval) or (error), matching the old behaviour
template <class GDALType> class GDALCallbackWorker : public GDALAsyncWorker<GDALType> {
    public:
  using GDALAsyncWorker<GDALType>::GDALAsyncWorker;

    protected:
  void OnOK() override {
    // The environment is being torn down, the callback cannot be delivered
    if (env_context().shutting_down()) {
      return;
    }
    Napi::AsyncWorker::OnOK();
  }
  void OnError(const Napi::Error &e) override {
    if (env_context().shutting_down()) {
      return;
    }    Napi::AsyncWorker::OnError(this->reportError(e));
  }
  std::vector<napi_value> GetResult(Napi::Env env) override {
    std::vector<napi_value> r = {env.Null(), this->ProduceRVal()};
    return r;
  }
};

// This async worker returns a Promise
template <class GDALType> class GDALPromiseWorker : public GDALAsyncWorker<GDALType> {
    public:
  typedef typename GDALAsyncWorker<GDALType>::GDALMainFunc GDALMainFunc;
  typedef typename GDALAsyncWorker<GDALType>::GDALRValFunc GDALRValFunc;

    private:
  Napi::Promise::Deferred deferred;

    public:
  GDALPromiseWorker(
    Napi::Env env,
    Napi::FunctionReference *progressCallback,
    const std::shared_ptr<GDALAbortSignal> &abort,
    const GDALMainFunc &doit,
    const GDALRValFunc &rval,
    const std::map<std::string, Napi::Object> &objects,
    const std::vector<long> &ds_uids);

  inline Napi::Value Promise() {
    return deferred.Promise();
  }

    protected:
  void OnOK() override;
  void OnError(const Napi::Error &e) override;
};

template <class GDALType>
GDALPromiseWorker<GDALType>::GDALPromiseWorker(
  Napi::Env env,
  Napi::FunctionReference *progressCallback,
  const std::shared_ptr<GDALAbortSignal> &abort,
  const GDALMainFunc &doit,
  const GDALRValFunc &rval,
  const std::map<std::string, Napi::Object> &objects,
  const std::vector<long> &ds_uids)
  : GDALAsyncWorker<GDALType>(env, progressCallback, abort, doit, rval, objects, ds_uids), deferred(env) {
}

template <class GDALType> void GDALPromiseWorker<GDALType>::OnOK() {
  if (env_context().shutting_down()) {
    return;
  }
  deferred.Resolve(this->ProduceRVal());
}

template <class GDALType> void GDALPromiseWorker<GDALType>::OnError(const Napi::Error &e) {
  if (env_context().shutting_down()) return;
  deferred.Reject(this->reportError(e).Value());
}

// This the basic unit of the GDALAsyncable framework
// GDALAsyncableJob is a GDAL job consisting of a main
// lambda that calls GDAL and rval lambda that transforms
// the return value
// It handles persistence of the referenced objects and
// can be executed both synchronously and asynchronously
//
// GDALType is the intermediary type that will be carried
// across the context boundaries
//
// The caller must ensure that all the lambdas can be executed in
// another thread:
// * no capturing of automatic variables (C++ memory management)
// * no referencing of JS-visible objects in main() (V8 memory management)
// * protecting all JS-visible objects from the GC by calling persist() (V8 MM)
// * locking all GDALDatasets (GDAL limitation)
//
// If a GDALDataset is locked, but not persisted, the GC could still
// try to free it - in this case it will stop the JS world and then it will wait
// on the Dataset lock in PtrManager::dispose() blocking the event loop - the situation
// is safe but it is still best if it is avoided (see the comments in ptr_manager.cpp)

template <class GDALType> class GDALAsyncableJob {
    public:
  typedef std::function<GDALType(const GDALExecutionProgress &)> GDALMainFunc;
  typedef std::function<Napi::Value(const GDALType, const GetFromPersistentFunc &)> GDALRValFunc;
  // This is the lambda that produces the <GDALType> object
  GDALMainFunc main;
  // This is the lambda that produces the JS return object from the <GDALType> object
  GDALRValFunc rval;
  Napi::FunctionReference *progress;
  // The AbortSignal of the operation, if any
  std::shared_ptr<GDALAbortSignal> abort;

  GDALAsyncableJob(long ds_uid)
    : main(), rval(), progress(nullptr), abort(nullptr), persistent(), ds_uids({ds_uid}), autoIndex(0) {};
  GDALAsyncableJob(std::vector<long> ds_uids)
    : main(), rval(), progress(nullptr), abort(nullptr), persistent(), ds_uids(ds_uids), autoIndex(0) {};

  // Registers the 'abort' listener on the AbortSignal given as a JS value.
  // Returns false, with a pending JS exception, if it is not an AbortSignal.
  inline bool setAbortSignal(const Napi::Value &value) {
    if (value.IsEmpty() || value.IsUndefined() || value.IsNull()) return true;
    auto signal = std::make_shared<GDALAbortSignal>();
    if (!signal->arm(value)) {
      // The caller returns at once with the pending TypeError and the operation
      // never starts, so it is the one that has to give the progress callback
      // up - the worker and the sync execution context are its only other owners
      releaseProgress();
      return false;
    }
    abort = signal;
    return true;
  }

  // Releases the progress callback of an operation which will never run
  inline void releaseProgress() {
    if (progress == nullptr) return;
    progress->Reset();
    delete progress;
    progress = nullptr;
  }

  // True if GDAL is able to interrupt this operation: either because a progress
  // callback has been installed or because an AbortSignal is armed. GDAL can be
  // interrupted only through the progress callback, so the trampoline has to be
  // installed even when the user did not ask for progress notifications.
  inline bool interruptible() const {
    return progress != nullptr || abort != nullptr;
  }

  inline void persist(const std::string &key, const Napi::Object &obj) {
    persistent[key] = obj;
  }

  inline void persist(const std::string &key, const Napi::Value &v) {
    persistent[key] = v.As<Napi::Object>();
  }

  inline void persist(const Napi::Value &v) {
    persistent[std::to_string(autoIndex++)] = v.As<Napi::Object>();
  }

  inline void persist(const Napi::Object &obj1, const Napi::Object &obj2) {
    persist(obj1);
    persist(obj2);
  }

  inline void persist(const std::vector<Napi::Object> &objs) {
    for (auto const &i : objs) persist(i);
  }

  // The promise form of an asynchronous method, used by the methods which take
  // a callback and by the getters, which never do. The worker takes over the
  // progress callback, exactly like the callback form below.
  inline Napi::Value promiseRun(const Napi::CallbackInfo &info) {
    auto *worker = new GDALPromiseWorker<GDALType>(info.Env(), progress, abort, main, rval, persistent, ds_uids);
    Napi::Value promise = worker->Promise();
    worker->Queue();
    return promise;
  }

  Napi::Value run(const Napi::CallbackInfo &info, bool async, int cb_arg) {
    // The rval lambdas look this up by name ("this"), so it cannot go through
    // the auto-indexed overload
    if (!info.This().IsEmpty() && info.This().IsObject()) persist("this", info.This());
    if (async) {
      if (progress) persist("progress_cb", progress->Value());
      // The callback is optional: without one the method returns the promise
      // itself. It is either at `cb_arg` or, when the caller left the optional
      // arguments in front of it out, the last argument of the call.
      const size_t length = info.Length();
      int num = -1;
      if (cb_arg >= 0 && static_cast<size_t>(cb_arg) < length && info[cb_arg].IsFunction()) {
        num = cb_arg;
      } else if (length > 0 && info[length - 1].IsFunction()) {
        num = static_cast<int>(length) - 1;
      }
      if (num < 0) { return promiseRun(info); }
      // The worker makes its own reference to the callback
      auto *worker = new GDALCallbackWorker<GDALType>(
        info[num].As<Napi::Function>(), progress, abort, main, rval, persistent, ds_uids);
      worker->Queue();
      return info.Env().Undefined();
    }
    // See the note in the promise overload below
    if (abort != nullptr && abort->aborted()) {
      abort->disarm();
      abort.reset();
      abortError(info.Env()).ThrowAsJavaScriptException();
      return info.Env().Undefined();
    }
    try {
      GDALExecutionProgress executionProgress(
        new GDALSyncExecutionProgress(progress), abort != nullptr ? abort->state : nullptr);
      AsyncGuard lock(ds_uids, eventLoopWarn);
      GDALType obj = main(executionProgress);
      // rval is the user function that will create the returned value
      // we give it a lambda that can access the persistent storage created for this operation
      return rval(obj, [this](const char *key) { return this->persistent[key]; });
    } catch (const GDALError &err) {
      reportSyncError(info.Env(), err);
    } catch (const char *err) {
      reportSyncError(info.Env(), GDALError(err != nullptr ? err : "Operation failed"));
    } catch (const std::exception &err) {
      reportSyncError(info.Env(), GDALError(err.what() != nullptr ? err.what() : "Operation failed"));
    }
    return info.Env().Undefined();
  }

  Napi::Value run(const Napi::CallbackInfo &info, bool async) {
    // See the note in the callback overload above
    if (!info.This().IsEmpty() && info.This().IsObject()) persist("this", info.This());
    if (async) { return promiseRun(info); }
    // In sync mode the event loop is blocked, so the listener can never run
    // while the operation is in progress - the signal is only useful for an
    // abort which has already happened. It is also the only sync case which is
    // not reported by the progress trampoline, so the error is built here.
    if (abort != nullptr && abort->aborted()) {
      // The listener is detached before the exception is thrown: nothing can
      // be called into JS anymore while one is pending, not even the
      // removeEventListener() of the destructors which run on the way out
      abort->disarm();
      abort.reset();
      abortError(info.Env()).ThrowAsJavaScriptException();
      return info.Env().Undefined();
    }
    try {
      GDALExecutionProgress executionProgress(
        new GDALSyncExecutionProgress(progress), abort != nullptr ? abort->state : nullptr);
      AsyncGuard lock(ds_uids, eventLoopWarn);
      GDALType obj = main(executionProgress);
      // rval is the user function that will create the returned value
      // we give it a lambda that can access the persistent storage created for this operation
      return rval(obj, [this](const char *key) { return this->persistent[key]; });
    } catch (const GDALError &err) {
      reportSyncError(info.Env(), err);
    } catch (const char *err) {
      reportSyncError(info.Env(), GDALError(err != nullptr ? err : "Operation failed"));
    } catch (const std::exception &err) {
      reportSyncError(info.Env(), GDALError(err.what() != nullptr ? err.what() : "Operation failed"));
    }
    return info.Env().Undefined();
  }

    private:
  // Reports the failure of a synchronous operation: an interruption which made
  // GDAL fail is an AbortError, everything else keeps the class and the code it
  // was raised with
  inline void reportSyncError(Napi::Env env, const GDALError &error) {
    if (abort != nullptr && abort->interrupted()) {
      // The listener is detached before the exception is thrown, nothing can be
      // called into JS anymore while one is pending
      abort->disarm();
      abort.reset();
      throwError(env, GDALError::aborted());
      return;
    }
    throwError(env, error);
  }

  std::map<std::string, Napi::Object> persistent;
  const std::vector<long> ds_uids;
  unsigned autoIndex;
};
} // namespace node_gdal
#endif
