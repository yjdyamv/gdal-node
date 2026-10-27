
#include "gdal_multilinestring.hpp"
#include "../collections/geometry_collection_children.hpp"
#include "../gdal_common.hpp"
#include "gdal_geometry.hpp"
#include "gdal_geometrycollection.hpp"
#include "gdal_linestring.hpp"

#include <stdlib.h>

namespace node_gdal {

Napi::FunctionReference &MultiLineString::constructor() {
  return node_gdal::env_context().constructor<MultiLineString>();
}

/**
 * @constructor
 * @class MultiLineString
 * @extends GeometryCollection
 */

/**
 * Converts it to a polygon.
 *
 * @method polygonize
 * @instance
 * @memberof MultiLineString
 * @return {Polygon}
 */
namespace {

Napi::Value toString(const Napi::CallbackInfo &info) {
  return Napi::String::New(node_gdal::napi_env(), "MultiLineString");
}


Napi::Value polygonize(const Napi::CallbackInfo &info) {

  MultiLineString *geom = node_gdal::UnwrapWrapped<MultiLineString>(info.This().As<Napi::Object>());

  return Geometry::New(geom->get()->Polygonize());
}

} // namespace

void MultiLineString::Initialize(Napi::Object target) {
  Napi::Env env = target.Env();
  static constexpr MethodEntry methods[] = {
    method("toString", toString),
    method("polygonize", polygonize),
  };

  Napi::Function lcons = GDALDefineClass<MultiLineString>(env, "MultiLineString", methods);

  // lcons->Inherit() has no DefineClass equivalent
  node_gdal::Inherit(lcons, GeometryCollection::constructor().Value());

  target.Set("MultiLineString", lcons);

  constructor() = Napi::Persistent(lcons);
}

} // namespace node_gdal
