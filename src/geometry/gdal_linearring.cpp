
#include "gdal_linearring.hpp"
#include "../utils/args.hpp"
#include "../collections/linestring_points.hpp"
#include "../gdal_common.hpp"
#include "gdal_geometry.hpp"
#include "gdal_linestring.hpp"

#include <stdlib.h>

namespace node_gdal {

Napi::FunctionReference &LinearRing::constructor() {
  return node_gdal::env_context().constructor<LinearRing>();
}

Napi::Value LinearRing::New(OGRLinearRing *geom, bool owned) {

  if (!geom) { return node_gdal::napi_env().Null(); }

  // make a copy of geometry owned by a feature
  // + no need to track when a feature is destroyed
  // + no need to throw errors when a method trys to modify an owned read-only
  // geometry
  // - is slower

  if (!owned) { geom = static_cast<OGRLinearRing *>(geom->clone()); }

  node_gdal::detail::PendingObjectGuard pending(geom);
  std::vector<napi_value> args;
  Napi::Object obj = LinearRing::constructor().Value().New(args);
  LinearRing *wrapped = node_gdal::UnwrapWrapped<LinearRing>(obj);

  return obj;
}

/**
 * Concrete representation of a closed ring.
 *
 * @constructor
 * @class LinearRing
 * @extends LineString
 */

/**
 * Computes the area enclosed by the ring.
 *
 * @method getArea
 * @instance
 * @memberof LinearRing
 * @return {number}
 */
namespace {
// LinearRing::getArea()
struct getAreaBody : node_gdal::SyncMethodBody {
  static Napi::Value run(const Napi::CallbackInfo &info) {
    auto obj = node_gdal::unwrapThis<LinearRing>(info, "LinearRing");
    if (!obj) { return obj.failure; }
    return Number::New(info.Env(), obj->get()->get_Area());
  }
};
} // namespace

GDAL_SYNC_METHOD(LinearRing::getArea, getAreaBody)

namespace {

Napi::Value toString(const Napi::CallbackInfo &info) {
  return Napi::String::New(node_gdal::napi_env(), "LinearRing");
}

} // namespace

void LinearRing::Initialize(Napi::Object target) {
  Napi::Env env = target.Env();
  static constexpr MethodEntry methods[] = {
    method("toString", toString),
    method("getArea", getArea),
  };

  Napi::Function lcons = GDALDefineClass<LinearRing>(env, "LinearRing", methods);

  // lcons->Inherit() has no DefineClass equivalent
  node_gdal::Inherit(lcons, LineString::constructor().Value());

  target.Set("LinearRing", lcons);

  constructor() = Napi::Persistent(lcons);
}

} // namespace node_gdal
