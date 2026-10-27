#include "polygon_rings.hpp"
#include "../utils/args.hpp"
#include "../gdal_common.hpp"
#include "../geometry/gdal_geometry.hpp"
#include "../geometry/gdal_linearring.hpp"
#include "../geometry/gdal_polygon.hpp"

namespace node_gdal {

Napi::FunctionReference &PolygonRings::constructor() {
  return node_gdal::env_context().constructor<PolygonRings>();
}

PolygonRings::PolygonRings(const Napi::CallbackInfo &info) : GDALObject<PolygonRings>(info) {
  if (info.Length() < 1 || !info[0].IsObject()) {
    Napi::Error::New(info.Env(), "Cannot create PolygonRings directly").ThrowAsJavaScriptException();
    return;
  }
  node_gdal::set_private(info.This(), "parent_", info[0]);
}

PolygonRings::~PolygonRings() {
}

/**
 * A collection of polygon rings, used by {@link Polygon}.
 *
 * @class PolygonRings
 */

Napi::Value PolygonRings::New(Napi::Value geom) {

  std::vector<napi_value> args = {geom};
  Napi::Object obj = PolygonRings::constructor().Value().New(args);

  return obj;
}

/**
 * Returns the number of rings that exist in the collection.
 *
 * @method count
 * @instance
 * @memberof PolygonRings
 * @return {number}
 */
/**
 * Returns the ring at the specified index. The ring
 * at index `0` will always be the polygon's exterior ring.
 *
 * @example
 *
 * var exterior = polygon.rings.get(0);
 * var interior = polygon.rings.get(1);
 *
 * @method get
 * @instance
 * @memberof PolygonRings
 * @param {number} index
 * @throws {Error}
 * @return {LinearRing}
 */
/**
 * Adds a ring to the collection.
 *
 * @example
 *
 * var ring1 = new gdal.LinearRing();
 * ring1.points.add(0,0);
 * ring1.points.add(1,0);
 * ring1.points.add(1,1);
 * ring1.points.add(0,1);
 * ring1.points.add(0,0);
 *
 * // one at a time:
 * polygon.rings.add(ring1);
 *
 * // many at once:
 * polygon.rings.add([ring1, ...]);
 *
 * @method add
 * @instance
 * @memberof PolygonRings
 * @param {LinearRing|LinearRing[]} rings
 */
namespace {

Napi::Value toString(const Napi::CallbackInfo &info) {
  return Napi::String::New(node_gdal::napi_env(), "PolygonRings");
}


Napi::Value count(const Napi::CallbackInfo &info) {

  Napi::Object parent =
    node_gdal::get_private(info.This(), "parent_").As<Napi::Object>();
  Polygon *geom = node_gdal::UnwrapWrapped<Polygon>(parent);

  int i = geom->get()->getExteriorRing() ? 1 : 0;
  i += geom->get()->getNumInteriorRings();

  return Napi::Number::New(node_gdal::napi_env(), i);
}


Napi::Value get(const Napi::CallbackInfo &info) {
  Args args(info);

  Napi::Object parent =
    node_gdal::get_private(info.This(), "parent_").As<Napi::Object>();
  Polygon *geom = node_gdal::UnwrapWrapped<Polygon>(parent);

  int i;
  if (!args.get(0, "index", i)) return args.failed();

  // the lookup below fails without setting an error when the index is out of
  // range, and fromGDALOr() would otherwise report the failure of an earlier
  // call
  CPLErrorReset();
  OGRLinearRing *r;
  if (i == 0) {
    r = geom->get()->getExteriorRing();
  } else {
    r = geom->get()->getInteriorRing(i - 1);
  }
  if (r == nullptr) {
    throw GDALError::fromGDALOr("Ring not found: index " + std::to_string(i));
    return node_gdal::napi_env().Undefined();
  }
  return LinearRing::New(r, false);
}


Napi::Value add(const Napi::CallbackInfo &info) {

  Napi::Object parent =
    node_gdal::get_private(info.This(), "parent_").As<Napi::Object>();
  Polygon *geom = node_gdal::UnwrapWrapped<Polygon>(parent);

  LinearRing *ring;

  if (info.Length() < 1) {
    Napi::Error::New(node_gdal::napi_env(), "ring(s) must be given").ThrowAsJavaScriptException();
    return node_gdal::napi_env().Undefined();
  }
  if (info[0].IsArray()) {
    // set from array of geometry objects
    Napi::Array array = info[0].As<Napi::Array>();
    int length = array.Length();
    for (int i = 0; i < length; i++) {
      Napi::Value element = array.As<Napi::Object>().Get(i);
      if (node_gdal::IsInstanceOf<LinearRing>(element)) {
        ring = node_gdal::UnwrapWrapped<LinearRing>(element.As<Napi::Object>());
        OGRErr err = geom->get()->addRing(ring->get());
        if (err) {
          throw GDALError(getOGRErrMsg(err));
          return node_gdal::napi_env().Undefined();
        }
      } else {
        Napi::Error::New(node_gdal::napi_env(), "All array elements must be LinearRings").ThrowAsJavaScriptException();
        return node_gdal::napi_env().Undefined();
      }
    }
  } else if (node_gdal::IsInstanceOf<LinearRing>(info[0])) {
    ring = node_gdal::UnwrapWrapped<LinearRing>(info[0].As<Napi::Object>());
    OGRErr err = geom->get()->addRing(ring->get());
    if (err) {
      throw GDALError(getOGRErrMsg(err));
      return node_gdal::napi_env().Undefined();
    }
  } else {
    Napi::Error::New(node_gdal::napi_env(), "ring(s) must be a LinearRing or array of LinearRings").ThrowAsJavaScriptException();
    return node_gdal::napi_env().Undefined();
  }

  return node_gdal::napi_env().Undefined();
}

} // namespace

void PolygonRings::Initialize(Napi::Object target) {
  Napi::Env env = target.Env();
  static constexpr MethodEntry methods[] = {
    method("toString", toString),
    method("count", count),
    method("get", get),
    method("add", add),
  };

  Napi::Function lcons = GDALDefineClass<PolygonRings>(env, "PolygonRings", methods);

  target.Set("PolygonRings", lcons);

  constructor() = Napi::Persistent(lcons);
}

} // namespace node_gdal
