
#include "gdal_geometrycollection.hpp"
#include "../collections/geometry_collection_children.hpp"
#include "../gdal_common.hpp"
#include "gdal_geometry.hpp"

#include <stdlib.h>

namespace node_gdal {

Napi::FunctionReference &GeometryCollection::constructor() {
  return node_gdal::env_context().constructor<GeometryCollection>();
}

/**
 * A collection of 1 or more geometry objects.
 *
 * @constructor
 * @class GeometryCollection
 * @extends Geometry
 */
/**
 * Computes the combined area of the geometries.
 *
 * @method getArea
 * @instance
 * @memberof GeometryCollection
 * @return {number}
 */
namespace {
// GeometryCollection::getArea()
struct getAreaBody : node_gdal::SyncMethodBody {
  static Napi::Value run(const Napi::CallbackInfo &info) {
    auto obj = node_gdal::unwrapThis<GeometryCollection>(info, "GeometryCollection");
    if (!obj) { return obj.failure; }
    return Number::New(info.Env(), obj->get()->get_Area());
  }
};
} // namespace

GDAL_SYNC_METHOD(GeometryCollection::getArea, getAreaBody)

/**
 * Compute the length of a multicurve.
 *
 * @method getLength
 * @instance
 * @memberof GeometryCollection
 * @return {number}
 */
namespace {
// GeometryCollection::getLength()
struct getLengthBody : node_gdal::SyncMethodBody {
  static Napi::Value run(const Napi::CallbackInfo &info) {
    auto obj = node_gdal::unwrapThis<GeometryCollection>(info, "GeometryCollection");
    if (!obj) { return obj.failure; }
    return Number::New(info.Env(), obj->get()->get_Length());
  }
};
} // namespace

GDAL_SYNC_METHOD(GeometryCollection::getLength, getLengthBody)

/**
 * All geometries represented by this collection.
 *
 * @kind member
 * @name children
 * @instance
 * @memberof GeometryCollection
 * @type {GeometryCollectionChildren}
 */
namespace {

Napi::Value toString(const Napi::CallbackInfo &info) {
  return Napi::String::New(node_gdal::napi_env(), "GeometryCollection");
}


Napi::Value childrenGetter(const Napi::CallbackInfo &info) {
  return node_gdal::get_private(info.This(), "children_");
}

} // namespace

void GeometryCollection::Initialize(Napi::Object target) {
  Napi::Env env = target.Env();
  static constexpr MethodEntry methods[] = {
    method("toString", toString),
    method("getArea", getArea),
    method("getLength", getLength),
  };

  static constexpr AccessorEntry accessors[] = {
    attr("children", childrenGetter, READ_ONLY_SETTER),
  };

  Napi::Function lcons = GDALDefineClass<GeometryCollection>(env, "GeometryCollection", methods, accessors);

  // lcons->Inherit() has no DefineClass equivalent
  node_gdal::Inherit(lcons, Geometry::constructor().Value());

  target.Set("GeometryCollection", lcons);

  constructor() = Napi::Persistent(lcons);
}

} // namespace node_gdal
