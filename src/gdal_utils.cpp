#include "gdal_utils.hpp"
#include "gdal_common.hpp"
#include "utils/args.hpp"
#include "gdal_dataset.hpp"
#include "gdal_spatial_reference.hpp"
#include "utils/string_list.hpp"

#if GDAL_VERSION_MAJOR > 2 || (GDAL_VERSION_MAJOR == 2 && GDAL_VERSION_MINOR >= 3)
#define GDALDatasetToHandle(x) GDALDataset::ToHandle(x)
#define GDALDatasetFromHandle(x) GDALDataset::FromHandle(x)
#else
#define GDALDatasetToHandle(x) static_cast<GDALDatasetH>(x)
#define GDALDatasetFromHandle(x) static_cast<GDALDataset *>(x)
#endif

namespace node_gdal {

void Utils::Initialize(Napi::Object target) {
  Napi::Env env = target.Env();

  GDAL_SetAsyncableMethod(env, target, "info", info);
  GDAL_SetAsyncableMethod(env, target, "translate", translate);
  GDAL_SetAsyncableMethod(env, target, "vectorTranslate", vectorTranslate);
  GDAL_SetAsyncableMethod(env, target, "warp", warp);
  GDAL_SetAsyncableMethod(env, target, "buildVRT", buildvrt);
  GDAL_SetAsyncableMethod(env, target, "rasterize", rasterize);
  GDAL_SetAsyncableMethod(env, target, "dem", dem);
}

/**
 * @typedef {object} UtilOptions
 * @property {ProgressCb} [progress_cb]
 * @property {AbortSignal} [abort_signal] AbortSignal to interrupt the operation
 */

/**
 * Library version of gdal_translate.
 *
 * @example
 * const ds = gdal.open('input.tif')
 * const out = gdal.translate('/vsimem/temp.tif', ds, [ '-b', '1' ])
 *
 * @throws {Error}
 * @method translate
 * @static
 * @param {string} destination destination filename
 * @param {Dataset} source source dataset
 * @param {string[]} [args] array of CLI options for gdal_translate
 * @param {UtilOptions} [options] additional options
 * @param {ProgressCb} [options.progress_cb]
 * @param {AbortSignal} [options.abort_signal] AbortSignal to interrupt the operation
 * @return {Dataset}
 */

/**
 * Library version of gdal_translate.
 * Remember that all library version tools return a live opened
 * dataset. If you need to use the file immediately following an
 * operation, you should flush/close it.
 * @async
 *
 * @example
 * const ds = gdal.open('input.tif')
 * const out = gdal.translate('/vsimem/temp.tif', ds, [ '-b', '1' ])
 * @throws {Error}
 *
 * @method translateAsync
 * @static
 * @param {string} destination destination filename
 * @param {Dataset} source source dataset
 * @param {string[]} [args] array of CLI options for gdal_translate
 * @param {UtilOptions} [options] additional options
 * @param {ProgressCb} [options.progress_cb]
 * @param {AbortSignal} [options.abort_signal] AbortSignal to interrupt the operation
 * @param {callback<Dataset>} [callback=undefined]
 * @return {Promise<Dataset>}
 */
namespace {

struct translateBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    Args args(info);
    auto aosOptions = std::make_shared<CPLStringList>();

    std::string dst;
    Napi::Object src, options;
    Napi::Array cli_args;
    Napi::FunctionReference *progress_cb = nullptr;
    Napi::Value abort_signal;
    if (!(args.get(0, "dst", dst) && args.get(1, "src", src) && args.optional(2, "args", cli_args) &&
          args.optional(3, "options", options) && args.fromCallback(options, "progress_cb", progress_cb) &&
          args.from(options, "abort_signal", abort_signal))) {
      return args.failed();
    }
    Dataset *ds = node_gdal::unwrap_or_throw<Dataset>(src, "Dataset");
    GDALDataset * raw = node_gdal::raw_or_throw<GDALDataset *>(ds, "GDALDataset");

    if (!cli_args.IsEmpty())
      for (unsigned i = 0; i < cli_args.Length(); ++i) {
        aosOptions->AddString(node_gdal::Stringify(cli_args.Get(i)).c_str());
      }

