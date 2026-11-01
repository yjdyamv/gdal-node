#include <memory>
#include <vector>

#include "gdal_transformer.hpp"
#include "gdal_common.hpp"
#include "gdal_dataset.hpp"
#include "gdal_spatial_reference.hpp"
#include "utils/args.hpp"

namespace node_gdal {

Napi::FunctionReference &Transformer::constructor() {
  return node_gdal::env_context().constructor<Transformer>();
}

/**
 * @typedef {object} TransformerPoint
 * @property {number} x
 * @property {number} y
 * @property {number} [z]
 */

/**
 * A coordinate transformer.
 *
 * A `Transformer` is created either from a source and a target
 * {@link SpatialReference}, or from a {@link Dataset}. In the second form the
 * geotransform of the dataset and its SRS are used, which is what
 * `gdal.warp()` does.
 *
 * @example
 * const transformer = new gdal.Transformer(srcSrs, dstSrs)
 * const pt = transformer.transformPoint(false, { x: 0, y: 0 })
 *
 * @example
 * const transformer = new gdal.Transformer(ds)
 * const pt = transformer.transformPoint(false, { x: 0, y: 0 })
 *
 * @class Transformer
 * @param {SpatialReference|Dataset} src
 * @param {SpatialReference} [dst]
 * @param {object} [options] creation options
 */
Transformer::Transformer(const Napi::CallbackInfo &info) : GDALObject<Transformer>(info), this_(nullptr) {
  Napi::Env env = info.Env();
  Args args(info);

  if (info.Length() < 1) {
    Napi::Error::New(env, "src must be given").ThrowAsJavaScriptException();
    return;
  }

  // new Transformer(dataset)
  if (node_gdal::IsInstanceOf<Dataset>(info[0])) {
    Dataset *ds = node_gdal::unwrap_or_throw<Dataset>(info[0].As<Napi::Object>(), "Dataset");
    if (!ds) { return; }
    GDALDataset *raw = node_gdal::raw_or_throw<GDALDataset *>(ds, "GDALDataset");
    GDAL_LOCK_PARENT_VOID(ds);
    CPLErrorReset();
    this_ = GDALCreateGenImgProjTransformer2(raw, nullptr, nullptr);
    if (this_ == nullptr) {
      Napi::Error::New(env, CPLGetLastErrorMsg()).ThrowAsJavaScriptException();
      return;
    }
    return;
  }

  // new Transformer(srcSrs, dstSrs, [options])
  if (!node_gdal::IsInstanceOf<SpatialReference>(info[0])) {
    Napi::TypeError::New(env, "src must be a SpatialReference or a Dataset").ThrowAsJavaScriptException();
    return;
  }
  SpatialReference *src = node_gdal::unwrap_or_throw<SpatialReference>(info[0].As<Napi::Object>(), "SpatialReference");
  if (!src) { return; }
  OGRSpatialReference *src_srs = node_gdal::raw_or_throw<OGRSpatialReference *>(src, "OGRSpatialReference");

  if (info.Length() < 2 || !node_gdal::IsInstanceOf<SpatialReference>(info[1])) {
    Napi::TypeError::New(env, "dst must be a SpatialReference").ThrowAsJavaScriptException();
    return;
  }
  SpatialReference *dst = node_gdal::unwrap_or_throw<SpatialReference>(info[1].As<Napi::Object>(), "SpatialReference");
  if (!dst) { return; }
  OGRSpatialReference *dst_srs = node_gdal::raw_or_throw<OGRSpatialReference *>(dst, "OGRSpatialReference");

  CPLErrorReset();
  this_ = GDALCreateReprojectionTransformerEx(
    OGRSpatialReference::ToHandle(src_srs), OGRSpatialReference::ToHandle(dst_srs), nullptr);
  if (this_ == nullptr) {
    Napi::Error::New(env, CPLGetLastErrorMsg()).ThrowAsJavaScriptException();
    return;
  }
}

Transformer::~Transformer() {
  dispose();
}

void Transformer::dispose() {
  if (this_) {
    LOG("Disposing Transformer [%p]", this_);
    GDALDestroyTransformer(this_);
    this_ = nullptr;
  }
}

Napi::Value toString(const Napi::CallbackInfo &info) {
  return Napi::String::New(node_gdal::napi_env(), "Transformer");
}

/**
 * Transforms a single point.
 *
 * @example
 * const pt = transformer.transformPoint(false, { x: 0, y: 0 })
 *
 * @method transformPoint
 * @instance
 * @memberof Transformer
 * @throws {Error}
 * @param {boolean} dstToSrc `true` to transform from the destination to the
 * source, `false` for the other direction
 * @param {TransformerPoint} point
 * @return {TransformerPoint} The transformed point
 */
Napi::Value transformPoint(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  Transformer *transformer = node_gdal::unwrap_or_throw<Transformer>(info.This(), "Transformer");
  if (!transformer) { return env.Undefined(); }
  void *raw = transformer->get();

  Args args(info);
  bool dst_to_src = false;
  Napi::Object point;
  if (!args.get(0, "dstToSrc", dst_to_src)) { return args.failed(); }
  if (!args.get(1, "point", point)) { return args.failed(); }
  if (!point.Has("x")) {
    Napi::TypeError::New(env, "x must be given").ThrowAsJavaScriptException();
    return env.Undefined();
  }
  if (!point.Has("y")) {
    Napi::TypeError::New(env, "y must be given").ThrowAsJavaScriptException();
    return env.Undefined();
  }
  double x = 0, y = 0, z = 0;
  if (!args.from(point, "x", x)) { return args.failed(); }
  if (!args.from(point, "y", y)) { return args.failed(); }
  if (!args.from(point, "z", z)) { return args.failed(); }

  int success = 0;
  CPLErrorReset();
  if (!GDALUseTransformer(raw, dst_to_src ? TRUE : FALSE, 1, &x, &y, &z, &success)) {
    Napi::Error::New(env, CPLGetLastErrorMsg()).ThrowAsJavaScriptException();
    return env.Undefined();
  }
  if (!success) {
    Napi::Error::New(env, "Point is not transformable").ThrowAsJavaScriptException();
    return env.Undefined();
  }

  Napi::Object result = Napi::Object::New(env);
  result.Set("x", Napi::Number::New(env, x));
  result.Set("y", Napi::Number::New(env, y));
  result.Set("z", Napi::Number::New(env, z));
  return result;
}

/**
 * Transforms an array of points.
 *
 * @example
 * const pts = transformer.transformPoints(false, [ { x: 0, y: 0 }, { x: 1, y: 1 } ])
 *
 * @method transformPoints
 * @instance
 * @memberof Transformer
 * @throws {Error}
 * @param {boolean} dstToSrc
 * @param {TransformerPoint[]} points
 * @return {TransformerPoint[]} The transformed points
 */
Napi::Value transformPoints(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  Transformer *transformer = node_gdal::unwrap_or_throw<Transformer>(info.This(), "Transformer");
  if (!transformer) { return env.Undefined(); }
  void *raw = transformer->get();

  Args args(info);
  bool dst_to_src = false;
  Napi::Array points;
  if (!args.get(0, "dstToSrc", dst_to_src)) { return args.failed(); }
  if (!args.get(1, "points", points)) { return args.failed(); }

  size_t count = points.Length();
  std::vector<double> xs(count), ys(count), zs(count);
  std::vector<int> success(count);
  for (size_t i = 0; i < count; i++) {
    Napi::Value value = points.Get(i);
    if (!value.IsObject()) {
      Napi::TypeError::New(env, "points must contain only objects").ThrowAsJavaScriptException();
      return env.Undefined();
    }
    Napi::Object point = value.As<Napi::Object>();
    if (!point.Has("x")) {
      Napi::TypeError::New(env, "x must be given").ThrowAsJavaScriptException();
      return env.Undefined();
    }
    if (!point.Has("y")) {
      Napi::TypeError::New(env, "y must be given").ThrowAsJavaScriptException();
      return env.Undefined();
    }
    xs[i] = 0;
    ys[i] = 0;
    if (!args.from(point, "x", xs[i])) { return args.failed(); }
    if (!args.from(point, "y", ys[i])) { return args.failed(); }
    zs[i] = 0;
    if (!args.from(point, "z", zs[i])) { return args.failed(); }
  }

  CPLErrorReset();
  if (!GDALUseTransformer(
        raw, dst_to_src ? TRUE : FALSE, (int)count, xs.data(), ys.data(), zs.data(), success.data())) {
    Napi::Error::New(env, CPLGetLastErrorMsg()).ThrowAsJavaScriptException();
    return env.Undefined();
  }

  Napi::Array result = Napi::Array::New(env, count);
  for (size_t i = 0; i < count; i++) {
    if (!success[i]) {
      result.Set(i, env.Null());
      continue;
    }
    Napi::Object point = Napi::Object::New(env);
    point.Set("x", Napi::Number::New(env, xs[i]));
    point.Set("y", Napi::Number::New(env, ys[i]));
    point.Set("z", Napi::Number::New(env, zs[i]));
    result.Set(i, point);
  }
  return result;
}

void Transformer::Initialize(Napi::Object target) {
  Napi::Env env = target.Env();
  static constexpr MethodEntry methods[] = {
    method("toString", toString),
    method("transformPoint", transformPoint),
    method("transformPoints", transformPoints),
  };

  Napi::Function lcons = GDALDefineClass<Transformer>(env, "Transformer", methods);

  target.Set("Transformer", lcons);

  constructor() = Napi::Persistent(lcons);
}

} // namespace node_gdal
