
#include "gdal_simplecurve.hpp"
#include "../utils/args.hpp"
#include "../collections/linestring_points.hpp"
#include "../gdal_common.hpp"
#include "gdal_geometry.hpp"
#include "gdal_point.hpp"
#include "gdal_linestring.hpp"

#include <stdlib.h>

namespace node_gdal {

Napi::FunctionReference &SimpleCurve::constructor() {
  return node_gdal::env_context().constructor<SimpleCurve>();
}

/**
 * Abstract class representing all SimpleCurves.
 *
 * @constructor
 * @class SimpleCurve
 * @extends Geometry
 */
SimpleCurve::SimpleCurve(const Napi::CallbackInfo &info)
  : CurveBase<SimpleCurve, OGRSimpleCurve, LineStringPoints>(info) {
  Napi::Error::New(info.Env(), "SimpleCurve is an abstract class and cannot be instantiated")
    .ThrowAsJavaScriptException();
}


/**
 * Returns the point at the specified distance along the SimpleCurve.
 *
 * @method value
 * @instance
 * @memberof SimpleCurve
 * @param {number} distance
 * @return {Point}
 * @throws {RangeError} the curve has no points
 */
/**
 * Compute the length of a multiSimpleCurve.
 *
 * @method getLength
 * @instance
 * @memberof SimpleCurve
 * @return {number}
 */
namespace {
// SimpleCurve::getLength()
struct getLengthBody : node_gdal::SyncMethodBody {
  static Napi::Value run(const Napi::CallbackInfo &info) {
    auto obj = node_gdal::unwrapThis<SimpleCurve>(info, "SimpleCurve");
    if (!obj) { return obj.failure; }
    return Number::New(info.Env(), obj->get()->get_Length());
  }
};
} // namespace

GDAL_SYNC_METHOD(SimpleCurve::getLength, getLengthBody)

/**
 * The points that make up the SimpleCurve geometry.
 *
 * @kind member
 * @name points
 * @instance
 * @memberof SimpleCurve
 * @type {LineStringPoints}
 */
/**
 * Add a segment of another LineString to this SimpleCurve subtype.
 *
 * Adds the request range of vertices to the end of this compound curve in an
 * efficient manner. If the start index is larger than the end index then the
 * vertices will be reversed as they are copied.
 *
 * @method addSubLineString
 * @instance
 * @memberof SimpleCurve
 * @param {LineString} LineString to be added
 * @param {number} [start=0] the first vertex to copy, defaults to 0 to start with
 * the first vertex in the other LineString
 * @param {number} [end=-1] the last vertex to copy, defaults to -1 indicating the
 * last vertex of the other LineString
 * @return {void}
 */
namespace {

Napi::Value toString(const Napi::CallbackInfo &info) {
  return Napi::String::New(node_gdal::napi_env(), "SimpleCurve");
}


Napi::Value value(const Napi::CallbackInfo &info) {
  Args args(info);

  SimpleCurve *geom = node_gdal::UnwrapWrapped<SimpleCurve>(info.This().As<Napi::Object>());

  double dist;

  if (!args.get(0, "distance", dist)) return args.failed();
  // `OGRSimpleCurve::Value()` reads the first and the last vertex of the curve
  // without looking whether there is one, so asking for the point of an empty
  // curve segfaults the process
  if (geom->get()->getNumPoints() == 0) {
    Napi::RangeError::New(node_gdal::napi_env(), "Cannot get the value of an empty curve").ThrowAsJavaScriptException();
    return node_gdal::napi_env().Undefined();
  }

  OGRPoint *pt = new OGRPoint();
  geom->get()->Value(dist, pt);

  return Point::New(pt);
}


Napi::Value pointsGetter(const Napi::CallbackInfo &info) {
  return node_gdal::get_private(info.This(), "points_");
}


Napi::Value addSubLineString(const Napi::CallbackInfo &info) {
  Args args(info);

  SimpleCurve *geom = node_gdal::UnwrapWrapped<SimpleCurve>(info.This().As<Napi::Object>());
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

  geom->get()->addSubLineString(other->get(), start, end);

  geom->updateAmountOfGeometryMemory();

  return node_gdal::napi_env().Undefined();
}

} // namespace

void SimpleCurve::Initialize(Napi::Object target) {
  Napi::Env env = target.Env();
  static constexpr MethodEntry methods[] = {
    method("toString", toString),
    method("getLength", getLength),
    method("value", value),
    method("addSubLineString", addSubLineString),
  };

  static constexpr AccessorEntry accessors[] = {
    attr("points", pointsGetter, READ_ONLY_SETTER),
  };

  Napi::Function lcons = GDALDefineClass<SimpleCurve>(env, "SimpleCurve", methods, accessors);

  // lcons->Inherit() has no DefineClass equivalent
  node_gdal::Inherit(lcons, Geometry::constructor().Value());

  target.Set("SimpleCurve", lcons);

  constructor() = Napi::Persistent(lcons);
}

} // namespace node_gdal