    GDALAsyncableJob<GDALDataset *> job(ds->uid);
    job.progress = progress_cb;
    // An AbortSignal alone is enough to make the operation interruptible: GDAL
    // can be interrupted only through the progress trampoline, which therefore
    // has to be installed even when there is no progress callback
    if (!job.setAbortSignal(abort_signal)) return info.Env().Undefined();
    const bool interruptible = job.interruptible();
    job.main = [raw, dst, aosOptions, interruptible](const GDALExecutionProgress &progress) {
      CPLErrorReset();
      auto b = aosOptions;
      auto psOptions = GDALTranslateOptionsNew(aosOptions->List(), nullptr);
      if (psOptions == nullptr) throw GDALError::fromGDAL();
      if (interruptible) GDALTranslateOptionsSetProgress(psOptions, ProgressTrampoline, (void *)&progress);
      GDALDataset *r = GDALDatasetFromHandle(GDALTranslate(dst.c_str(), GDALDatasetToHandle(raw), psOptions, nullptr));
      GDALTranslateOptionsFree(psOptions);
      if (r == nullptr) throw GDALError::fromGDAL();
      return r;
    };
    job.rval = [](GDALDataset *ds, const GetFromPersistentFunc &) { return Dataset::New(ds); };

    return job.run(info, node_gdal::is_async<Async>, 4);
  }
};

} // namespace

GDAL_ASYNC_METHOD(Utils::translate, translateBody)

/**
 * Library version of ogr2ogr.
 *
 * @example
 * const ds = gdal.open('input.geojson')
 * const out = gdal.vectorTranslate('/vsimem/temp.gpkg', ds, [ '-of', 'GPKG' ])
 *
 * @throws {Error}
 * @method vectorTranslate
 * @static
 * @param {string|Dataset} destination destination
 * @param {Dataset} source source dataset
 * @param {string[]} [args] array of CLI options for ogr2ogr
 * @param {UtilOptions} [options] additional options
 * @param {ProgressCb} [options.progress_cb]
 * @param {AbortSignal} [options.abort_signal] AbortSignal to interrupt the operation
 * @return {Dataset}
 */

/**
 * Library version of ogr2ogr.
 * @async
 *
 * @example
 * const ds = gdal.open('input.geojson')
 * const out = gdal.vectorTranslate('/vsimem/temp.gpkg', ds, [ '-of', 'GPKG' ])
 * @throws {Error}
 *
 * @method vectorTranslateAsync
 * @static
 * @param {string|Dataset} destination destination
 * @param {Dataset} source source dataset
 * @param {string[]} [args] array of CLI options for ogr2ogr
 * @param {UtilOptions} [options] additional options
 * @param {ProgressCb} [options.progress_cb]
 * @param {AbortSignal} [options.abort_signal] AbortSignal to interrupt the operation
 * @param {callback<Dataset>} [callback=undefined]
 * @return {Promise<Dataset>}
 */
namespace {

struct vectorTranslateBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    Args args(info);
    auto aosOptions = std::make_shared<CPLStringList>();

    std::string dst_filename("");
    Dataset *dst_ds = nullptr;
    if (info.Length() < 1) {
      Napi::Error::New(node_gdal::napi_env(), "\"dst\" must be given").ThrowAsJavaScriptException();
      return node_gdal::napi_env().Undefined();
    }

    GDALDataset *dst_raw = nullptr;
    if (info[0].IsString()) {
      if (!args.get(0, "dst", dst_filename)) return args.failed();
    } else if (info[0].IsObject()) {
      if (!args.getWrapped(0, "dst", "Dataset", dst_ds)) return args.failed();
      GDALDataset * _dst_raw = node_gdal::raw_or_throw<GDALDataset *>(dst_ds, "GDALDataset");
      dst_raw = _dst_raw;
    } else {
      Napi::Error::New(node_gdal::napi_env(), "\"dst\" must be an object or a gdal.Dataset").ThrowAsJavaScriptException();
      return node_gdal::napi_env().Undefined();
    }

    Napi::Object src;
    if (!args.get(1, "src", src)) return args.failed();
    Dataset *ds = node_gdal::unwrap_or_throw<Dataset>(src, "Dataset");
    GDALDataset * src_raw = node_gdal::raw_or_throw<GDALDataset *>(ds, "GDALDataset");

    Napi::Array cli_args;
    if (!args.optional(2, "args", cli_args)) return args.failed();
    if (!cli_args.IsEmpty())
      for (unsigned i = 0; i < cli_args.Length(); ++i) {
        aosOptions->AddString(node_gdal::Stringify(cli_args.Get(i)).c_str());
      }

    Napi::Object options;
    Napi::FunctionReference *progress_cb = nullptr;
    Napi::Value abort_signal;
    if (!args.optional(3, "options", options)) return args.failed();
    if (!args.fromCallback(options, "progress_cb", progress_cb)) return args.failed();
    if (!args.from(options, "abort_signal", abort_signal)) return args.failed();

