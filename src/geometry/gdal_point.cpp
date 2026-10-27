#include "gdal_point.hpp"
#include "../gdal_common.hpp"
#include "gdal_geometry.hpp"

#include <stdlib.h>

namespace node_gdal {

Napi::FunctionReference &Point::constructor() {
  return node_gdal::env_context().constructor<Point>();
}

/**
 * Point class.
 *
 * @constructor
 * @class Point
 * @extends Geometry
 * @param {number} x
 * @param {number} y
 * @param {number} [z]
 */
Point::Point(const Napi::CallbackInfo &info) : GeometryBase<Point, OGRPoint>(info) {
  if (info.Length() == 1) {
    Napi::Error::New(info.Env(), "Point constructor must be given 0, 2, or 3 arguments").ThrowAsJavaScriptException();
    return;
  }

  // GeometryBase<Point, OGRPoint>() has already created an empty OGRPoint,
  // which is (0, 0) - apply the arguments to it
  if (info.Length() > 0) this_->setX(info[0].As<Napi::Number>().DoubleValue());
  if (info.Length() > 1) this_->setY(info[1].As<Napi::Number>().DoubleValue());
  if (info.Length() > 2) this_->setZ(info[2].As<Napi::Number>().DoubleValue());
}


/**
 * @kind member
 * @name x
 * @instance
 * @memberof Point
 * @type {number}
 */
/**
 * @kind member
 * @name y
 * @instance
 * @memberof Point
 * @type {number}
 */
/**
 * @kind member
 * @name z
 * @instance
 * @memberof Point
 * @type {number}
 */
namespace {

Napi::Value toString(const Napi::CallbackInfo &info) {
  return Napi::String::New(node_gdal::napi_env(), "Point");
}


Napi::Value xGetter(const Napi::CallbackInfo &info) {
  Point *geom = node_gdal::UnwrapWrapped<Point>(info.This().As<Napi::Object>());
  return Napi::Number::New(node_gdal::napi_env(), (geom->get())->getX());
}


void xSetter(const Napi::CallbackInfo &info, const Napi::Value &value) {
  Point *geom = node_gdal::UnwrapWrapped<Point>(info.This().As<Napi::Object>());

  if (!value.IsNumber()) {
    Napi::Error::New(node_gdal::napi_env(), "y must be a number").ThrowAsJavaScriptException();
    return;
  }
  double x = value.As<Napi::Number>().DoubleValue();

  ((OGRPoint *)geom->get())->setX(x);
}


Napi::Value yGetter(const Napi::CallbackInfo &info) {
  Point *geom = node_gdal::UnwrapWrapped<Point>(info.This().As<Napi::Object>());
  return Napi::Number::New(node_gdal::napi_env(), (geom->get())->getY());
}


void ySetter(const Napi::CallbackInfo &info, const Napi::Value &value) {
  Point *geom = node_gdal::UnwrapWrapped<Point>(info.This().As<Napi::Object>());

  if (!value.IsNumber()) {
    Napi::Error::New(node_gdal::napi_env(), "y must be a number").ThrowAsJavaScriptException();
    return;
  }
  double y = value.As<Napi::Number>().DoubleValue();

  ((OGRPoint *)geom->get())->setY(y);
}


Napi::Value zGetter(const Napi::CallbackInfo &info) {
  Point *geom = node_gdal::UnwrapWrapped<Point>(info.This().As<Napi::Object>());
  return Napi::Number::New(node_gdal::napi_env(), (geom->get())->getZ());
}


void zSetter(const Napi::CallbackInfo &info, const Napi::Value &value) {
  Point *geom = node_gdal::UnwrapWrapped<Point>(info.This().As<Napi::Object>());

  if (!value.IsNumber()) {
    Napi::Error::New(node_gdal::napi_env(), "z must be a number").ThrowAsJavaScriptException();
    return;
  }
  double z = value.As<Napi::Number>().DoubleValue();

  ((OGRPoint *)geom->get())->setZ(z);
}

} // namespace

void Point::Initialize(Napi::Object target) {
  Napi::Env env = target.Env();
  static constexpr MethodEntry methods[] = {
    method("toString", toString),
  };

  static constexpr AccessorEntry accessors[] = {
    attr("x", xGetter, xSetter),
    attr("y", yGetter, ySetter),
    attr("z", zGetter, zSetter),
  };

  Napi::Function lcons = GDALDefineClass<Point>(env, "Point", methods, accessors);

  // lcons->Inherit() has no DefineClass equivalent
  node_gdal::Inherit(lcons, Geometry::constructor().Value());

  // properties

  target.Set("Point", lcons);

  constructor() = Napi::Persistent(lcons);
}

} // namespace node_gdal
