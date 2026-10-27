
#include "gdal_linestring.hpp"
#include "../collections/linestring_points.hpp"
#include "../gdal_common.hpp"
#include "gdal_geometry.hpp"
#include "gdal_simplecurve.hpp"
#include "gdal_point.hpp"

#include <stdlib.h>

namespace node_gdal {

Napi::FunctionReference &LineString::constructor() {
  return node_gdal::env_context().constructor<LineString>();
}

/**
 * Concrete representation of a multi-vertex line.
 *
 * @example
 *
 * var lineString = new gdal.LineString();
 * lineString.points.add(new gdal.Point(0,0));
 * lineString.points.add(new gdal.Point(0,10));
 *
 * @constructor
 * @class LineString
 * @extends SimpleCurve
 */

namespace {

Napi::Value toString(const Napi::CallbackInfo &info) {
  return Napi::String::New(node_gdal::napi_env(), "LineString");
}

} // namespace

void LineString::Initialize(Napi::Object target) {
  Napi::Env env = target.Env();
  static constexpr MethodEntry methods[] = {
    method("toString", toString),
  };

  Napi::Function lcons = GDALDefineClass<LineString>(env, "LineString", methods);

  // lcons->Inherit() has no DefineClass equivalent
  node_gdal::Inherit(lcons, SimpleCurve::constructor().Value());

  target.Set("LineString", lcons);

  constructor() = Napi::Persistent(lcons);
}

} // namespace node_gdal
