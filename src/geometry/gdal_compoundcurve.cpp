
#include "gdal_compoundcurve.hpp"
#include "../collections/linestring_points.hpp"
#include "../gdal_common.hpp"
#include "gdal_geometry.hpp"
#include "gdal_point.hpp"

#include <stdlib.h>

namespace node_gdal {

Napi::FunctionReference &CompoundCurve::constructor() {
  return node_gdal::env_context().constructor<CompoundCurve>();
}

void CompoundCurve::SetPrivate(Napi::Object _this, Napi::Value value) {
  node_gdal::set_private(_this, "curves_", value);
};

/**
 * Concrete representation of a compound contionuos curve.
 *
 * @example
 *
 * var CompoundCurve = new gdal.CompoundCurve();
 * CompoundCurve.points.add(new gdal.Point(0,0));
 * CompoundCurve.points.add(new gdal.Point(0,10));
 *
 * @constructor
 * @class CompoundCurve
 * @extends Geometry
 */

/**
 * Points that make up the compound curve.
 *
 * @kind member
 * @name curves
 * @instance
 * @memberof CompoundCurve
 * @type {CompoundCurveCurves}
 */
namespace {

Napi::Value toString(const Napi::CallbackInfo &info) {
  return Napi::String::New(node_gdal::napi_env(), "CompoundCurve");
}


Napi::Value curvesGetter(const Napi::CallbackInfo &info) {
  node_gdal::requireAlive<CompoundCurve>(info.This(), "CompoundCurve");
  return node_gdal::get_private(info.This(), "curves_");
}

} // namespace

void CompoundCurve::Initialize(Napi::Object target) {
  Napi::Env env = target.Env();
  static constexpr MethodEntry methods[] = {
    method("toString", toString),
  };

  static constexpr AccessorEntry accessors[] = {
    attr("curves", curvesGetter, READ_ONLY_SETTER),
  };

  Napi::Function lcons = GDALDefineClass<CompoundCurve>(env, "CompoundCurve", methods, accessors);

  // lcons->Inherit() has no DefineClass equivalent
  node_gdal::Inherit(lcons, Geometry::constructor().Value());

  target.Set("CompoundCurve", lcons);

  constructor() = Napi::Persistent(lcons);
}

} // namespace node_gdal