    std::vector<long> uids = {ds->uid};
    if (dst_ds != nullptr) uids.push_back(dst_ds->uid);
    GDALAsyncableJob<GDALDataset *> job(uids);
    job.progress = progress_cb;
    // An AbortSignal alone is enough to make the operation interruptible: GDAL
    // can be interrupted only through the progress trampoline, which therefore
    // has to be installed even when there is no progress callback
    if (!job.setAbortSignal(abort_signal)) return info.Env().Undefined();
    const bool interruptible = job.interruptible();

    job.main = [src_raw, dst_filename, dst_raw, aosOptions, progress_cb, interruptible](const GDALExecutionProgress &progress) {
      CPLErrorReset();
      if (progress_cb) aosOptions->AddString("-progress");
      auto psOptions = GDALVectorTranslateOptionsNew(aosOptions->List(), nullptr);
      if (psOptions == nullptr) throw GDALError::fromGDAL();

      if (interruptible) GDALVectorTranslateOptionsSetProgress(psOptions, ProgressTrampoline, (void *)&progress);

      auto srcH = GDALDatasetToHandle(src_raw);
      GDALDataset *r = GDALDatasetFromHandle(
        GDALVectorTranslate(dst_filename.c_str(), GDALDatasetToHandle(dst_raw), 1, &srcH, psOptions, nullptr));

      GDALVectorTranslateOptionsFree(psOptions);
      if (r == nullptr) throw GDALError::fromGDAL();
      return r;
    };
    job.rval = [](GDALDataset *ds, const GetFromPersistentFunc &) { return Dataset::New(ds); };

    return job.run(info, node_gdal::is_async<Async>, 4);
  }
};

} // namespace

GDAL_ASYNC_METHOD(Utils::vectorTranslate, vectorTranslateBody)

/**
 * Library version of gdalinfo.
 *
 * @example
 * const output = gdal.info(gdal.open('input.tif'))
 * const output = gdal.info(gdal.open('/vsimem/temp.tif'), ['-json'])
 *
 * @throws {Error}
 * @method info
 * @static
 * @param {Dataset} dataset
 * @param {string[]} [args] array of CLI options for gdalinfo
 * @return {string}
 */

/**
 * Library version of gdalinfo.
 * @async
 *
 * @example
 * const ds = gdal.open('input.tif')
 * const output = gdal.info('/vsimem/temp.tif')
 * @throws {Error}
 *
 * @method infoAsync
 * @static
 * @param {Dataset} dataset
 * @param {string[]} [args] array of CLI options for gdalinfo
 * @param {callback<string>} [callback=undefined]
 * @return {Promise<string>}
 */
namespace {

struct infoBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    Args args(info);
    auto aosOptions = std::make_shared<CPLStringList>();

    Napi::Object src;
    if (!args.get(0, "src", src)) return args.failed();
    Dataset *ds = node_gdal::unwrap_or_throw<Dataset>(src, "Dataset");
    GDALDataset * raw = node_gdal::raw_or_throw<GDALDataset *>(ds, "GDALDataset");

    Napi::Array cli_args;
    if (!args.optional(1, "args", cli_args)) return args.failed();
    if (!cli_args.IsEmpty())
      for (unsigned i = 0; i < cli_args.Length(); ++i) {
        aosOptions->AddString(node_gdal::Stringify(cli_args.Get(i)).c_str());
      }

    GDALAsyncableJob<std::string> job(ds->uid);
    job.main = [raw, aosOptions](const GDALExecutionProgress &) {
      CPLErrorReset();
      auto psOptions = GDALInfoOptionsNew(aosOptions->List(), nullptr);
      if (psOptions == nullptr) throw GDALError::fromGDAL();
      char *r = GDALInfo(GDALDatasetToHandle(raw), psOptions);
      GDALInfoOptionsFree(psOptions);
      if (r == nullptr) throw GDALError::fromGDAL();
      std::string s(r);
      CPLFree(r);
      return s;
    };
    job.rval = [](std::string s, const GetFromPersistentFunc &) { return SafeString::New(s.c_str()); };

    return job.run(info, node_gdal::is_async<Async>, 2);
  }
};

} // namespace

GDAL_ASYNC_METHOD(Utils::info, infoBody)

