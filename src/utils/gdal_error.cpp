#include "gdal_error.hpp"

#include <cpl_error.h>

namespace node_gdal {

GDALError GDALError::fromGDAL() {
  const char *message = CPLGetLastErrorMsg();
  // A GDAL call which fails without setting a CPL error reports an empty
  // message, which node-addon-api would take for a success
  return GDALError(message != nullptr && *message != '\0' ? message : "Operation failed");
}

// The class of a JS error is its 'name' property: the subclasses of
// Napi::Error set it themselves, everything else has to set it here
static Napi::Error annotate(Napi::Error js, const GDALError &error) {
  if (error.name() != "Error" && !error.name().empty()) js.Value().Set("name", error.name());
  if (!error.code().empty()) js.Value().Set("code", error.code());
  return js;
}

Napi::Error toJS(Napi::Env env, const GDALError &error) {
  if (error.name() == "TypeError") return annotate(Napi::TypeError::New(env, error.message()), error);
  if (error.name() == "RangeError") return annotate(Napi::RangeError::New(env, error.message()), error);
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
