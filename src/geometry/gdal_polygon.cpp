
#include "gdal_polygon.hpp"
#include "../collections/polygon_rings.hpp"
#include "../gdal_common.hpp"
#include "gdal_geometry.hpp"

#include <stdlib.h>

namespace node_gdal {

Napi::FunctionReference &Polygon::constructor() {
  return node_gdal::env_context().constructor<Polygon>();
}

void Polygon::SetPrivate(Napi::Object _this, Napi::Value value) {
  node_gdal::set_private(_this, "rings_", value);
};

/**
 * Concrete class representing polygons.
 *
 * @constructor
 * @class Polygon
 * @extends Geometry
 */

/**
 * Computes the area of the polygon.
 *
 * @method getArea
 * @instance
 * @memberof Polygon
 * @return {number}
 */
namespace {
// Polygon::getArea()
struct getAreaBody : node_gdal::SyncMethodBody {
  static Napi::Value run(const Napi::CallbackInfo &info) {
    auto obj = node_gdal::unwrapThis<Polygon>(info, "Polygon");
    if (!obj) { return obj.failure; }
    return Number::New(info.Env(), obj->get()->get_Area());
  }
};
} // namespace

GDAL_SYNC_METHOD(Polygon::getArea, getAreaBody)

/**
 * The rings that make up the polygon geometry.
 *
 * @kind member
 * @name rings
 * @instance
 * @memberof Polygon
 * @type {PolygonRings}
 */
namespace {

Napi::Value toString(const Napi::CallbackInfo &info) {
  return Napi::String::New(node_gdal::napi_env(), "Polygon");
}


Napi::Value ringsGetter(const Napi::CallbackInfo &info) {
  node_gdal::requireAlive<Polygon>(info.This(), "Polygon");
  return node_gdal::get_private(info.This(), "rings_");
}

} // namespace

void Polygon::Initialize(Napi::Object target) {
  Napi::Env env = target.Env();
  static constexpr MethodEntry methods[] = {
    method("toString", toString),
    method("getArea", getArea),
  };

  static constexpr AccessorEntry accessors[] = {
    attr("rings", ringsGetter, READ_ONLY_SETTER),
  };

  Napi::Function lcons = GDALDefineClass<Polygon>(env, "Polygon", methods, accessors);

  // lcons->Inherit() has no DefineClass equivalent
  node_gdal::Inherit(lcons, Geometry::constructor().Value());

  target.Set("Polygon", lcons);

  constructor() = Napi::Persistent(lcons);
}

} // namespace node_gdal