/**
 * Library version of gdalwarp.
 *
 * @example
 * const ds = gdal.open('input.tif')
 * const output = gdal.warp('/vsimem/output.tiff', null, [ ds ], [ '-t_srs', 'epsg:3587' ])
 *
 * @throws {Error}
 * @method warp
 * @static
 * @param {string|null} dst_path destination path, null for an in-memory operation
 * @param {Dataset|null} dst_ds destination dataset, null for a new dataset
 * @param {Dataset[]} src_ds array of source datasets
 * @param {string[]} [args] array of CLI options for gdalwarp
 * @param {UtilOptions} [options] additional options
 * @param {ProgressCb} [options.progress_cb]
 * @param {AbortSignal} [options.abort_signal] AbortSignal to interrupt the operation
 * @return {Dataset}
 */

/**
 * Library version of gdalwarp.
 * @async
 *
 * @example
 * const ds = await gdal.openAsync('input.tif')
 * const output = await gdal.warpAsync('/vsimem/output.tiff', null, [ ds ], [ '-t_srs', 'epsg:3587' ])
 * @throws {Error}
 *
 * @method warpAsync
 * @static
 * @param {string|null} dst_path destination path, null for an in-memory operation
 * @param {Dataset|null} dst_ds destination dataset, null for a new dataset
 * @param {Dataset[]} src_ds array of source datasets
 * @param {string[]} [args] array of CLI options for gdalwarp
 * @param {UtilOptions} [options] additional options
 * @param {ProgressCb} [options.progress_cb]
 * @param {AbortSignal} [options.abort_signal] AbortSignal to interrupt the operation
 * @param {callback<Dataset>} [callback=undefined]
 * @return {Promise<Dataset>}
 */
namespace {

struct warpBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    Args args(info);
    auto aosOptions = std::make_shared<CPLStringList>();
    std::vector<long> uids;

    std::string dst_path("");
    Napi::Object dst_ds;

    if (!args.optional(0, "dst_path", dst_path)) return args.failed();

    if (!args.optional(1, "dst_ds", dst_ds)) return args.failed();
    GDALDatasetH gdal_dst_ds = nullptr;
    if (!dst_ds.IsEmpty()) {
      Dataset *ds = node_gdal::unwrap_or_throw<Dataset>(dst_ds, "Dataset");
      GDALDataset * raw = node_gdal::raw_or_throw<GDALDataset *>(ds, "GDALDataset");
      gdal_dst_ds = GDALDatasetToHandle(raw);
      uids.push_back(ds->uid);
    }

    if (dst_path.length() == 0 && gdal_dst_ds == nullptr) {
      Napi::Error::New(node_gdal::napi_env(), "Either \"dst_path\" or \"dst_ds\" must be given").ThrowAsJavaScriptException();
      return node_gdal::napi_env().Undefined();
    }

    Napi::Array src_ds;
    if (!args.get(2, "src_ds", src_ds)) return args.failed();
    if (src_ds.Length() < 1) {
      Napi::Error::New(node_gdal::napi_env(), "\"src_ds\" must contain at least one element").ThrowAsJavaScriptException();
      return node_gdal::napi_env().Undefined();
    }
    auto gdal_src_ds = std::shared_ptr<GDALDatasetH[]>(new GDALDatasetH[src_ds.Length()]);
    for (unsigned i = 0; i < src_ds.Length(); ++i) {
      Dataset *ds = node_gdal::unwrap_or_throw<Dataset>(src_ds.As<Napi::Object>().Get(i).As<Napi::Object>(), "Dataset");
      GDALDataset * raw = node_gdal::raw_or_throw<GDALDataset *>(ds, "GDALDataset");
      gdal_src_ds.get()[i] = GDALDatasetToHandle(raw);
      uids.push_back(ds->uid);
    }

    Napi::Array cli_args;
    if (!args.optional(3, "args", cli_args)) return args.failed();
    if (!cli_args.IsEmpty())
      for (unsigned i = 0; i < cli_args.Length(); ++i) {
        aosOptions->AddString(node_gdal::Stringify(cli_args.Get(i)).c_str());
      }

    Napi::Object options;
    Napi::FunctionReference *progress_cb = nullptr;
    Napi::Value abort_signal;
    if (!args.optional(4, "options", options)) return args.failed();
    if (!args.fromCallback(options, "progress_cb", progress_cb)) return args.failed();
    if (!args.from(options, "abort_signal", abort_signal)) return args.failed();

