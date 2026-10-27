#include "geometry_collection_children.hpp"
#include "../utils/args.hpp"
#include "../gdal_common.hpp"
#include "../geometry/gdal_geometry.hpp"
#include "../geometry/gdal_geometrycollection.hpp"

namespace node_gdal {

Napi::FunctionReference &GeometryCollectionChildren::constructor() {
  return node_gdal::env_context().constructor<GeometryCollectionChildren>();
}

GeometryCollectionChildren::GeometryCollectionChildren(const Napi::CallbackInfo &info) : GDALObject<GeometryCollectionChildren>(info) {
  if (info.Length() < 1 || !info[0].IsObject()) {
    Napi::Error::New(info.Env(), "Cannot create GeometryCollectionChildren directly").ThrowAsJavaScriptException();
    return;
  }
  node_gdal::set_private(info.This(), "parent_", info[0]);
}

GeometryCollectionChildren::~GeometryCollectionChildren() {
}

/**
 * A collection of Geometries, used by {@link GeometryCollection}.
 *
 * @class GeometryCollectionChildren
 */

Napi::Value GeometryCollectionChildren::New(Napi::Value geom) {

  std::vector<napi_value> args = {geom};
  Napi::Object obj = GeometryCollectionChildren::constructor().Value().New(args);

  return obj;
}

/**
 * Returns the number of items.
 *
 * @method count
 * @instance
 * @memberof GeometryCollectionChildren
 * @return {number}
 */
/**
 * Returns the geometry at the specified index.
 *
 * @method get
 * @instance
 * @memberof GeometryCollectionChildren
 * @param {number} index 0-based index
 * @throws {Error}
 * @return {Geometry}
 */
/**
 * Removes the geometry at the specified index.
 *
 * @method remove
 * @instance
 * @memberof GeometryCollectionChildren
 * @param {number} index 0-based index, -1 for all geometries
 */
/**
 * Adds geometry(s) to the collection.
 *
 * @example
 *
 * // one at a time:
 * geometryCollection.children.add(new Point(0,0,0));
 *
 * // add many at once:
 * geometryCollection.children.add([
 *     new Point(1,0,0),
 *     new Point(1,0,0)
 * ]);
 *
 * @method add
 * @instance
 * @memberof GeometryCollectionChildren
 * @param {Geometry|Geometry[]} geometry
 */
namespace {

Napi::Value toString(const Napi::CallbackInfo &info) {
  return Napi::String::New(node_gdal::napi_env(), "GeometryCollectionChildren");
}


Napi::Value count(const Napi::CallbackInfo &info) {

  Napi::Object parent =
    node_gdal::get_private(info.This(), "parent_").As<Napi::Object>();
  GeometryCollection *geom = node_gdal::UnwrapWrapped<GeometryCollection>(parent);

  return Napi::Number::New(node_gdal::napi_env(), geom->get()->getNumGeometries());
}


Napi::Value get(const Napi::CallbackInfo &info) {
  Args args(info);

  Napi::Object parent =
    node_gdal::get_private(info.This(), "parent_").As<Napi::Object>();
  GeometryCollection *geom = node_gdal::UnwrapWrapped<GeometryCollection>(parent);

  int i;
  if (!args.get(0, "index", i)) return args.failed();

  // the lookup below fails without setting an error when the index is out of
  // range, and fromGDALOr() would otherwise report the failure of an earlier
  // call
  CPLErrorReset();
  auto r = geom->get()->getGeometryRef(i);
  if (r == nullptr) {
    throw GDALError::fromGDALOr("Geometry not found: index " + std::to_string(i));
    return node_gdal::napi_env().Undefined();
  }
  return Geometry::New(r, false);
}


Napi::Value remove(const Napi::CallbackInfo &info) {
  Args args(info);

  Napi::Object parent =
    node_gdal::get_private(info.This(), "parent_").As<Napi::Object>();
  GeometryCollection *geom = node_gdal::UnwrapWrapped<GeometryCollection>(parent);

  int i;
  if (!args.get(0, "index", i)) return args.failed();

  OGRErr err = geom->get()->removeGeometry(i);
  if (err) {
    throw GDALError(getOGRErrMsg(err));
    return node_gdal::napi_env().Undefined();
  }

  return node_gdal::napi_env().Undefined();
}


Napi::Value add(const Napi::CallbackInfo &info) {

  Napi::Object parent =
    node_gdal::get_private(info.This(), "parent_").As<Napi::Object>();
  GeometryCollection *geom = node_gdal::UnwrapWrapped<GeometryCollection>(parent);

  Geometry *child;

  if (info.Length() < 1) {
    Napi::Error::New(node_gdal::napi_env(), "child(ren) must be given").ThrowAsJavaScriptException();
    return node_gdal::napi_env().Undefined();
  }
  if (info[0].IsArray()) {
    // set from array of geometry objects
    Napi::Array array = info[0].As<Napi::Array>();
    int length = array.Length();
    for (int i = 0; i < length; i++) {
      Napi::Value element = array.As<Napi::Object>().Get(i);
      if (node_gdal::IsInstanceOf<Geometry>(element)) {
        child = node_gdal::UnwrapWrapped<Geometry>(element.As<Napi::Object>());
        OGRErr err = geom->get()->addGeometry(child->get());
        if (err) {
          throw GDALError(getOGRErrMsg(err));
          return node_gdal::napi_env().Undefined();
        }
      } else {
        Napi::Error::New(node_gdal::napi_env(), "All array elements must be geometry objects").ThrowAsJavaScriptException();
        return node_gdal::napi_env().Undefined();
      }
    }
  } else if (node_gdal::IsInstanceOf<Geometry>(info[0])) {
    child = node_gdal::UnwrapWrapped<Geometry>(info[0].As<Napi::Object>());
    OGRErr err = geom->get()->addGeometry(child->get());
    if (err) {
      throw GDALError(getOGRErrMsg(err));
      return node_gdal::napi_env().Undefined();
    }
  } else {
    Napi::Error::New(node_gdal::napi_env(), "child must be a geometry object or array of geometry objects").ThrowAsJavaScriptException();
    return node_gdal::napi_env().Undefined();
  }

  return node_gdal::napi_env().Undefined();
}

} // namespace

void GeometryCollectionChildren::Initialize(Napi::Object target) {
  Napi::Env env = target.Env();
  static constexpr MethodEntry methods[] = {
    method("toString", toString),
    method("count", count),
    method("get", get),
    method("remove", remove),
    method("add", add),
  };

  Napi::Function lcons = GDALDefineClass<GeometryCollectionChildren>(env, "GeometryCollectionChildren", methods);

  target.Set("GeometryCollectionChildren", lcons);

  constructor() = Napi::Persistent(lcons);
}

} // namespace node_gdal
