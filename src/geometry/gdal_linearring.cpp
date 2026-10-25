
#include "gdal_linearring.hpp"
#include "../utils/args.hpp"
#include "../collections/linestring_points.hpp"
#include "../gdal_common.hpp"
#include "gdal_geometry.hpp"
#include "gdal_linestring.hpp"

#include <stdlib.h>

namespace node_gdal {

Napi::FunctionReference &LinearRing::constructor() {
  return node_gdal::env_context().constructor<LinearRing>();
}

void LinearRing::Initialize(Napi::Object target) {
  Napi::Env env = target.Env();
  SELF_CLASS(LinearRing);

  // NOTE: the descriptor macros carry their own trailing comma
  Napi::Function lcons = GDALDefineClass<SELF>(env, "LinearRing",
    {
        METHOD(toString)
        METHOD(getArea)
        METHOD(addSubLineString)
    });

  // lcons->Inherit() has no DefineClass equivalent
  node_gdal::Inherit(lcons, LineString::constructor().Value());

  target.Set("LinearRing", lcons);

  constructor() = Napi::Persistent(lcons);
}

Napi::Value LinearRing::New(OGRLinearRing *geom, bool owned) {

  if (!geom) { return node_gdal::napi_env().Null(); }

  // make a copy of geometry owned by a feature
  // + no need to track when a feature is destroyed
  // + no need to throw errors when a method trys to modify an owned read-only
  // geometry
  // - is slower

  if (!owned) { geom = static_cast<OGRLinearRing *>(geom->clone()); }

  node_gdal::detail::PendingObjectGuard pending(geom);
  std::vector<napi_value> args;
  Napi::Object obj = LinearRing::constructor().Value().New(args);
  LinearRing *wrapped = node_gdal::UnwrapWrapped<LinearRing>(obj);

  return obj;
}

/**
 * Concrete representation of a closed ring.
 *
 * @constructor
 * @class LinearRing
 * @extends LineString
 */

NAPI_METHOD(LinearRing::toString) {
  return Napi::String::New(node_gdal::napi_env(), "LinearRing");
}

/**
 * Computes the area enclosed by the ring.
 *
 * @method getArea
 * @instance
 * @memberof LinearRing
 * @return {number}
 */
NODE_WRAPPED_METHOD_WITH_RESULT(LinearRing, getArea, Number, get_Area);

NAPI_METHOD(LinearRing::addSubLineString) {
  Args args(info);

  LinearRing *geom = node_gdal::UnwrapWrapped<LinearRing>(info.This().As<Napi::Object>());
  LineString *other;
  int start = 0;
  int end = -1;

  if (!args.getWrapped(0, "line", "LineString", other)) return args.failed();
  if (!args.optional(1, "start", start)) return args.failed();
  if (!args.optional(2, "end", end)) return args.failed();

  int n = other->get()->getNumPoints();

  if (start < 0 || end < -1 || start >= n || end >= n) {
    Napi::RangeError::New(node_gdal::napi_env(), "Invalid start or end index for LineString").ThrowAsJavaScriptException();
    return node_gdal::napi_env().Undefined();
  }

  geom->this_->addSubLineString(other->get(), start, end);

  return node_gdal::napi_env().Undefined();
}

} // namespace node_gdal