    GDALAsyncableJob<GDALDataset *> job(uids);
    int src_count = src_ds.Length();
    job.progress = progress_cb;
    // An AbortSignal alone is enough to make the operation interruptible: GDAL
    // can be interrupted only through the progress trampoline, which therefore
    // has to be installed even when there is no progress callback
    if (!job.setAbortSignal(abort_signal)) return info.Env().Undefined();
    const bool interruptible = job.interruptible();
    job.main =
      [dst_path, gdal_dst_ds, src_count, gdal_src_ds, aosOptions, interruptible](const GDALExecutionProgress &progress) {
        CPLErrorReset();
        auto psOptions = GDALWarpAppOptionsNew(aosOptions->List(), nullptr);
        if (psOptions == nullptr) throw GDALError::fromGDAL();
        if (interruptible) GDALWarpAppOptionsSetProgress(psOptions, ProgressTrampoline, (void *)&progress);
        GDALDatasetH r = GDALWarp(
          dst_path.length() > 0 ? dst_path.c_str() : nullptr,
          gdal_dst_ds,
          src_count,
          gdal_src_ds.get(),
          psOptions,
          nullptr);
        GDALWarpAppOptionsFree(psOptions);
        if (r == nullptr) throw GDALError::fromGDAL();
        return GDALDatasetFromHandle(r);
      };
    job.rval = [](GDALDataset *ds, const GetFromPersistentFunc &) { return Dataset::New(ds); };

    return job.run(info, node_gdal::is_async<Async>, 5);
  }
};

} // namespace

GDAL_ASYNC_METHOD(Utils::warp, warpBody)

/**
 * Library version of gdalbuildvrt.
 *
 * @example
 * const ds1 = gdal.buildVRT('/vsimem/target.tiff',
 *    [ 'input1.tif', 'input2.tif' ],
 *    [ '-resolution', 'highest' ] );
 *
 * const ds2 = gdal.buildVRT('/vsimem/target.tiff',
 *    [ gdal.open('input1.tif'), gdal.open('input2.tif') ],
 *    [ '-resolution', 'highest' ] );
 *
 * @throws {Error}
 * @method buildVRT
 * @static
 * @param {string|null} dst_path destination path, null for an in-memory operation
 * @param {Dataset[]|string[]} src_ds array of source datasets
 * @param {string[]} [args] array of CLI options for gdalbuildvrt
 * @param {UtilOptions} [options] additional options
 * @param {ProgressCb} [options.progress_cb]
 * @param {AbortSignal} [options.abort_signal] AbortSignal to interrupt the operation
 * @return {Dataset}
 */

/**
 * Library version of gdalbuildvrt.
 * @async
 *
 * @example
 * const ds1 = await gdal.buildVRTAsync('/vsimem/target.tiff',
 *    [ 'input1.tif', 'input2.tif' ],
 *    [ '-resolution', 'highest' ] );
 *
 * const ds2 = gdal.buildVRT('/vsimem/target.tiff',
 *    [ await gdal.openAsync('input1.tif'), await gdal.openAsync('input2.tif') ],
 *    [ '-resolution', 'highest' ] );
 *
 * @throws {Error}
 *
 * @method buildVRTAsync
 * @static
 * @param {string|null} dst_path destination path, null for an in-memory operation
 * @param {Dataset[]|string[]} src_ds array of source datasets
 * @param {string[]} [args] array of CLI options for gdalbuildvrt
 * @param {UtilOptions} [options] additional options
 * @param {ProgressCb} [options.progress_cb]
 * @param {AbortSignal} [options.abort_signal] AbortSignal to interrupt the operation
 * @param {callback<Dataset>} [callback=undefined]
 * @return {Promise<Dataset>}
 */
namespace {

struct buildvrtBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    Args args(info);
    auto aosOptions = std::make_shared<CPLStringList>();
    std::vector<long> uids;

    std::string dst_path("");

    if (!args.get(0, "dst_path", dst_path)) return args.failed();

    Napi::Array src_ds;
    if (!args.get(1, "src_ds", src_ds)) return args.failed();
    if (src_ds.Length() < 1) {
      Napi::Error::New(node_gdal::napi_env(), "\"src_ds\" must contain at least one element").ThrowAsJavaScriptException();
      return node_gdal::napi_env().Undefined();
    }

