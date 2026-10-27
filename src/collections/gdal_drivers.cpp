#include "gdal_drivers.hpp"
#include "../gdal_common.hpp"
#include "../gdal_driver.hpp"

#include <climits>
#include <cmath>

namespace node_gdal {

Napi::FunctionReference &GDALDrivers::constructor() {
  return node_gdal::env_context().constructor<GDALDrivers>();
}

GDALDrivers::GDALDrivers(const Napi::CallbackInfo &info) : GDALObject<GDALDrivers>(info) {
  // `gdal.drivers` is created by the module; the class is not constructible
  // from JS, which the test suite asserts
  if (info.Length() > 0 && info[0].IsBoolean()) return;
  Napi::Error::New(info.Env(), "Cannot create GDALDrivers directly").ThrowAsJavaScriptException();
}

GDALDrivers::~GDALDrivers() {
}

/**
 * An collection of all {@link Driver}
 * registered with GDAL.
 *
 * @class GDALDrivers
 */

Napi::Value GDALDrivers::New() {

  std::vector<napi_value> args = {Napi::Boolean::New(node_gdal::napi_env(), true)};
  Napi::Object obj = GDALDrivers::constructor().Value().New(args);

  return obj;
}

/**
 * Returns a driver with the specified name.
 *
 * Note: Prior to GDAL2.x there is a separate driver for vector VRTs and raster
 * VRTs. Use `"VRT:vector"` to fetch the vector VRT driver and `"VRT:raster"` to
 * fetch the raster VRT driver.
 *
 * @method get
 * @instance
 * @memberof GDALDrivers
 * @param {number|string} index 0-based index or driver name
 * @throws {Error}
 * @return {Driver}
 */
/**
 * Returns an array with the names of all the drivers registered with GDAL.
 *
 * @method getNames
 * @instance
 * @memberof GDALDrivers
 * @return {string[]}
 */
/**
 * Returns the number of drivers registered with GDAL.
 *
 * @method count
 * @instance
 * @memberof GDALDrivers
 * @return {number}
 */
namespace {

Napi::Value toString(const Napi::CallbackInfo &info) {
  return Napi::String::New(node_gdal::napi_env(), "GDALDrivers");
}


Napi::Value get(const Napi::CallbackInfo &info) {

  GDALDriver *gdal_driver;

  if (info.Length() == 0) {
    Napi::Error::New(node_gdal::napi_env(), "Either driver name or index must be provided").ThrowAsJavaScriptException();
    return node_gdal::napi_env().Undefined();
  }

  if (info[0].IsString()) {
    // try getting OGR driver first, and then GDAL driver if it fails
    // A driver named "VRT" exists for both GDAL and OGR, so if building
    // with <2.0 require user to specify which driver to pick
    std::string name = info[0].As<Napi::String>().Utf8Value();

    if (name == "VRT:vector") { name = "VRT"; }

    if (name == "VRT:raster") { name = "VRT"; }
    gdal_driver = GetGDALDriverManager()->GetDriverByName(name.c_str());
    if (gdal_driver) {
      return Driver::New(gdal_driver);
    }

    // A missing driver is not a failure of GDAL, so there is no error to
    // report: GDALError::fromGDAL() would hand back whatever the last
    // operation to fail left behind, which has nothing to do with the lookup
    Napi::Error::New(node_gdal::napi_env(), "Cannot find driver: " + name).ThrowAsJavaScriptException();
    return node_gdal::napi_env().Undefined();

  } else if (info[0].IsNumber()) {
    // The number is narrowed to an int below, which wraps around for a value
    // which is not finite or which does not fit in an int: `2 ** 32` became
    // driver 0 and `2 ** 31` was reported back as `-2147483648`. Such a value
    // cannot name a driver, so it is refused with the number which was given
    const double value = info[0].As<Napi::Number>().DoubleValue();

    if (!std::isfinite(value) || value < INT_MIN || value > INT_MAX) {
      char number[32];
      std::snprintf(number, sizeof(number), "%g", value);      Napi::Error::New(node_gdal::napi_env(), std::string("Cannot find driver with index: ") + number).ThrowAsJavaScriptException();
      return node_gdal::napi_env().Undefined();
    }

    int i = static_cast<int>(value);

    gdal_driver = GetGDALDriverManager()->GetDriver(i);
    if (gdal_driver) {
      return Driver::New(gdal_driver);
    }

    Napi::Error::New(node_gdal::napi_env(), "Cannot find driver with index: " + std::to_string(i)).ThrowAsJavaScriptException();
    return node_gdal::napi_env().Undefined();

  } else {
    Napi::Error::New(node_gdal::napi_env(), "Argument must be string or integer").ThrowAsJavaScriptException();
    return node_gdal::napi_env().Undefined();
  }
}


Napi::Value getNames(const Napi::CallbackInfo &info) {
  int gdal_count = GetGDALDriverManager()->GetDriverCount();
  int i, ogr_count = 0;
  std::string name;

  int n = gdal_count + ogr_count;

  Napi::Array driver_names = Napi::Array::New(node_gdal::napi_env(), n);

  for (i = 0; i < gdal_count; ++i) {
    GDALDriver *driver = GetGDALDriverManager()->GetDriver(i);
    name = driver->GetDescription();
    driver_names.Set( i, SafeString::New(name.c_str()));
  }

  return driver_names;
}


Napi::Value count(const Napi::CallbackInfo &info) {

  int count = GetGDALDriverManager()->GetDriverCount();

  return Napi::Number::New(node_gdal::napi_env(), count);
}

} // namespace

void GDALDrivers::Initialize(Napi::Object target) {
  Napi::Env env = target.Env();
  static constexpr MethodEntry methods[] = {
    method("toString", toString),
    method("count", count),
    method("get", get),
    method("getNames", getNames),
  };

  Napi::Function lcons = GDALDefineClass<GDALDrivers>(env, "GDALDrivers", methods);

  GDALAllRegister();

  target.Set("GDALDrivers", lcons);

  constructor() = Napi::Persistent(lcons);
}

} // namespace node_gdal
