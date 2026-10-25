#include "async.hpp"

namespace node_gdal {

// Per-thread: the addon can be loaded in several V8 isolates at the same time

// *message coming from GDAL points to a statically allocated buffer
GDALProgressInfo::GDALProgressInfo(double complete, const char *message) : complete(complete), message(message) {
}

GDALProgressInfo::GDALProgressInfo(const GDALProgressInfo &o) : complete(o.complete), message(o.message) {
}

GDALProgressInfo::GDALProgressInfo() : complete(0), message(nullptr) {
}

// The abort error which is delivered to the JS world, it has the same shape as
// the AbortError of the Node.js core (node:internal/errors)
Napi::Error abortError(Napi::Env env) {
  Napi::Error error = Napi::Error::New(env, "The operation was aborted");
  Napi::Object obj = error.Value();
  obj.Set("name", "AbortError");
  obj.Set("code", "ABORT_ERR");
  return error;
}

bool GDALAbortSignal::arm(const Napi::Value &value) {
  Napi::Env env = value.Env();
  if (value.IsEmpty() || !value.IsObject()) {
    Napi::TypeError::New(env, "abort_signal must be an AbortSignal").ThrowAsJavaScriptException();
    return false;
  }

  // Duck-typed on purpose: the AbortSignal of the Node.js core, but also
  // anything else which has the same shape, is accepted
  Napi::Object obj = value.As<Napi::Object>();
  Napi::Value aborted = obj.Get("aborted");
  Napi::Value add = obj.Get("addEventListener");
  Napi::Value remove = obj.Get("removeEventListener");
  if (!aborted.IsBoolean() || !add.IsFunction() || !remove.IsFunction()) {
    Napi::TypeError::New(env, "abort_signal must be an AbortSignal").ThrowAsJavaScriptException();
    return false;
  }

  state = std::make_shared<GDALAbortState>();
  state->aborted.store(aborted.As<Napi::Boolean>().Value());
  // The listener must not touch the signal object itself - it only stores the
  // flag which the aux thread reads
  std::shared_ptr<GDALAbortState> shared = state;
  listener = Napi::Persistent(Napi::Function::New(
    env,
    [shared](const Napi::CallbackInfo &info) {
      shared->aborted.store(true);
      return info.Env().Undefined();
    },
    "node-gdal:onAbort"));
  signal = Napi::Persistent(obj);

  try {
    add.As<Napi::Function>().Call(obj, {Napi::String::New(env, "abort"), listener.Value()});
  } catch (const Napi::Error &) {
    // The exception from the user-supplied addEventListener() is pending
    return false;
  }
  return true;
}

void GDALAbortSignal::disarm() {
  if (listener.IsEmpty() && signal.IsEmpty()) return;
  // Once the JS world is being torn down there is nothing left to unregister
  // and the V8 handles may not be used anymore
  EnvContext *ctx = EnvContext::active();
  if (ctx != nullptr && !ctx->shutting_down()) {
    try {
      // Calling into JS while an exception is pending would clear it - this
      // happens when the operation is aborted before it even starts and the
      // AbortError is thrown before the job is destroyed
      if (!signal.IsEmpty() && !listener.IsEmpty() && !signal.Env().IsExceptionPending()) {
        Napi::Object obj = signal.Value();
        obj.Get("removeEventListener")
          .As<Napi::Function>()
          .Call(obj, {Napi::String::New(signal.Env(), "abort"), listener.Value()});
      }
    } catch (const Napi::Error &) {
      // An exception from a user-supplied removeEventListener() is not worth
      // reporting, the operation has already finished
    }
  }
  listener.Reset();
  signal.Reset();
}

GDALAbortSignal::~GDALAbortSignal() {
  disarm();
}

// This is the GDAL form of the progress callback trampoline
// It can be invoked both in the main thread (in sync mode) or in auxillary thread (in async mode)
// It is essentially a gateway between the GDAL world and Node.js/V8 world
int ProgressTrampoline(double dfComplete, const char *pszMessage, void *pProgressArg) {
  GDALExecutionProgress *context = (GDALExecutionProgress *)pProgressArg;
  // Returning FALSE interrupts the GDAL operation - this is the only way to
  // interrupt an operation which has a progress callback, the AbortSignal only
  // sets the flag and GDAL polls it through this trampoline
  if (context->Aborted()) {
    // Remember that it was us who stopped GDAL: the failure which follows has to
    // be reported as an AbortError and not as a random GDAL error
    context->MarkInterrupted();
    return 0;
  }
  // The dispatcher in async.hpp will delete it
  GDALProgressInfo *info = new GDALProgressInfo(dfComplete, pszMessage);
  // Go to the dispatcher
  context->Send(info);
  return 1;
}

// From async.hpp:
// typedef Napi::AsyncProgressQueueWorker<GDALProgressInfo> GDALAsyncProgressWorker;
// typedef GDALAsyncProgressWorker::ExecutionProgress GDALAsyncExecutionProgress;
// GDALAsyncExecutionProgress is an instance of a node-addon-api templated class, in this case
// the AsyncWorker is the final owner of the progress_callback
GDALExecutionProgress::GDALExecutionProgress(
  const GDALAsyncExecutionProgress *async, const std::shared_ptr<GDALAbortState> &abort)
  : async(async), sync(nullptr), abort(abort) {
}
GDALExecutionProgress::GDALExecutionProgress(
  const GDALSyncExecutionProgress *sync, const std::shared_ptr<GDALAbortState> &abort)
  : async(nullptr), sync(sync), abort(abort) {
}

GDALExecutionProgress::~GDALExecutionProgress() {
  // The GDALAsyncExecutionProgress is owned by Node.js
  if (sync) delete sync;
}

// sync/async dispatcher
void GDALExecutionProgress::Send(GDALProgressInfo *info) const {
  auto infoHolder = std::unique_ptr<GDALProgressInfo>(info);
  // async mode -> we are in an aux thread, we can't go back to JS
  // we must enqueue a job on the event loop and wait for the JS world to stop
  // the enqueuing is in the async worker, then once the JS world is not running
  // AsyncWorker::OnProgress will get invoked on the main thread
  if (async) async->Send(info, 1);
  // sync mode -> the JS world is not running, we can go back directly
  // this code is below
  if (sync) sync->Send(info);
}

// This is the sync execution context, it is the final owner of the progress_callback
GDALSyncExecutionProgress::GDALSyncExecutionProgress(Napi::FunctionReference *cb) : progress_callback(cb) {};
GDALSyncExecutionProgress::~GDALSyncExecutionProgress() {
  if (progress_callback != nullptr) {
    // The reference destructor is suppressed by the argument macros
    progress_callback->Reset();
    delete progress_callback;
  }
};

// Going back to JS in sync mode
void GDALSyncExecutionProgress::Send(GDALProgressInfo *info) const {
  // There is no progress callback when the trampoline was installed only to
  // support an AbortSignal
  if (progress_callback == nullptr) return;
  std::vector<napi_value> argv;
  argv.push_back(Napi::Number::New(node_gdal::napi_env(), info->complete));
  argv.push_back(SafeString::New(node_gdal::napi_env(), info->message));
  try {
    progress_callback->Value().Call(argv);
  } catch (const Napi::Error &) {
    throw "sync progress callback exception";
  }
}

} // namespace node_gdal