    std::shared_ptr<CPLStringList> aosSrcDs = nullptr;
    std::shared_ptr<GDALDatasetH[]> gdalSrcDs = nullptr;
    if (src_ds.As<Napi::Object>().Get(static_cast<uint32_t>(0)).IsString()) {
      aosSrcDs = std::make_shared<CPLStringList>();
      for (unsigned i = 0; i < src_ds.Length(); ++i) {
        if (!src_ds.As<Napi::Object>().Get(i).IsString()) {
          Napi::Error::New(node_gdal::napi_env(), "All \"src_ds\" elements must have the same type").ThrowAsJavaScriptException();
          return node_gdal::napi_env().Undefined();
        }
        aosSrcDs->AddString(node_gdal::Stringify(src_ds.As<Napi::Object>().Get(i)).c_str());
      }
      uids.push_back(0);
    } else {
      gdalSrcDs = std::shared_ptr<GDALDatasetH[]>(new GDALDatasetH[src_ds.Length()]);
      for (unsigned i = 0; i < src_ds.Length(); ++i) {
        Napi::Value v = src_ds.As<Napi::Object>().Get(i);
        Dataset *ds = node_gdal::unwrap_or_throw<Dataset>(v, "Dataset");
        GDALDataset * raw = node_gdal::raw_or_throw<GDALDataset *>(ds, "GDALDataset");
        gdalSrcDs.get()[i] = GDALDatasetToHandle(raw);
        uids.push_back(ds->uid);
      }
    }

    Napi::Array cli_args;
    if (!args.optional(2, "args", cli_args)) return args.failed();
    if (!cli_args.IsEmpty())
      for (unsigned i = 0; i < cli_args.Length(); ++i) {
        aosOptions->AddString(node_gdal::Stringify(cli_args.Get(i)).c_str());
      }

    Napi::Object options;
    Napi::FunctionReference *progress_cb = nullptr;
    Napi::Value abort_signal;
    if (!args.optional(3, "options", options)) return args.failed();
    if (!args.fromCallback(options, "progress_cb", progress_cb)) return args.failed();
    if (!args.from(options, "abort_signal", abort_signal)) return args.failed();

    GDALAsyncableJob<GDALDataset *> job(uids);
    int src_count = src_ds.Length();
    job.progress = progress_cb;
    // An AbortSignal alone is enough to make the operation interruptible: GDAL
    // can be interrupted only through the progress trampoline, which therefore
    // has to be installed even when there is no progress callback
    if (!job.setAbortSignal(abort_signal)) return info.Env().Undefined();
    const bool interruptible = job.interruptible();
    job.main =
      [dst_path, src_count, gdalSrcDs, aosSrcDs, aosOptions, interruptible](const GDALExecutionProgress &progress) {
        CPLErrorReset();
        auto psOptions = GDALBuildVRTOptionsNew(aosOptions->List(), nullptr);
        if (psOptions == nullptr) throw GDALError::fromGDAL();
        if (interruptible) GDALBuildVRTOptionsSetProgress(psOptions, ProgressTrampoline, (void *)&progress);

        GDALDatasetH r = GDALBuildVRT(
          dst_path.c_str(),
          src_count,
          gdalSrcDs.get(),
          aosSrcDs.get() != nullptr ? aosSrcDs->List() : nullptr,
          psOptions,
          nullptr);

        GDALBuildVRTOptionsFree(psOptions);
        if (r == nullptr) throw GDALError::fromGDAL();
        return GDALDatasetFromHandle(r);
      };
    job.rval = [](GDALDataset *ds, const GetFromPersistentFunc &) { return Dataset::New(ds); };

    return job.run(info, node_gdal::is_async<Async>, 4);
  }
};

} // namespace

GDAL_ASYNC_METHOD(Utils::buildvrt, buildvrtBody)

/**
 * Library version of gdal_rasterize.
 *
 * @example
 * const ds1 = gdal.rasterize('/vsimem/target.tiff',
 *    src_ds,
 *    [ '-b', '1' ] );
 *
 * const ds2 = gdal.rasterize(dst_ds,
 *    src_ds,
 *    [ '-b', '1' ] );
 *
 * @throws {Error}
 * @method rasterize
 * @static
 * @param {string|Dataset} destination
 * @param {Dataset} source dataset
 * @param {string[]} [args] array of CLI options for gdal_rasterize
 * @param {UtilOptions} [options] additional options
 * @param {ProgressCb} [options.progress_cb]
 * @param {AbortSignal} [options.abort_signal] AbortSignal to interrupt the operation
 * @return {Dataset}
 */

