#include "gdal_error.hpp"
#include "env_context.hpp"

#include <cpl_error.h>

namespace node_gdal {

GDALError GDALError::fromGDAL() {
  const char *message = CPLGetLastErrorMsg();
  // A GDAL call which fails without setting a CPL error reports an empty
  // message, which node-addon-api would take for a success
  const std::string text = message != nullptr && *message != '\0' ? message : "Operation failed";

  // The class and the code of the JS error follow the error number, so that a
  // caller can tell a missing file from a refused write with `instanceof`.
  // `CPLE_AppDefined` is everything which GDAL itself did not classify, and it
  // is the code of the base class
  switch (CPLGetLastErrorNo()) {
  case CPLE_AppDefined: return GDALError(text, "GDALError", "CPLE_AppDefined");
  case CPLE_OutOfMemory: return GDALError(text, "GDALOutOfMemoryError", "CPLE_OutOfMemory");
  case CPLE_FileIO: return GDALError(text, "GDALFileIOError", "CPLE_FileIO");
  case CPLE_OpenFailed: return GDALError(text, "GDALOpenError", "CPLE_OpenFailed");
  case CPLE_IllegalArg: return GDALError(text, "GDALIllegalArgumentError", "CPLE_IllegalArg");
  case CPLE_NotSupported: return GDALError(text, "GDALUnsupportedError", "CPLE_NotSupported");
  case CPLE_AssertionFailed: return GDALError(text, "GDALAssertionError", "CPLE_AssertionFailed");
  case CPLE_NoWriteAccess: return GDALError(text, "GDALNoWriteAccessError", "CPLE_NoWriteAccess");
  case CPLE_UserInterrupt: return GDALError(text, "GDALUserInterruptError", "CPLE_UserInterrupt");
  case CPLE_ObjectNull: return GDALError(text, "GDALObjectNullError", "CPLE_ObjectNull");
  case CPLE_HttpResponse: return GDALError(text, "GDALHttpError", "CPLE_HttpResponse");
  // the object storage failures of /vsicurl, /vsis3 and the rest of the virtual
  // file systems which talk to a server - they share one class
  case CPLE_BucketNotFound: return GDALError(text, "GDALAWSError", "CPLE_AWSBucketNotFound");
  case CPLE_ObjectNotFound: return GDALError(text, "GDALAWSError", "CPLE_AWSObjectNotFound");
  case CPLE_AccessDenied: return GDALError(text, "GDALAWSError", "CPLE_AWSAccessDenied");
  case CPLE_InvalidCredentials: return GDALError(text, "GDALAWSError", "CPLE_AWSInvalidCredentials");
  case CPLE_SignatureDoesNotMatch: return GDALError(text, "GDALAWSError", "CPLE_AWSSignatureDoesNotMatch");
  case CPLE_ObjectStorageGenericError: return GDALError(text, "GDALAWSError", "CPLE_AWSError");
  default: return GDALError(text);
  }
}

namespace {

// The name of the class of `error`: the base class is the one of everything
// which does not name a subclass
const char *className(const GDALError &error) {
  if (error.name().empty() || error.name() == "Error") return "GDALError";
  return error.name().c_str();
}

// Sets the prototype of `object`. A JS class is nothing but a constructor whose
// prototype is the parent of the prototype of its instances, and neither of the
// two can be written with N-API, so the two members of `Object` which do it are
// used instead
void setPrototype(Napi::Env env, Napi::Object object, Napi::Value prototype) {
  env.Global()
    .Get("Object")
    .As<Napi::Object>()
    .Get("setPrototypeOf")
    .As<Napi::Function>()
    .Call({object, prototype});
}

// Creates a class whose instances are `Error`s with the prototype of the class.
// The constructor of `Error` cannot be called with the prototype of a subclass,
// so the error is built first and its prototype is replaced afterwards, which is
// what makes `instanceof` see the class and `Error.prototype.stack` work.
Napi::Function makeErrorClass(Napi::Env env, const char *name, const char *code, Napi::Value parent) {
  Napi::Function js_class = Napi::Function::New(
    env,
    [](const Napi::CallbackInfo &info) -> Napi::Value {
      Napi::Env env = info.Env();
      std::string message =
        info.Length() > 0 && info[0].IsString() ? info[0].As<Napi::String>().Utf8Value() : "Operation failed";
      Napi::Object error = Napi::Error::New(env, message).Value().As<Napi::Object>();
      // `new.target` is the class which is being constructed, which is the
      // class of the error which is being built
      Napi::Value target = info.NewTarget();
      if (target.IsFunction()) setPrototype(env, error, target.As<Napi::Function>().Get("prototype"));
      // a code which is not the one of the class
      if (info.Length() > 1 && info[1].IsString()) error.Set("code", info[1]);
      return error;
    },
    name);

  Napi::Object prototype = js_class.Get("prototype").As<Napi::Object>();
  setPrototype(env, prototype, parent);
  prototype.Set("name", name);
  if (code != nullptr) prototype.Set("code", code);
  return js_class;
}

} // namespace

void initErrorClasses(Napi::Env env, Napi::Object target) {
  Napi::Function error = env.Global().Get("Error").As<Napi::Function>();
  Napi::Object error_prototype = error.Get("prototype").As<Napi::Object>();

  // the class of every failure which GDAL reported
  Napi::Function base = makeErrorClass(env, "GDALError", nullptr, error_prototype);
  target.Set("GDALError", base);
  auto &classes = EnvContext::find(env)->error_classes();
  classes.emplace("GDALError", Napi::Persistent(base));

  // one class per CPLE error number, see GDALError::fromGDAL()
  static const struct {
    const char *name;
    const char *code;
  } subclasses[] = {
    {"GDALOutOfMemoryError", "CPLE_OutOfMemory"},
    {"GDALFileIOError", "CPLE_FileIO"},
    {"GDALOpenError", "CPLE_OpenFailed"},
    {"GDALIllegalArgumentError", "CPLE_IllegalArg"},
    {"GDALUnsupportedError", "CPLE_NotSupported"},
    {"GDALAssertionError", "CPLE_AssertionFailed"},
    {"GDALNoWriteAccessError", "CPLE_NoWriteAccess"},
    {"GDALUserInterruptError", "CPLE_UserInterrupt"},
    {"GDALObjectNullError", "CPLE_ObjectNull"},
    {"GDALHttpError", "CPLE_HttpResponse"},
    {"GDALAWSError", "CPLE_AWSError"},
  };

  Napi::Value base_prototype = base.Get("prototype");
  for (const auto &subclass : subclasses) {
    Napi::Function js_class = makeErrorClass(env, subclass.name, subclass.code, base_prototype);
    target.Set(subclass.name, js_class);
    classes.emplace(subclass.name, Napi::Persistent(js_class));
  }

  // the class of an operation which its AbortSignal interrupted, which has the
  // shape of the AbortError of the Node.js core
  Napi::Function aborted = makeErrorClass(env, "AbortError", "ABORT_ERR", base_prototype);
  target.Set("AbortError", aborted);
  classes.emplace("AbortError", Napi::Persistent(aborted));
}

// The class of a JS error is its 'name' property: the classes of the failures
// set it on their prototype, everything else has to set it here
static Napi::Error annotate(Napi::Error js, const GDALError &error) {
  if (error.name() != "Error" && !error.name().empty()) js.Value().Set("name", error.name());
  if (!error.code().empty()) js.Value().Set("code", error.code());
  return js;
}

Napi::Error toJS(Napi::Env env, const GDALError &error) {
  // the errors of a bad argument keep their Node.js class
  if (error.name() == "TypeError") return Napi::TypeError::New(env, error.message());
  if (error.name() == "RangeError") return Napi::RangeError::New(env, error.message());

  auto &classes = EnvContext::find(env)->error_classes();
  auto found = classes.find(className(error));
  if (found != classes.end() && !found->second.IsEmpty()) {
    Napi::Object js = found->second.New({Napi::String::New(env, error.message())});
    // a code which is not the one of the class, see GDALError::fromGDAL()
    if (!error.code().empty()) js.Set("code", error.code());
    return Napi::Error(env, js);
  }

  // the classes are not there, which happens only before the module is
  // initialized
  return annotate(Napi::Error::New(env, error.message()), error);
}

void throwError(Napi::Env env, const GDALError &error) {
  // An exception which is already pending is left alone: nothing can be called
  // into JS while one is, and a second one would replace it silently
  if (env.IsExceptionPending()) return;
  toJS(env, error).ThrowAsJavaScriptException();
}

Napi::Value throwTypeError(Napi::Env env, const std::string &message) {
  throwError(env, GDALError(message, "TypeError"));
  return env.Undefined();
}

Napi::Value throwRangeError(Napi::Env env, const std::string &message) {
  throwError(env, GDALError(message, "RangeError"));
  return env.Undefined();
}

} // namespace node_gdal
