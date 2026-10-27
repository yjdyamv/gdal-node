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

void MultiPolygon::Initialize(Napi::Object target) {
  Napi::Env env = target.Env();
  SELF_CLASS(MultiPolygon);

  // NOTE: the descriptor macros carry their own trailing comma
  Napi::Function lcons = GDALDefineClass<SELF>(env, "MultiPolygon",
    {
        METHOD(toString)
        METHOD(unionCascaded)
        METHOD(getArea)
    });

  // lcons->Inherit() has no DefineClass equivalent
  node_gdal::Inherit(lcons, GeometryCollection::constructor().Value());

  target.Set("MultiPolygon", lcons);

  constructor() = Napi::Persistent(lcons);
}

/**
 * @constructor
 * @class MultiPolygon
 * @extends GeometryCollection
 */

NAPI_METHOD(MultiPolygon::toString) {
  return Napi::String::New(node_gdal::napi_env(), "MultiPolygon");
}

/**
 * Unions all the geometries and returns the result.
 *
 * @method unionCascaded
 * @instance
 * @memberof MultiPolygon
 * @return {Geometry}
 */
NAPI_METHOD(MultiPolygon::unionCascaded) {

  MultiPolygon *geom = node_gdal::UnwrapWrapped<MultiPolygon>(info.This().As<Napi::Object>());
  auto r = geom->this_->UnionCascaded();
  if (r == nullptr) {
    NODE_THROW_LAST_CPLERR;
    return node_gdal::napi_env().Undefined();
  }

  return Geometry::New(r);
}

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

} // namespace node_gdal