/**
 * Library version of gdal_rasterize.
 * @async
 *
 * @example
 * const ds1 = await gdal.rasterizeAsync('/vsimem/target.tiff',
 *    src_ds,
 *    [ '-b', '1' ] );
 *
 * const ds2 = await gdal.rasterizeAsync(dst_ds,
 *    src_ds,
 *    [ '-b', '1' ] );
 *
 * @throws {Error}
 * @method rasterizeAsync
 * @static
 * @param {string|Dataset} destination
 * @param {Dataset} source dataset
 * @param {string[]} [args] array of CLI options for gdal_rasterize
 * @param {UtilOptions} [options] additional options
 * @param {ProgressCb} [options.progress_cb]
 * @param {AbortSignal} [options.abort_signal] AbortSignal to interrupt the operation
 * @param {callback<Dataset>} [callback=undefined]
 * @return {Promise<Dataset>}
 */
namespace {

struct rasterizeBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    Args args(info);
    auto aosOptions = std::make_shared<CPLStringList>();

    std::string dst_path("");
    Napi::Object dst_ds;

    GDALDataset *dst_raw = nullptr;
    if (info.Length() > 1 && info[0].IsString()) {
      if (!args.get(0, "dst", dst_path)) return args.failed();
    } else if (info.Length() > 1 && info[0].IsObject()) {
      if (!args.get(0, "dst", dst_ds)) return args.failed();
      Dataset *dst_obj = node_gdal::unwrap_or_throw<Dataset>(dst_ds, "Dataset");
      GDALDataset * _dst_raw = node_gdal::raw_or_throw<GDALDataset *>(dst_obj, "GDALDataset");
      dst_raw = _dst_raw;
    } else {
      Napi::Error::New(node_gdal::napi_env(), "dst must be given").ThrowAsJavaScriptException();
      return node_gdal::napi_env().Undefined();
    }

    Napi::Object src;
    if (!args.get(1, "src", src)) return args.failed();
    Dataset *ds = node_gdal::unwrap_or_throw<Dataset>(src, "Dataset");
    GDALDataset * src_raw = node_gdal::raw_or_throw<GDALDataset *>(ds, "GDALDataset");

    Napi::Array cli_args;
    if (!args.optional(2, "args", cli_args)) return args.failed();
    if (!cli_args.IsEmpty())
      for (unsigned i = 0; i < cli_args.Length(); ++i) {
        aosOptions->AddString(node_gdal::Stringify(cli_args.Get(i)).c_str());
      }

    Napi::Object options;
    Napi::FunctionReference *progress_cb = nullptr;
    Napi::Value abort_signal;
    if (!args.optional(3, "options", options)) return args.failed();
    if (!args.fromCallback(options, "progress_cb", progress_cb)) return args.failed();
    if (!args.from(options, "abort_signal", abort_signal)) return args.failed();

    GDALAsyncableJob<GDALDataset *> job(ds->uid);
    job.progress = progress_cb;
    // An AbortSignal alone is enough to make the operation interruptible: GDAL
    // can be interrupted only through the progress trampoline, which therefore
    // has to be installed even when there is no progress callback
    if (!job.setAbortSignal(abort_signal)) return info.Env().Undefined();
    const bool interruptible = job.interruptible();
    job.main = [dst_path, dst_raw, src_raw, aosOptions, interruptible](const GDALExecutionProgress &progress) {
      CPLErrorReset();
      auto psOptions = GDALRasterizeOptionsNew(aosOptions->List(), nullptr);
      if (psOptions == nullptr) throw GDALError::fromGDAL();
      if (interruptible) GDALRasterizeOptionsSetProgress(psOptions, ProgressTrampoline, (void *)&progress);

      GDALDatasetH r = GDALRasterize(
        dst_path.length() > 0 ? dst_path.c_str() : nullptr,
        dst_raw != nullptr ? dst_raw : nullptr,
        src_raw,
        psOptions,
        nullptr);

      GDALRasterizeOptionsFree(psOptions);
      if (r == nullptr) throw GDALError::fromGDAL();
      return GDALDatasetFromHandle(r);
    };
    job.rval = [](GDALDataset *ds, const GetFromPersistentFunc &) { return Dataset::New(ds); };

    return job.run(info, node_gdal::is_async<Async>, 4);
  }
};

} // namespace

GDAL_ASYNC_METHOD(Utils::rasterize, rasterizeBody)

