
#include "gdal_multipoint.hpp"
#include "../collections/geometry_collection_children.hpp"
#include "../gdal_common.hpp"
#include "gdal_geometry.hpp"
#include "gdal_geometrycollection.hpp"
#include "gdal_point.hpp"

#include <stdlib.h>

namespace node_gdal {

Napi::FunctionReference &MultiPoint::constructor() {
  return node_gdal::env_context().constructor<MultiPoint>();
}

/**
 * @constructor
 * @class MultiPoint
 * @extends GeometryCollection
 */

namespace {

Napi::Value toString(const Napi::CallbackInfo &info) {
  return Napi::String::New(node_gdal::napi_env(), "MultiPoint");
}

} // namespace

void MultiPoint::Initialize(Napi::Object target) {
  Napi::Env env = target.Env();
  static constexpr MethodEntry methods[] = {
    method("toString", toString),
  };

  Napi::Function lcons = GDALDefineClass<MultiPoint>(env, "MultiPoint", methods);

  // lcons->Inherit() has no DefineClass equivalent
  node_gdal::Inherit(lcons, GeometryCollection::constructor().Value());

  target.Set("MultiPoint", lcons);

  constructor() = Napi::Persistent(lcons);
}

} // namespace node_gdal
