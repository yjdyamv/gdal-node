
#include "gdal_multicurve.hpp"
#include "../collections/geometry_collection_children.hpp"
#include "../gdal_common.hpp"
#include "gdal_geometry.hpp"
#include "gdal_geometrycollection.hpp"

#include <stdlib.h>

namespace node_gdal {

Napi::FunctionReference &MultiCurve::constructor() {
  return node_gdal::env_context().constructor<MultiCurve>();
}

/**
 * @constructor
 * @class MultiCurve
 * @extends GeometryCollection
 */

/**
 * Builds the polygons which the lines of the collection form.
 *
 * @method polygonize
 * @instance
 * @memberof MultiCurve
 * @return {GeometryCollection} The polygons which the lines form, `null` if they do not close
 */
namespace {

Napi::Value toString(const Napi::CallbackInfo &info) {
  return Napi::String::New(node_gdal::napi_env(), "MultiCurve");
}


Napi::Value polygonize(const Napi::CallbackInfo &info) {

  MultiCurve *geom = node_gdal::UnwrapWrapped<MultiCurve>(info.This().As<Napi::Object>());

  return Geometry::New(geom->get()->Polygonize());
}

} // namespace

void MultiCurve::Initialize(Napi::Object target) {
  Napi::Env env = target.Env();
  static constexpr MethodEntry methods[] = {
    method("toString", toString),
    method("polygonize", polygonize),
  };

  Napi::Function lcons = GDALDefineClass<MultiCurve>(env, "MultiCurve", methods);

  // lcons->Inherit() has no DefineClass equivalent
  node_gdal::Inherit(lcons, GeometryCollection::constructor().Value());

  target.Set("MultiCurve", lcons);

  constructor() = Napi::Persistent(lcons);
}

} // namespace node_gdal