/**
 * Library version of gdaldem.
 *
 * @example
 * const ds = gdal.open('input.tif')
 * const output = gdal.dem('/vsimem/output.tiff', ds, 'hillshade',  [ '-z', '2' ])
 *
 * @throws {Error}
 * @method dem
 * @static
 * @param {string} dst_path destination path
 * @param {Dataset} src_ds source dataset
 * @param {'hillshade'|'slope'|'aspect'|'color-relief'|'TRI'|'TPI'|'Roughness'} mode processing mode
 * @param {string[]} [args] array of CLI options for gdaldem
 * @param {string} [colorFile] optional color filename, see https://gdal.org/programs/gdaldem.html for more details
 * @param {UtilOptions} [options] additional options
 * @param {ProgressCb} [options.progress_cb]
 * @param {AbortSignal} [options.abort_signal] AbortSignal to interrupt the operation
 * @return {Dataset}
 */

/**
 * Library version of gdaldem.
 * @async
 *
 * @example
 * const ds = await gdal.openAsync('input.tif')
 * const output = await gdal.demAsync('/vsimem/output.tiff', ds, 'hillshade', [ '-z', '2' ])
 *
 * @throws {Error}
 * @method demAsync
 * @static
 * @param {string} dst_path destination path
 * @param {Dataset} src_ds source dataset
 * @param {'hillshade'|'slope'|'aspect'|'color-relief'|'TRI'|'TPI'|'Roughness'} mode processing mode
 * @param {string[]} [args] array of CLI options for gdaldem
 * @param {string} [colorFile] optional color filename, see https://gdal.org/programs/gdaldem.html for more details
 * @param {UtilOptions} [options] additional options
 * @param {ProgressCb} [options.progress_cb]
 * @param {AbortSignal} [options.abort_signal] AbortSignal to interrupt the operation
 * @param {callback<Dataset>} [callback=undefined]
 * @return {Promise<Dataset>}
 */
namespace {

struct demBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    Args args(info);
    auto aosOptions = std::make_shared<CPLStringList>();

    std::string dst_path;
    if (!args.get(0, "dst_path", dst_path)) return args.failed();

    Napi::Object src;
    if (!args.get(1, "src", src)) return args.failed();
    Dataset *ds = node_gdal::unwrap_or_throw<Dataset>(src, "Dataset");
    GDALDataset * raw = node_gdal::raw_or_throw<GDALDataset *>(ds, "GDALDataset");

    std::string mode;
    if (!args.get(2, "mode", mode)) return args.failed();

    Napi::Array cli_args;
    if (!args.optional(3, "args", cli_args)) return args.failed();
    if (!cli_args.IsEmpty()) {
      for (unsigned i = 0; i < cli_args.Length(); ++i) {
        aosOptions->AddString(node_gdal::Stringify(cli_args.Get(i)).c_str());
      }
    }

    std::string colorFilename = "";
    if (!args.optional(4, "colorFilename", colorFilename)) return args.failed();

    Napi::Object options;
    Napi::FunctionReference *progress_cb = nullptr;
    Napi::Value abort_signal;
    if (!args.optional(5, "options", options)) return args.failed();
    if (!args.fromCallback(options, "progress_cb", progress_cb)) return args.failed();
    if (!args.from(options, "abort_signal", abort_signal)) return args.failed();

    GDALAsyncableJob<GDALDataset *> job(ds->uid);
    job.progress = progress_cb;
    // An AbortSignal alone is enough to make the operation interruptible: GDAL
    // can be interrupted only through the progress trampoline, which therefore
    // has to be installed even when there is no progress callback
    if (!job.setAbortSignal(abort_signal)) return info.Env().Undefined();
    const bool interruptible = job.interruptible();
    job.main = [dst_path, mode, raw, colorFilename, aosOptions, interruptible](const GDALExecutionProgress &progress) {
      CPLErrorReset();
      auto psOptions = GDALDEMProcessingOptionsNew(aosOptions->List(), nullptr);
      if (psOptions == nullptr) throw GDALError::fromGDAL();
      if (interruptible) GDALDEMProcessingOptionsSetProgress(psOptions, ProgressTrampoline, (void *)&progress);
      GDALDataset *r = GDALDatasetFromHandle(GDALDEMProcessing(
        dst_path.c_str(),
        GDALDatasetToHandle(raw),
        mode.c_str(),
        colorFilename.size() > 0 ? colorFilename.c_str() : nullptr,
        psOptions,
        nullptr));

      GDALDEMProcessingOptionsFree(psOptions);
      if (r == nullptr) throw GDALError::fromGDAL();
      return GDALDatasetFromHandle(r);
    };
    job.rval = [](GDALDataset *ds, const GetFromPersistentFunc &) { return Dataset::New(ds); };

    return job.run(info, node_gdal::is_async<Async>, 6);
  }
};

} // namespace

GDAL_ASYNC_METHOD(Utils::dem, demBody)

} // namespace node_gdal
