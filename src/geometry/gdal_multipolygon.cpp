#include "gdal_multipolygon.hpp"
#include "../collections/geometry_collection_children.hpp"
#include "../gdal_common.hpp"
#include "gdal_geometry.hpp"
#include "gdal_geometrycollection.hpp"
#include "gdal_polygon.hpp"

#include <stdlib.h>

namespace node_gdal {

Napi::FunctionReference &MultiPolygon::constructor() {
  return node_gdal::env_context().constructor<MultiPolygon>();
}

/**
 * @constructor
 * @class MultiPolygon
 * @extends GeometryCollection
 */

/**
 * Unions all the geometries and returns the result.
 *
 * @method unionCascaded
 * @instance
 * @memberof MultiPolygon
 * @return {Geometry}
 */
/**
 * Computes the combined area of the collection.
 *
 * @method getArea
 * @instance
 * @memberof MultiPolygon
 * @return {number}
 */
namespace {
// MultiPolygon::getArea()
struct getAreaBody : node_gdal::SyncMethodBody {
  static Napi::Value run(const Napi::CallbackInfo &info) {
    auto obj = node_gdal::unwrapThis<MultiPolygon>(info, "MultiPolygon");
    if (!obj) { return obj.failure; }
    return Number::New(info.Env(), obj->get()->get_Area());
  }
};
} // namespace

GDAL_SYNC_METHOD(MultiPolygon::getArea, getAreaBody)

namespace {

Napi::Value toString(const Napi::CallbackInfo &info) {
  return Napi::String::New(node_gdal::napi_env(), "MultiPolygon");
}


Napi::Value unionCascaded(const Napi::CallbackInfo &info) {

  MultiPolygon *geom = node_gdal::UnwrapWrapped<MultiPolygon>(info.This().As<Napi::Object>());
  auto r = geom->get()->UnionCascaded();
  if (r == nullptr) {
    throw GDALError::fromGDALOr("Cannot compute the union of the polygons");
    return node_gdal::napi_env().Undefined();
  }

  return Geometry::New(r);
}

} // namespace

void MultiPolygon::Initialize(Napi::Object target) {
  Napi::Env env = target.Env();
  static constexpr MethodEntry methods[] = {
    method("toString", toString),
    method("unionCascaded", unionCascaded),
    method("getArea", getArea),
  };

  Napi::Function lcons = GDALDefineClass<MultiPolygon>(env, "MultiPolygon", methods);

  // lcons->Inherit() has no DefineClass equivalent
  node_gdal::Inherit(lcons, GeometryCollection::constructor().Value());

  target.Set("MultiPolygon", lcons);

  constructor() = Napi::Persistent(lcons);
}

} // namespace node_gdal
