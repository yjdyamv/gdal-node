
#include "gdal_polygon.hpp"
#include "../collections/polygon_rings.hpp"
#include "../gdal_common.hpp"
#include "gdal_geometry.hpp"

#include <stdlib.h>

namespace node_gdal {

Napi::FunctionReference &Polygon::constructor() {
  return node_gdal::env_context().constructor<Polygon>();
}

void Polygon::Initialize(Napi::Object target) {
  Napi::Env env = target.Env();
  SELF_CLASS(Polygon);

  // NOTE: the descriptor macros carry their own trailing comma
  Napi::Function lcons = GDALDefineClass<SELF>(env, "Polygon",
    {
        METHOD(toString)
        METHOD(getArea)
        ATTR(lcons, "rings", ringsGetter, READ_ONLY_SETTER)
    });

  // lcons->Inherit() has no DefineClass equivalent
  node_gdal::Inherit(lcons, Geometry::constructor().Value());

  target.Set("Polygon", lcons);

  constructor() = Napi::Persistent(lcons);
}

void Polygon::SetPrivate(Napi::Object _this, Napi::Value value) {
  GDAL_SET_PRIVATE(_this, "rings_", value);
};

/**
 * Concrete class representing polygons.
 *
 * @constructor
 * @class Polygon
 * @extends Geometry
 */

NAPI_METHOD(Polygon::toString) {
  return Napi::String::New(node_gdal::napi_env(), "Polygon");
}

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
NAPI_GETTER(Polygon::ringsGetter) {
  return GDAL_GET_PRIVATE(info.This(), "rings_");
}

} // namespace node_gdal
