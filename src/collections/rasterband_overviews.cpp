#include "rasterband_overviews.hpp"
#include "../utils/args.hpp"
#include "../gdal_common.hpp"
#include "../gdal_rasterband.hpp"

namespace node_gdal {

Napi::FunctionReference &RasterBandOverviews::constructor() {
  return node_gdal::env_context().constructor<RasterBandOverviews>();
}

RasterBandOverviews::RasterBandOverviews(const Napi::CallbackInfo &info) : GDALObject<RasterBandOverviews>(info) {
  if (info.Length() < 1 || !info[0].IsObject()) {
    Napi::Error::New(info.Env(), "Cannot create RasterBandOverviews directly").ThrowAsJavaScriptException();
    return;
  }
  node_gdal::set_private(info.This(), "parent_", info[0]);
}

RasterBandOverviews::~RasterBandOverviews() {
}

/**
 * An encapsulation of a {@link RasterBand} overview functionality.
 *
 * @class RasterBandOverviews
 */

Napi::Value RasterBandOverviews::New(Napi::Value band_obj) {

  std::vector<napi_value> args = {band_obj};
  Napi::Object obj = RasterBandOverviews::constructor().Value().New(args);

  return obj;
}

/**
 * Fetches the overview at the provided index.
 *
 * @method get
 * @instance
 * @memberof RasterBandOverviews
 * @throws {Error}
 * @param {number} index 0-based index
 * @return {RasterBand}
 */

/**
 * Fetches the overview at the provided index.
 * @async
 *
 * @method getAsync
 * @instance
 * @memberof RasterBandOverviews
 * @throws {Error}
 * @param {number} index 0-based index
 * @param {callback<RasterBand>} [callback=undefined]
 * @return {Promise<RasterBand>}
 */
namespace {

struct getBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    Args args(info);

    Napi::Object parent =
      node_gdal::get_private(info.This(), "parent_").As<Napi::Object>();

    RasterBand *band = node_gdal::unwrap_or_throw<RasterBand>(parent, "RasterBand");

    int id;
    if (!args.get(0, "id", id)) return args.failed();

    GDALAsyncableJob<GDALRasterBand *> job(band->parent_uid);
    job.persist(parent);
    job.main = [band, id](const GDALExecutionProgress &) {
      CPLErrorReset();
      GDALRasterBand *result = band->get()->GetOverview(id);
      if (result == nullptr) { throw GDALError("Specified overview not found"); }
      return result;
    };
    job.rval = [band](GDALRasterBand *result, const GetFromPersistentFunc &) {
      return RasterBand::New(result, band->getParent());
    };
    return job.run(info, node_gdal::is_async<Async>, 1);
  }
};

} // namespace

GDAL_ASYNC_METHOD(RasterBandOverviews::get, getBody)

/**
 * Fetch best sampling overview.
 *
 * Returns the most reduced overview of the given band that still satisfies the
 * desired number of samples. This function can be used with zero as the number
 * of desired samples to fetch the most reduced overview. The same band as was
 * passed in will be returned if it has not overviews, or if none of the
 * overviews have enough samples.
 *
 * @method getBySampleCount
 * @instance
 * @memberof RasterBandOverviews
 * @param {number} samples
 * @return {RasterBand}
 */

/**
 * Fetch best sampling overview.
 * @async
 *
 * Returns the most reduced overview of the given band that still satisfies the
 * desired number of samples. This function can be used with zero as the number
 * of desired samples to fetch the most reduced overview. The same band as was
 * passed in will be returned if it has not overviews, or if none of the
 * overviews have enough samples.
 *
 * @method getBySampleCountAsync
 * @instance
 * @memberof RasterBandOverviews
 * @param {number} samples
 * @param {callback<RasterBand>} [callback=undefined]
 * @return {Promise<RasterBand>}
 */
namespace {

struct getBySampleCountBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    Args args(info);

    Napi::Object parent =
      node_gdal::get_private(info.This(), "parent_").As<Napi::Object>();
    RasterBand *band = node_gdal::unwrap_or_throw<RasterBand>(parent, "RasterBand");

    int n_samples;
    if (!args.get(0, "minimum number of samples", n_samples)) return args.failed();

    GDALAsyncableJob<GDALRasterBand *> job(band->parent_uid);
    job.persist(parent);
    job.main = [band, n_samples](const GDALExecutionProgress &) {
      CPLErrorReset();
      GDALRasterBand *result = band->get()->GetRasterSampleOverview(n_samples);
      if (result == nullptr) { throw GDALError("Specified overview not found"); }
      return result;
    };
    job.rval = [band](GDALRasterBand *result, const GetFromPersistentFunc &) {
      return RasterBand::New(result, band->getParent());
    };
    return job.run(info, node_gdal::is_async<Async>, 1);
  }
};

} // namespace

GDAL_ASYNC_METHOD(RasterBandOverviews::getBySampleCount, getBySampleCountBody)

/**
 * Returns the number of overviews.
 *
 * @method count
 * @instance
 * @memberof RasterBandOverviews
 * @return {number}
 */

/**
 * Returns the number of overviews.
 * @async
 *
 * @method countAsync
 * @instance
 * @memberof RasterBandOverviews
 * @param {callback<number>} [callback=undefined]
 * @return {Promise<number>}
 */
namespace {

struct countBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {

    Napi::Object parent =
      node_gdal::get_private(info.This(), "parent_").As<Napi::Object>();
    RasterBand *band = node_gdal::unwrap_or_throw<RasterBand>(parent, "RasterBand");

    GDALAsyncableJob<int> job(band->parent_uid);
    job.persist(parent);
    job.main = [band](const GDALExecutionProgress &) {
      int count = band->get()->GetOverviewCount();
      return count;
    };
    job.rval = [](int count, const GetFromPersistentFunc &) { return Napi::Number::New(node_gdal::napi_env(), count); };
    return job.run(info, node_gdal::is_async<Async>, 0);
  }
};

} // namespace

GDAL_ASYNC_METHOD(RasterBandOverviews::count, countBody)

namespace {

Napi::Value toString(const Napi::CallbackInfo &info) {
  return Napi::String::New(node_gdal::napi_env(), "RasterBandOverviews");
}

} // namespace

void RasterBandOverviews::Initialize(Napi::Object target) {
  Napi::Env env = target.Env();
  static constexpr MethodEntry methods[] = {
    method("toString", toString),
    asyncMethod("count", count, countAsync),
    asyncMethod("get", get, getAsync),
    asyncMethod("getBySampleCount", getBySampleCount, getBySampleCountAsync),
  };

  Napi::Function lcons = GDALDefineClass<RasterBandOverviews>(env, "RasterBandOverviews", methods);

  target.Set("RasterBandOverviews", lcons);

  constructor() = Napi::Persistent(lcons);
}

} // namespace node_gdal
