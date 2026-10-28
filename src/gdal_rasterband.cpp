
#include "gdal_common.hpp"
#include "utils/args.hpp"

#include "collections/rasterband_overviews.hpp"
#include "collections/rasterband_pixels.hpp"
#include "collections/colortable.hpp"
#include "gdal_dataset.hpp"
#include "gdal_mdarray.hpp"
#include "gdal_majorobject.hpp"
#include "gdal_rasterband.hpp"
#include "utils/string_list.hpp"

#include <cpl_port.h>
#include <limits>

namespace node_gdal {

Napi::FunctionReference &RasterBand::constructor() {
  return node_gdal::env_context().constructor<RasterBand>();
}

RasterBand::RasterBand(const Napi::CallbackInfo &info) : GDALObject<RasterBand>(info), uid(0), this_(nullptr), parent_ds(0) {
  GDALRasterBand *pending = node_gdal::TakePendingObject<GDALRasterBand>();
  if (pending != nullptr) {
    this_ = pending;
    LOG("Created band [%p] (dataset = %p)", this_, this_->GetDataset());
  } else {
    Napi::Error::New(info.Env(), "Cannot create RasterBand directly").ThrowAsJavaScriptException();
    return;
  }

  node_gdal::set_private(info.This(), "overviews_", RasterBandOverviews::New(info.This()));
  node_gdal::set_private(info.This(), "pixels_", RasterBandPixels::New(info.This()));
}

RasterBand::~RasterBand() {
  dispose();
}

void RasterBand::dispose() {
  if (this_) {
    LOG("Disposing band [%p]", this_);

    object_store().dispose(uid);

    LOG("Disposed band [%p]", this_);

    // The band itself is not deleted here: a band is owned by its Dataset, and
    // the ones the addon owns (the algebra operators) are deleted by the object
    // store (see ObjectStore::dispose()), while the source Dataset is still
    // alive, which is required by their destructor.
    this_ = nullptr;
  }
}

/**
 * A single raster band (or channel).
 *
 * @class RasterBand
 */
Napi::Value RasterBand::New(GDALRasterBand *raw, GDALDataset *raw_parent, bool owned) {

  if (!raw) { return node_gdal::napi_env().Null(); }
  if (object_store().has(raw)) { return object_store().get(raw); }

  node_gdal::detail::PendingObjectGuard pending(raw);
  std::vector<napi_value> args;
  Napi::Object obj = RasterBand::constructor().Value().New(args);
  RasterBand *wrapped = node_gdal::UnwrapWrapped<RasterBand>(obj);

  LOG("Adding band to cache[%p] (parent=%p)", raw, raw_parent);

  // add reference to dataset so dataset doesnt get GC'ed while band is alive
  // DONT USE GDALRasterBand.GetDataset() ... it will return a "fake" dataset
  // for overview bands
  // https://github.com/naturalatlas/node-gdal/blob/master/deps/libgdal/gdal/frmts/gtiff/geotiff.cpp#L84

  Napi::Object ds;
  if (!object_store().has(raw_parent)) {
    LOG("Band's parent dataset disappeared from cache (band = %p, dataset = %p)", raw, raw_parent);
    Napi::Error::New(node_gdal::napi_env(), "Band's parent dataset disappeared from cache").ThrowAsJavaScriptException();
    return node_gdal::napi_env().Undefined();
    // ds = Dataset::New(raw_parent); //this should never happen
  }

  ds = object_store().get(raw_parent);
  Dataset *parent = node_gdal::UnwrapWrapped<Dataset>(ds);
  long parent_uid = parent->uid;
  wrapped->uid = object_store().add(raw, *wrapped, parent_uid, owned);
  wrapped->parent_ds = raw_parent;
  wrapped->parent_uid = parent_uid;

  // the band reads it back in dsGetter, and it also keeps the dataset alive
  node_gdal::set_private(obj, "ds_", ds);

  return obj;
}

/**
 * Saves changes to disk.
 * @method flush
 * @instance
 * @memberof RasterBand
 */

/**
 * Saves changes to disk.
 * @async
 *
 * @method flushAsync
 * @instance
 * @memberof RasterBand
 * @param {callback<void>} [callback=undefined]
 * @return {Promise<void>}
 *
 */
namespace {
// RasterBand.flush()
struct flushBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    auto obj = node_gdal::unwrapThis<RasterBand>(info, "RasterBand", async);
    if (!obj) { return obj.failure; }
    auto *gdal_obj = obj->get();
    GDALAsyncableJob<OGRErr> job(obj->parent_uid);
    job.main = [gdal_obj](const GDALExecutionProgress &) {
      int err = gdal_obj->FlushCache();
      if (err) { throw GDALError(getOGRErrMsg(err)); }
      return err;
    };
    job.rval = [](OGRErr, const GetFromPersistentFunc &) { return v8_undefined(); };
    return job.run(info, node_gdal::is_async<Async>, 0);
  }
};
} // namespace

GDAL_ASYNC_METHOD(RasterBand::flush, flushBody)

/**
 * Return the status flags of the mask band associated with the band.
 *
 * The result will be a bitwise OR-ed set of status flags with the following
 * available definitions that may be extended in the future:
 *
 *   - `GMF_ALL_VALID` (`0x01`): There are no invalid pixels, all mask values
 * will be 255. When used this will normally be the only flag set.
 *   - `GMF_PER_DATASET` (`0x02`): The mask band is shared between all bands on
 * the dataset.
 *   - `GMF_ALPHA` (`0x04`): The mask band is actually an alpha band and may
 * have values other than 0 and 255.
 *   - `GMF_NODATA` (`0x08`): Indicates the mask is actually being generated
 * from nodata values. (mutually exclusive of `GMF_ALPHA`)
 *
 * @method getMaskFlags
 * @instance
 * @memberof RasterBand
 * @return {number} Mask flags ({@link GMF|see GMF constants})
 */
namespace {
// RasterBand::getMaskFlags()
struct getMaskFlagsBody : node_gdal::SyncMethodBody {
  static Napi::Value run(const Napi::CallbackInfo &info) {
    auto obj = node_gdal::unwrapThis<RasterBand>(info, "RasterBand");
    if (!obj) { return obj.failure; }
    GDAL_LOCK_PARENT(obj);
    return Integer::New(info.Env(), obj->get()->GetMaskFlags());
  }
};
} // namespace

GDAL_SYNC_METHOD(RasterBand::getMaskFlags, getMaskFlagsBody)

/**
 * Adds a mask band to the current band.
 *
 * @throws {Error}
 * @method createMaskBand
 * @instance
 * @memberof RasterBand

 * @param {number} flags Mask flags ({@link GMF|see GMF constants})
 */
namespace {
// RasterBand::createMaskBand()
struct createMaskBandBody : node_gdal::SyncMethodBody {
  static Napi::Value run(const Napi::CallbackInfo &info) {
    Args args(info);
    int param;
    if (!args.get(0, "mask flags", param)) { return args.failed(); }
    auto obj = node_gdal::unwrapThis<RasterBand>(info, "RasterBand");
    if (!obj) { return obj.failure; }
    int err = obj->get()->CreateMaskBand(param);
    if (err) { throw GDALError::fromGDAL(); }
    return info.Env().Undefined();
  }
};
} // namespace

GDAL_SYNC_METHOD(RasterBand::createMaskBand, createMaskBandBody)

/**
 * Return the mask band associated with the band.
 *
 * @method getMaskBand
 * @instance
 * @memberof RasterBand
 * @return {RasterBand|null}
 */
/**
 * Fill this band with a constant value.
 *
 * @throws {Error}
 * @method fill
 * @instance
 * @memberof RasterBand
 * @throws {Error}
 * @param {number} real_value
 * @param {number} [imaginary_value]
 */

/**
 * Fill this band with a constant value.
 * @async
 *
 * @throws {Error}
 * @method fillAsync
 * @instance
 * @memberof RasterBand
 * @throws {Error}
 * @param {number} real_value
 * @param {number} [imaginary_value]
 * @param {callback<void>} [callback=undefined]
 * @return {Promise<void>}
 */
namespace {

struct fillBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    Args args(info);
    double real, imaginary = 0;
    if (!args.get(0, "real value", real)) return args.failed();
    if (!args.optional(1, "imaginary value", real)) return args.failed();

    RasterBand *band = node_gdal::unwrap_or_throw<RasterBand>(info.This(), "RasterBand");

    GDALAsyncableJob<CPLErr> job(band->parent_uid);
    GDALRasterBand *gdal_obj = band->get();

    job.main = [gdal_obj, real, imaginary](const GDALExecutionProgress &) {
      CPLErrorReset();
      CPLErr err = gdal_obj->Fill(real, imaginary);
      if (err) { throw GDALError::fromGDAL(); }
      return err;
    };
    job.rval = [](CPLErr, const GetFromPersistentFunc &) { return node_gdal::napi_env().Undefined().As<Napi::Value>(); };

    return job.run(info, node_gdal::is_async<Async>, 2);
  }
};

} // namespace

GDAL_ASYNC_METHOD(RasterBand::fill, fillBody)

// --- Custom error handling to handle VRT errors ---
// see: https://github.com/mapbox/mapnik-omnivore/issues/10
//
// The handler goes on the thread-local stack of GDAL instead of replacing the
// global one, so two statistics which run at the same time in two threads do
// not read each other's message and neither of them has to wait for the other
// one to be done with the raster

thread_local std::string stats_file_err;
thread_local CPLErrorHandler previous_error_handler = nullptr;

void CPL_STDCALL statisticsErrorHandler(CPLErr eErrClass, int err_no, const char *msg) {
  // the first error is the most specific one: the ones which follow it
  // only say that reading a block of the failed source has failed
  if (err_no == CPLE_OpenFailed && stats_file_err.empty()) { stats_file_err = msg; }
  if (previous_error_handler) { previous_error_handler(eErrClass, err_no, msg); }
}
void pushStatsErrorHandler() {
  // The computation which ran before this one on this thread may have left its
  // own message behind and it has nothing to do with this one
  stats_file_err.clear();
  previous_error_handler = CPLGetErrorHandler(nullptr);
  CPLPushErrorHandler(statisticsErrorHandler);
}
void popStatsErrorHandler() {
  if (!previous_error_handler) return;
  CPLPopErrorHandler();
  previous_error_handler = nullptr;
}

/**
 * Return a view of this raster band as a 2D multidimensional GDALMDArray.
 *
 * The band must be linked to a GDALDataset.
 *
 * If the dataset has a geotransform attached, the X and Y dimensions of the returned array will have an associated indexing variable.
 *
 * Requires GDAL>=3.3 with MDArray support, won't be defined otherwise
 *
 * @throws {Error}
 * @method asMDArray
 * @instance
 * @memberof RasterBand
 * @return {MDArray}
 */
#if GDAL_VERSION_MAJOR > 3 || (GDAL_VERSION_MAJOR == 3 && GDAL_VERSION_MINOR >= 1)
#endif

/**
 * Fetch image statistics.
 *
 * Returns the minimum, maximum, mean and standard deviation of all pixel values
 * in this band. If approximate statistics are sufficient, the
 * `allow_approximation` argument can be set to `true` in which case overviews,
 * or a subset of image tiles may be used in computing the statistics.
 *
 * @throws {Error}
 * @method getStatistics
 * @instance
 * @memberof RasterBand
 * @param {boolean} allow_approximation If `true` statistics may be computed
 * based on overviews or a subset of all tiles.
 * @param {boolean} force If `false` statistics will only be returned if it can
 * be done without rescanning the image.
 * @return {object} Statistics containing `"min"`, `"max"`, `"mean"`,
 * `"std_dev"` properties.
 */
/**
 * @typedef {object} stats
 * @property {number} min
 * @property {number} max
 * @property {number} mean
 * @property {number} std_dev
 */

/**
 * Computes image statistics.
 *
 * Returns the minimum, maximum, mean and standard deviation of all pixel values
 * in this band. If approximate statistics are sufficient, the
 * `allow_approximation` argument can be set to `true` in which case overviews,
 * or a subset of image tiles may be used in computing the statistics.
 *
 * @throws {Error}
 * @method computeStatistics
 * @instance
 * @memberof RasterBand

 * @param {boolean} allow_approximation If `true` statistics may be computed
 * based on overviews or a subset of all tiles.
 * @param {object} [options]
 * @param {ProgressCb} [options.progress_cb] Called with the progress of the
 * computation
 * @param {AbortSignal} [options.abort_signal] AbortSignal to interrupt the
 * computation
 * @return {stats} Statistics containing `"min"`, `"max"`, `"mean"`,
 * `"std_dev"` properties.
 */

/**
 * Computes image statistics.
 * {{async}}
 *
 * Returns the minimum, maximum, mean and standard deviation of all pixel values
 * in this band. If approximate statistics are sufficient, the
 * `allow_approximation` argument can be set to `true` in which case overviews,
 * or a subset of image tiles may be used in computing the statistics.
 *
 * @throws {Error}
 * @method computeStatisticsAsync
 * @instance
 * @memberof RasterBand
 * @param {boolean} allow_approximation If `true` statistics may be computed
 * based on overviews or a subset of all tiles.
 * @param {object} [options]
 * @param {ProgressCb} [options.progress_cb] Called with the progress of the
 * computation
 * @param {AbortSignal} [options.abort_signal] AbortSignal to interrupt the
 * computation
 * @param {callback<stats>} [callback=undefined]
 * @return {Promise<stats>} Statistics containing `"min"`, `"max"`, `"mean"`,
 * `"std_dev"` properties.
 */
namespace {

struct computeStatisticsBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    Args args(info);
    struct stats_t {
      double min, max, mean, std_dev;
    };
    int approx;

    if (!args.get(0, "allow approximation", approx)) return args.failed();
    RasterBand *band = node_gdal::unwrap_or_throw<RasterBand>(info.This(), "RasterBand");

    GDALAsyncableJob<stats_t> job(band->parent_uid);
    GDALRasterBand *gdal_obj = band->get();

    Napi::FunctionReference *progress_cb;
    NODE_PROGRESS_CB_OPT(1, progress_cb, job);
    job.progress = progress_cb;
    // An AbortSignal alone is enough to make the operation interruptible, the
    // trampoline has to be installed even without a progress callback
    const bool interruptible = job.interruptible();

    job.main = [gdal_obj, approx, interruptible](const GDALExecutionProgress &progress) {
      struct stats_t stats;

      CPLErrorReset();
      pushStatsErrorHandler();
      CPLErr err = gdal_obj->ComputeStatistics(
        approx,
        &stats.min,
        &stats.max,
        &stats.mean,
        &stats.std_dev,
        interruptible ? ProgressTrampoline : nullptr,
        interruptible ? (void *)&progress : nullptr);
      popStatsErrorHandler();
      if (!stats_file_err.empty()) {
        throw GDALError(stats_file_err);
      } else if (err != CPLE_None) {
        throw GDALError::fromGDAL();
      }

      return stats;
    };

    job.rval = [](stats_t r, const GetFromPersistentFunc &) {
      Napi::Object result = Napi::Object::New(node_gdal::napi_env());
      result.Set( Napi::String::New(node_gdal::napi_env(), "min"), Napi::Number::New(node_gdal::napi_env(), r.min));
      result.Set( Napi::String::New(node_gdal::napi_env(), "max"), Napi::Number::New(node_gdal::napi_env(), r.max));
      result.Set( Napi::String::New(node_gdal::napi_env(), "mean"), Napi::Number::New(node_gdal::napi_env(), r.mean));
      result.Set( Napi::String::New(node_gdal::napi_env(), "std_dev"), Napi::Number::New(node_gdal::napi_env(), r.std_dev));
      return result;
    };

    return job.run(info, node_gdal::is_async<Async>, 2);
  }
};

} // namespace

GDAL_ASYNC_METHOD(RasterBand::computeStatistics, computeStatisticsBody)

/**
 * Set statistics on the band. This method can be used to store
 * min/max/mean/standard deviation statistics.
 *
 * @throws {Error}
 * @method setStatistics
 * @instance
 * @memberof RasterBand
 * @param {number} min
 * @param {number} max
 * @param {number} mean
 * @param {number} std_dev
 */
/**
 * Returns band metadata.
 *
 * @method getMetadata
 * @instance
 * @memberof RasterBand
 * @param {string} [domain]
 * @return {any}
 */

/**
 * Returns band metadata.
 * @async
 *
 * @method getMetadataAsync
 * @instance
 * @memberof RasterBand
 * @param {string} [domain]
 * @param {callback<any>} [callback=undefined]
 * @return {Promise<any>}
 */
namespace {

struct getMetadataBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    Args args(info);

    std::string domain("");
    if (!args.optional(0, "domain", domain)) return args.failed();
    RasterBand *band = node_gdal::unwrap_or_throw<RasterBand>(info.This(), "RasterBand");
    GDALRasterBand * raw = node_gdal::raw_or_throw<GDALRasterBand *>(band, "GDALRasterBand");

    GDALAsyncableJob<CSLConstList> job(band->parent_uid);
    job.main = [raw, domain](const GDALExecutionProgress &) {
      return raw->GetMetadata(domain.empty() ? nullptr : domain.c_str());
    };
    job.rval = [](CSLConstList md, const GetFromPersistentFunc &) { return MajorObject::getMetadata(md); };
    return job.run(info, node_gdal::is_async<Async>, 1);
  }
};

} // namespace

GDAL_ASYNC_METHOD(RasterBand::getMetadata, getMetadataBody)

/**
 * Set metadata. Can return a warning (false) for formats not supporting persistent metadata.
 *
 * @method setMetadata
 * @instance
 * @memberof RasterBand
 * @param {object|string[]} metadata
 * @param {string} [domain]
 * @return {boolean}
 */

/**
 * Set metadata. Can return a warning (false) for formats not supporting persistent metadata.
 * @async
 *
 * @method setMetadataAsync
 * @instance
 * @memberof RasterBand
 * @param {object|string[]} metadata
 * @param {string} [domain]
 * @param {callback<boolean>} [callback=undefined]
 * @return {Promise<boolean>}
 */
namespace {

struct setMetadataBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    Args args(info);
    RasterBand *band = node_gdal::unwrap_or_throw<RasterBand>(info.This(), "RasterBand");
    GDALRasterBand * raw = node_gdal::raw_or_throw<GDALRasterBand *>(band, "GDALRasterBand");

    auto options = make_shared<StringList>();
    if (info.Length() == 0 || options->parse(info[0])) {
      node_gdal::ThrowOverPending(node_gdal::napi_env(), "Failed parsing metadata");
      return node_gdal::napi_env().Undefined();
    }

    std::string domain("");
    if (!args.optional(1, "domain", domain)) return args.failed();

    GDALAsyncableJob<CPLErr> job(band->parent_uid);
    job.main = [raw, options, domain](const GDALExecutionProgress &) {
      CPLErr r = raw->SetMetadata(options->get(), domain.empty() ? nullptr : domain.c_str());
      if (r == CE_Failure) throw GDALError::fromGDAL();
      return r;
    };
    job.rval = [](CPLErr r, const GetFromPersistentFunc &) { return Napi::Boolean::New(node_gdal::napi_env(), r == CE_None); };
    return job.run(info, node_gdal::is_async<Async>, 2);
  }
};

} // namespace

GDAL_ASYNC_METHOD(RasterBand::setMetadata, setMetadataBody)

/**
 * @readonly
 * @kind member
 * @name ds
 * @instance
 * @memberof RasterBand
 * @type {Dataset}
 */
/**
 * @readonly
 * @kind member
 * @name overviews
 * @instance
 * @memberof RasterBand
 * @type {RasterBandOverviews}
 */
/**
 * @readonly
 * @kind member
 * @name pixels
 * @instance
 * @memberof RasterBand
 * @type {RasterBandPixels}
 */
/**
 * @readonly
 * @kind member
 * @name id
 * @instance
 * @memberof RasterBand
 * @type {number|null}
 */

/**
 * @readonly
 * @kind member
 * @name idAsync
 * @instance
 * @memberof RasterBand
 * @type {Promise<number|null>}
 * @asyncGetter
 */

namespace {
struct idGetterBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    RasterBand *band = node_gdal::unwrap_or_throw<RasterBand>(info.This(), "RasterBand");
    GDALRasterBand * raw = node_gdal::raw_or_throw<GDALRasterBand *>(band, "GDALRasterBand");

    GDALAsyncableJob<int> job(band->parent_uid);
    job.main = [raw](const GDALExecutionProgress &) {
      CPLErrorReset();
      return raw->GetBand();
    };
    job.rval = [](int id, const GetFromPersistentFunc &) {
      if (id == 0) return node_gdal::napi_env().Null().As<Napi::Value>();
      return Napi::Number::New(node_gdal::napi_env(), id).As<Napi::Value>();
    };
    return job.run(info, node_gdal::is_async<Async>);
  }
};
} // namespace

GDAL_ASYNC_GETTER(RasterBand::idGetter, idGetterBody)

/**
 * Name of of band.
 *
 * @readonly
 * @kind member
 * @name description
 * @instance
 * @memberof RasterBand
 * @type {string}
 */

/**
 * Name of of band.
 * @asyncGetter
 *
 * @readonly
 * @kind member
 * @name descriptionAsync
 * @instance
 * @memberof RasterBand
 * @type {Promise<string>}
 */
namespace {
struct descriptionGetterBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    RasterBand *band = node_gdal::unwrap_or_throw<RasterBand>(info.This(), "RasterBand");
    GDALRasterBand * raw = node_gdal::raw_or_throw<GDALRasterBand *>(band, "GDALRasterBand");

    GDALAsyncableJob<const char *> job(band->parent_uid);
    job.main = [raw](const GDALExecutionProgress &) { return raw->GetDescription(); };
    job.rval = [](const char *desc, const GetFromPersistentFunc &) { return SafeString::New(desc); };
    return job.run(info, node_gdal::is_async<Async>);
  }
};
} // namespace

GDAL_ASYNC_GETTER(RasterBand::descriptionGetter, descriptionGetterBody)

/**
 * Size object containing `"x"` and `"y"` properties.
 *
 * @readonly
 * @kind member
 * @name size
 * @instance
 * @memberof RasterBand
 * @type {xyz}
 */

/**
 * Size object containing `"x"` and `"y"` properties.
 * @asyncGetter
 *
 * @readonly
 * @kind member
 * @name sizeAsync
 * @instance
 * @memberof RasterBand
 * @type {Promise<xyz>}
 */
namespace {
struct sizeGetterBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    RasterBand *band = node_gdal::unwrap_or_throw<RasterBand>(info.This(), "RasterBand");
    GDALRasterBand * raw = node_gdal::raw_or_throw<GDALRasterBand *>(band, "GDALRasterBand");

    struct xy {
      int x, y;
    };
    GDALAsyncableJob<xy> job(band->parent_uid);
    job.main = [raw](const GDALExecutionProgress &) {
      xy r;
      r.x = raw->GetXSize();
      r.y = raw->GetYSize();
      return r;
    };
    job.rval = [](xy r, const GetFromPersistentFunc &) {
      Napi::Object result = Napi::Object::New(node_gdal::napi_env());
      result.Set( Napi::String::New(node_gdal::napi_env(), "x"), Napi::Number::New(node_gdal::napi_env(), r.x));
      result.Set( Napi::String::New(node_gdal::napi_env(), "y"), Napi::Number::New(node_gdal::napi_env(), r.y));
      return result;
    };
    return job.run(info, node_gdal::is_async<Async>);
  }
};
} // namespace

GDAL_ASYNC_GETTER(RasterBand::sizeGetter, sizeGetterBody)

/**
 * Size object containing `"x"` and `"y"` properties.
 *
 * @readonly
 * @kind member
 * @name blockSize
 * @instance
 * @memberof RasterBand
 * @type {xyz}
 */

/**
 * Size object containing `"x"` and `"y"` properties.
 * @asyncGetter
 *
 * @readonly
 * @kind member
 * @name blockSizeAsync
 * @instance
 * @memberof RasterBand
 * @type {Promise<xyz>}
 */
namespace {
struct blockSizeGetterBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    RasterBand *band = node_gdal::unwrap_or_throw<RasterBand>(info.This(), "RasterBand");
    GDALRasterBand * raw = node_gdal::raw_or_throw<GDALRasterBand *>(band, "GDALRasterBand");

    struct xy {
      int x, y;
    };
    GDALAsyncableJob<xy> job(band->parent_uid);
    job.main = [raw](const GDALExecutionProgress &) {
      xy r;
      raw->GetBlockSize(&r.x, &r.y);
      return r;
    };
    job.rval = [](xy r, const GetFromPersistentFunc &) {
      Napi::Object result = Napi::Object::New(node_gdal::napi_env());
      result.Set( Napi::String::New(node_gdal::napi_env(), "x"), Napi::Number::New(node_gdal::napi_env(), r.x));
      result.Set( Napi::String::New(node_gdal::napi_env(), "y"), Napi::Number::New(node_gdal::napi_env(), r.y));
      return result;
    };
    return job.run(info, node_gdal::is_async<Async>);
  }
};
} // namespace

GDAL_ASYNC_GETTER(RasterBand::blockSizeGetter, blockSizeGetterBody)

template <typename T> struct MaybeResult {
  T value;
  int success;
};

/**
 * Minimum value for this band.
 *
 * @readonly
 * @kind member
 * @name minimum
 * @instance
 * @memberof RasterBand
 * @type {number|null}
 */

/**
 * Minimum value for this band.
 * @asyncGetter
 *
 * @readonly
 * @kind member
 * @name minimumAsync
 * @instance
 * @memberof RasterBand
 * @type {Promise<number|null>}
 */
namespace {
struct minimumGetterBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    RasterBand *band = node_gdal::unwrap_or_throw<RasterBand>(info.This(), "RasterBand");
    GDALRasterBand * raw = node_gdal::raw_or_throw<GDALRasterBand *>(band, "GDALRasterBand");

    GDALAsyncableJob<MaybeResult<double>> job(band->parent_uid);
    job.main = [raw](const GDALExecutionProgress &) {
      MaybeResult<double> r;
      CPLErrorReset();
      r.value = raw->GetMinimum(&r.success);
      return r;
    };
    job.rval = [](MaybeResult<double> r, const GetFromPersistentFunc &) {
      if (r.success) return Napi::Number::New(node_gdal::napi_env(), r.value).As<Napi::Value>();
      return node_gdal::napi_env().Null().As<Napi::Value>();
    };
    return job.run(info, node_gdal::is_async<Async>);
  }
};
} // namespace

GDAL_ASYNC_GETTER(RasterBand::minimumGetter, minimumGetterBody)

/**
 * Maximum value for this band.
 *
 * @readonly
 * @kind member
 * @name maximum
 * @instance
 * @memberof RasterBand
 * @type {number|null}
 */

/**
 * Maximum value for this band.
 * @asyncGetter
 *
 * @readonly
 * @kind member
 * @name maximumAsync
 * @instance
 * @memberof RasterBand
 * @type {Promise<number|null>}
 */
namespace {
struct maximumGetterBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    RasterBand *band = node_gdal::unwrap_or_throw<RasterBand>(info.This(), "RasterBand");
    GDALRasterBand * raw = node_gdal::raw_or_throw<GDALRasterBand *>(band, "GDALRasterBand");

    GDALAsyncableJob<MaybeResult<double>> job(band->parent_uid);
    job.main = [raw](const GDALExecutionProgress &) {
      MaybeResult<double> r;
      CPLErrorReset();
      r.value = raw->GetMaximum(&r.success);
      return r;
    };
    job.rval = [](MaybeResult<double> r, const GetFromPersistentFunc &) {
      if (r.success) return Napi::Number::New(node_gdal::napi_env(), r.value).As<Napi::Value>();
      return node_gdal::napi_env().Null().As<Napi::Value>();
    };
    return job.run(info, node_gdal::is_async<Async>);
  }
};
} // namespace

GDAL_ASYNC_GETTER(RasterBand::maximumGetter, maximumGetterBody)

/**
 * Raster value offset.
 *
 * @kind member
 * @name offset
 * @instance
 * @memberof RasterBand
 * @type {number|null}
 */

/**
 * Raster value offset.
 * @asyncGetter
 *
 * @kind member
 * @name offsetAsync
 * @instance
 * @memberof RasterBand
 * @readonly
 * @type {Promise<number|null>}
 */
namespace {
struct offsetGetterBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    RasterBand *band = node_gdal::unwrap_or_throw<RasterBand>(info.This(), "RasterBand");
    GDALRasterBand * raw = node_gdal::raw_or_throw<GDALRasterBand *>(band, "GDALRasterBand");

    GDALAsyncableJob<MaybeResult<double>> job(band->parent_uid);
    job.main = [raw](const GDALExecutionProgress &) {
      MaybeResult<double> r;
      CPLErrorReset();
      r.value = raw->GetOffset(&r.success);
      return r;
    };
    job.rval = [](MaybeResult<double> r, const GetFromPersistentFunc &) {
      if (r.success) return Napi::Number::New(node_gdal::napi_env(), r.value).As<Napi::Value>();
      return node_gdal::napi_env().Null().As<Napi::Value>();
    };
    return job.run(info, node_gdal::is_async<Async>);
  }
};
} // namespace

GDAL_ASYNC_GETTER(RasterBand::offsetGetter, offsetGetterBody)

/**
 * Raster value scale.
 *
 * @kind member
 * @name scale
 * @instance
 * @memberof RasterBand
 * @type {number|null}
 */

/**
 * Raster value scale.
 * @asyncGetter
 *
 * @kind member
 * @name scaleAsync
 * @instance
 * @memberof RasterBand
 * @readonly
 * @type {Promise<number|null>}
 */
namespace {
struct scaleGetterBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    RasterBand *band = node_gdal::unwrap_or_throw<RasterBand>(info.This(), "RasterBand");
    GDALRasterBand * raw = node_gdal::raw_or_throw<GDALRasterBand *>(band, "GDALRasterBand");

    GDALAsyncableJob<MaybeResult<double>> job(band->parent_uid);
    job.main = [raw](const GDALExecutionProgress &) {
      MaybeResult<double> r;
      CPLErrorReset();
      r.value = raw->GetScale(&r.success);
      return r;
    };
    job.rval = [](MaybeResult<double> r, const GetFromPersistentFunc &) {
      if (r.success) return Napi::Number::New(node_gdal::napi_env(), r.value).As<Napi::Value>();
      return node_gdal::napi_env().Null().As<Napi::Value>();
    };
    return job.run(info, node_gdal::is_async<Async>);
  }
};
} // namespace

GDAL_ASYNC_GETTER(RasterBand::scaleGetter, scaleGetterBody)

/**
 * No data value for this band.
 *
 * @kind member
 * @name noDataValue
 * @instance
 * @memberof RasterBand
 * @type {number|null}
 */

/**
 * No data value for this band.
 * @asyncGetter
 *
 * @kind member
 * @name noDataValueAsync
 * @instance
 * @memberof RasterBand
 * @readonly
 * @type {Promise<number|null>}
 */
namespace {
struct noDataValueGetterBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    RasterBand *band = node_gdal::unwrap_or_throw<RasterBand>(info.This(), "RasterBand");
    GDALRasterBand * raw = node_gdal::raw_or_throw<GDALRasterBand *>(band, "GDALRasterBand");

    GDALAsyncableJob<MaybeResult<double>> job(band->parent_uid);
    job.main = [raw](const GDALExecutionProgress &) {
      MaybeResult<double> r;
      CPLErrorReset();
      r.value = raw->GetNoDataValue(&r.success);
      return r;
    };
    job.rval = [](MaybeResult<double> r, const GetFromPersistentFunc &) {
      if (r.success)
        return Napi::Number::New(node_gdal::napi_env(), r.value).As<Napi::Value>();
      else
        return node_gdal::napi_env().Null().As<Napi::Value>();
    };
    return job.run(info, node_gdal::is_async<Async>);
  }
};
} // namespace

GDAL_ASYNC_GETTER(RasterBand::noDataValueGetter, noDataValueGetterBody)

/**
 * Raster unit type (name for the units of this raster's values).
 * For instance, it might be `"m"` for an elevation model in meters,
 * or `"ft"` for feet. If no units are available, a value of `""`
 * will be returned.
 *
 * @kind member
 * @name unitType
 * @instance
 * @memberof RasterBand
 * @type {string|null}
 */

/**
 * Raster unit type (name for the units of this raster's values).
 * For instance, it might be `"m"` for an elevation model in meters,
 * or `"ft"` for feet. If no units are available, a value of `""`
 * will be returned.
 * @asyncGetter
 *
 * @kind member
 * @name unitTypeAsync
 * @instance
 * @memberof RasterBand
 * @readonly
 * @type {Promise<string|null>}
 */
namespace {
struct unitTypeGetterBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    RasterBand *band = node_gdal::unwrap_or_throw<RasterBand>(info.This(), "RasterBand");
    GDALRasterBand * raw = node_gdal::raw_or_throw<GDALRasterBand *>(band, "GDALRasterBand");

    GDALAsyncableJob<const char *> job(band->parent_uid);
    job.main = [raw](const GDALExecutionProgress &) {
      CPLErrorReset();
      return raw->GetUnitType();
    };
    job.rval = [](const char *r, const GetFromPersistentFunc &) { return SafeString::New(r); };
    return job.run(info, node_gdal::is_async<Async>);
  }
};
} // namespace

GDAL_ASYNC_GETTER(RasterBand::unitTypeGetter, unitTypeGetterBody)

/**
 *
 * Pixel data type ({@link GDT|see GDT constants} used for this band.
 *
 * @readonly
 * @kind member
 * @name dataType
 * @instance
 * @memberof RasterBand
 * @type {string|null}
 */

/**
 *
 * Pixel data type ({@link GDT|see GDT constants} used for this band.
 * @asyncGetter
 *
 * @readonly
 * @kind member
 * @name dataTypeAsync
 * @instance
 * @memberof RasterBand
 * @type {Promise<string|null>}
 */
namespace {
struct dataTypeGetterBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    RasterBand *band = node_gdal::unwrap_or_throw<RasterBand>(info.This(), "RasterBand");
    GDALRasterBand * raw = node_gdal::raw_or_throw<GDALRasterBand *>(band, "GDALRasterBand");

    GDALAsyncableJob<GDALDataType> job(band->parent_uid);
    job.main = [raw](const GDALExecutionProgress &) {
      CPLErrorReset();
      return raw->GetRasterDataType();
    };
    job.rval = [](GDALDataType type, const GetFromPersistentFunc &) {
      if (type == GDT_Unknown) return node_gdal::napi_env().Null().As<Napi::Value>();
      return SafeString::New(GDALGetDataTypeName(type));
    };
    return job.run(info, node_gdal::is_async<Async>);
  }
};
} // namespace

GDAL_ASYNC_GETTER(RasterBand::dataTypeGetter, dataTypeGetterBody)

/**
 * Indicates if the band is read-only.
 *
 * @readonly
 * @kind member
 * @name readOnly
 * @instance
 * @memberof RasterBand
 * @type {boolean}
 */

/**
 * Indicates if the band is read-only.
 * @asyncGetter
 *
 * @readonly
 * @kind member
 * @name readOnlyAsync
 * @instance
 * @memberof RasterBand
 * @type {Promise<boolean>}
 */
namespace {
struct readOnlyGetterBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    RasterBand *band = node_gdal::unwrap_or_throw<RasterBand>(info.This(), "RasterBand");
    GDALRasterBand * raw = node_gdal::raw_or_throw<GDALRasterBand *>(band, "GDALRasterBand");

    GDALAsyncableJob<GDALAccess> job(band->parent_uid);
    job.main = [raw](const GDALExecutionProgress &) {
      CPLErrorReset();
      return raw->GetAccess();
    };
    job.rval = [](GDALAccess r, const GetFromPersistentFunc &) { return (r == GA_Update ? Napi::Boolean::New(node_gdal::napi_env(), false) : Napi::Boolean::New(node_gdal::napi_env(), true)); };
    return job.run(info, node_gdal::is_async<Async>);
  }
};
} // namespace

GDAL_ASYNC_GETTER(RasterBand::readOnlyGetter, readOnlyGetterBody)

/**
 * An indicator if the underlying datastore can compute arbitrary overviews
 * efficiently, such as is the case with OGDI over a network. Datastores with
 * arbitrary overviews don't generally have any fixed overviews, but GDAL's
 * `RasterIO()` method can be used in downsampling mode to get overview
 * data efficiently.
 *
 * @readonly
 * @kind member
 * @name hasArbitraryOverviews
 * @instance
 * @memberof RasterBand
 * @type {boolean}
 */

/**
 * An indicator if the underlying datastore can compute arbitrary overviews
 * efficiently, such as is the case with OGDI over a network. Datastores with
 * arbitrary overviews don't generally have any fixed overviews, but GDAL's
 * `RasterIO()` method can be used in downsampling mode to get overview
 * data efficiently.
 * @asyncGetter
 *
 * @readonly
 * @kind member
 * @name hasArbitraryOverviewsAsync
 * @instance
 * @memberof RasterBand
 * @type {Promise<boolean>}
 */
namespace {
struct hasArbitraryOverviewsGetterBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    RasterBand *band = node_gdal::unwrap_or_throw<RasterBand>(info.This(), "RasterBand");
    GDALRasterBand * raw = node_gdal::raw_or_throw<GDALRasterBand *>(band, "GDALRasterBand");

    GDALAsyncableJob<bool> job(band->parent_uid);
    job.main = [raw](const GDALExecutionProgress &) {
      CPLErrorReset();
      return static_cast<bool>(raw->HasArbitraryOverviews());
    };
    job.rval = [](bool r, const GetFromPersistentFunc &) { return Napi::Boolean::New(node_gdal::napi_env(), r); };
    return job.run(info, node_gdal::is_async<Async>);
  }
};
} // namespace

GDAL_ASYNC_GETTER(RasterBand::hasArbitraryOverviewsGetter, hasArbitraryOverviewsGetterBody)

/**
 * List of list of category names for this raster.
 *
 * @kind member
 * @name categoryNames
 * @instance
 * @memberof RasterBand
 * @type {string[]}
 */

/**
 * List of list of category names for this raster.
 * @asyncGetter
 *
 * @kind member
 * @name categoryNamesAsync
 * @instance
 * @memberof RasterBand
 * @readonly
 * @type {Promise<string[]>}
 */
namespace {
struct categoryNamesGetterBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    RasterBand *band = node_gdal::unwrap_or_throw<RasterBand>(info.This(), "RasterBand");
    GDALRasterBand * raw = node_gdal::raw_or_throw<GDALRasterBand *>(band, "GDALRasterBand");

    GDALAsyncableJob<std::shared_ptr<std::vector<std::string>>> job(band->parent_uid);
    job.main = [raw](const GDALExecutionProgress &) {
      auto names = std::make_shared<std::vector<std::string>>();
      CPLErrorReset();
      // Here we copy twice what we could have copied only once
      // In practice, GetCategoryNames always returns the same buffer
      // But in theory, a clever driver could implement some crazy lazy method
      // So we do everything "The Right Way"
      // This is not a performance-critical function anyway
      char **raw_names = raw->GetCategoryNames();
      int i = 0;

      while (raw_names != nullptr && raw_names[i] != nullptr) {
        names->push_back(raw_names[i]);
        i++;
      }
      return names;
    };
    job.rval = [](std::shared_ptr<std::vector<std::string>> names, const GetFromPersistentFunc &) {
      Napi::Array results = Napi::Array::New(node_gdal::napi_env());
      for (std::size_t i = 0; i < names->size(); ++i) results.Set( i, SafeString::New((*names.get())[i].c_str()));
      return results;
    };
    return job.run(info, node_gdal::is_async<Async>);
  }
};
} // namespace

GDAL_ASYNC_GETTER(RasterBand::categoryNamesGetter, categoryNamesGetterBody)

/**
 * Color interpretation mode ({@link GCI|see GCI constants})
 *
 * @kind member
 * @name colorInterpretation
 * @instance
 * @memberof RasterBand
 * @type {string|null}
 */

/**
 * Color interpretation mode ({@link GCI|see GCI constants})
 * @asyncGetter
 *
 * @kind member
 * @name colorInterpretationAsync
 * @instance
 * @memberof RasterBand
 * @readonly
 * @type {Promise<string|null>}
 */
namespace {
struct colorInterpretationGetterBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    RasterBand *band = node_gdal::unwrap_or_throw<RasterBand>(info.This(), "RasterBand");
    GDALRasterBand * raw = node_gdal::raw_or_throw<GDALRasterBand *>(band, "GDALRasterBand");

    GDALAsyncableJob<GDALColorInterp> job(band->parent_uid);
    job.persist("this", info.This());
    job.main = [raw](const GDALExecutionProgress &) { return raw->GetColorInterpretation(); };
    job.rval = [](GDALColorInterp ci, const GetFromPersistentFunc &) {
      if (ci == GCI_Undefined)
        return node_gdal::napi_env().Undefined().As<Napi::Value>();
      else
        return SafeString::New(GDALGetColorInterpretationName(ci));
    };

    return job.run(info, node_gdal::is_async<Async>);
  }
};
} // namespace

GDAL_ASYNC_GETTER(RasterBand::colorInterpretationGetter, colorInterpretationGetterBody)

/**
 * Color table ({@link ColorTable|see ColorTable})
 *
 * @kind member
 * @name colorTable
 * @instance
 * @memberof RasterBand
 * @type {ColorTable|null}
 */

/**
 * Color table ({@link ColorTable|see ColorTable})
 * @asyncGetter
 *
 * @kind member
 * @name colorTableAsync
 * @instance
 * @memberof RasterBand
 * @readonly
 * @type {Promise<ColorTable|null>}
 */
namespace {
struct colorTableGetterBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    RasterBand *band = node_gdal::unwrap_or_throw<RasterBand>(info.This(), "RasterBand");

    GDALRasterBand * raw = node_gdal::raw_or_throw<GDALRasterBand *>(band, "GDALRasterBand");

    GDALAsyncableJob<GDALColorTable *> job(band->parent_uid);
    job.main = [raw](const GDALExecutionProgress &) { return raw->GetColorTable(); };
    job.rval = [](GDALColorTable *ct, const GetFromPersistentFunc &getter) {
      if (ct != nullptr) return ColorTable::New(ct, getter("this"));
      return node_gdal::napi_env().Undefined().As<Napi::Value>();
    };

    return job.run(info, node_gdal::is_async<Async>);
  }
};
} // namespace

GDAL_ASYNC_GETTER(RasterBand::colorTableGetter, colorTableGetterBody)

namespace {

Napi::Value toString(const Napi::CallbackInfo &info) {
  return Napi::String::New(node_gdal::napi_env(), "RasterBand");
}


Napi::Value getMaskBand(const Napi::CallbackInfo &info) {
  RasterBand *band = node_gdal::unwrap_or_throw<RasterBand>(info.This(), "RasterBand");
  GDAL_LOCK_PARENT(band);
  GDALRasterBand *mask_band = band->get()->GetMaskBand();

  if (!mask_band) {
    return node_gdal::napi_env().Null();
    return node_gdal::napi_env().Undefined();
  }

  return RasterBand::New(mask_band, band->getParent());
}


Napi::Value asMDArray(const Napi::CallbackInfo &info) {
  RasterBand *band = node_gdal::unwrap_or_throw<RasterBand>(info.This(), "RasterBand");
  GDALRasterBand * raw = node_gdal::raw_or_throw<GDALRasterBand *>(band, "GDALRasterBand");
  GDAL_LOCK_PARENT(band);
  CPLErrorReset();
  std::shared_ptr<GDALMDArray> mdarray = raw->AsMDArray();
  if (mdarray == nullptr) {
    Napi::Error::New(node_gdal::napi_env(), CPLGetLastErrorMsg()).ThrowAsJavaScriptException();
    return node_gdal::napi_env().Undefined();
  }
  Napi::Value obj = MDArray::New(mdarray, band->getParent());
  return obj;
}

Napi::Value getStatistics(const Napi::CallbackInfo &info) {
  Args args(info);
  double min, max, mean, std_dev;
  int approx, force;
  if (!args.get(0, "allow approximation", approx)) return args.failed();
  if (!args.get(1, "force", force)) return args.failed();
  RasterBand *band = node_gdal::unwrap_or_throw<RasterBand>(info.This(), "RasterBand");
  GDAL_LOCK_PARENT(band);
  pushStatsErrorHandler();
  CPLErr err = band->get()->GetStatistics(approx, force, &min, &max, &mean, &std_dev);
  popStatsErrorHandler();
  if (!stats_file_err.empty()) {
    Napi::Error::New(node_gdal::napi_env(), stats_file_err.c_str()).ThrowAsJavaScriptException();
  } else if (err) {
    if (!force && err == CE_Warning) {
      Napi::Error::New(node_gdal::napi_env(), "Statistics cannot be efficiently computed without scanning raster").ThrowAsJavaScriptException();
      return node_gdal::napi_env().Undefined();
    }
    throw GDALError::fromGDAL();
    return node_gdal::napi_env().Undefined();
  }

  Napi::Object result = Napi::Object::New(node_gdal::napi_env());
  result.Set( Napi::String::New(node_gdal::napi_env(), "min"), Napi::Number::New(node_gdal::napi_env(), min));
  result.Set( Napi::String::New(node_gdal::napi_env(), "max"), Napi::Number::New(node_gdal::napi_env(), max));
  result.Set( Napi::String::New(node_gdal::napi_env(), "mean"), Napi::Number::New(node_gdal::napi_env(), mean));
  result.Set( Napi::String::New(node_gdal::napi_env(), "std_dev"), Napi::Number::New(node_gdal::napi_env(), std_dev));

  return result;
}


Napi::Value setStatistics(const Napi::CallbackInfo &info) {
  Args args(info);
  double min, max, mean, std_dev;

  if (!args.get(0, "min", min)) return args.failed();
  if (!args.get(1, "max", max)) return args.failed();
  if (!args.get(2, "mean", mean)) return args.failed();
  if (!args.get(3, "standard deviation", std_dev)) return args.failed();
  RasterBand *band = node_gdal::unwrap_or_throw<RasterBand>(info.This(), "RasterBand");
  GDAL_LOCK_PARENT(band);
  CPLErr err = band->get()->SetStatistics(min, max, mean, std_dev);

  if (err) {
    throw GDALError::fromGDAL();
    return node_gdal::napi_env().Undefined();
  }
  return node_gdal::napi_env().Undefined();
}


Napi::Value dsGetter(const Napi::CallbackInfo &info) {
  node_gdal::requireAlive<RasterBand>(info.This(), "RasterBand");
  return node_gdal::get_private(info.This(), "ds_");
}


Napi::Value overviewsGetter(const Napi::CallbackInfo &info) {
  node_gdal::requireAlive<RasterBand>(info.This(), "RasterBand");
  return node_gdal::get_private(info.This(), "overviews_");
}


Napi::Value pixelsGetter(const Napi::CallbackInfo &info) {
  node_gdal::requireAlive<RasterBand>(info.This(), "RasterBand");
  return node_gdal::get_private(info.This(), "pixels_");
}


void unitTypeSetter(const Napi::CallbackInfo &info, const Napi::Value &value) {
  RasterBand *band = node_gdal::unwrap_or_throw<RasterBand>(info.This(), "RasterBand");
  if (!value.IsString()) {
    Napi::Error::New(node_gdal::napi_env(), "Unit type must be a string").ThrowAsJavaScriptException();
    return;
  }
  std::string input = value.As<Napi::String>().Utf8Value();
  GDAL_LOCK_PARENT_VOID(band);
  CPLErr err = band->get()->SetUnitType(input.c_str());
  if (err) { throw GDALError::fromGDAL(); }
}


void noDataValueSetter(const Napi::CallbackInfo &info, const Napi::Value &value) {
  RasterBand *band = node_gdal::unwrap_or_throw<RasterBand>(info.This(), "RasterBand");

  CPLErr err;
  GDAL_LOCK_PARENT_VOID(band);
  CPLErrorReset();
  if (value.IsNull() || value.IsUndefined()) {
    err = band->get()->DeleteNoDataValue();
  } else if (value.IsNumber()) {
    err = band->get()->SetNoDataValue(value.As<Napi::Number>().DoubleValue());
  } else {
    Napi::Error::New(node_gdal::napi_env(), "No data value must be a number").ThrowAsJavaScriptException();
    return;
  }

  if (err != CE_None) { throw GDALError::fromGDAL(); }
}


void scaleSetter(const Napi::CallbackInfo &info, const Napi::Value &value) {
  RasterBand *band = node_gdal::unwrap_or_throw<RasterBand>(info.This(), "RasterBand");

  if (!value.IsNumber()) {
    Napi::Error::New(node_gdal::napi_env(), "Scale must be a number").ThrowAsJavaScriptException();
    return;
  }
  double input = value.As<Napi::Number>().DoubleValue();
  GDAL_LOCK_PARENT_VOID(band);
  CPLErr err = band->get()->SetScale(input);
  if (err) { throw GDALError::fromGDAL(); }
}


void offsetSetter(const Napi::CallbackInfo &info, const Napi::Value &value) {
  RasterBand *band = node_gdal::unwrap_or_throw<RasterBand>(info.This(), "RasterBand");

  if (!value.IsNumber()) {
    Napi::Error::New(node_gdal::napi_env(), "Offset must be a number").ThrowAsJavaScriptException();
    return;
  }
  double input = value.As<Napi::Number>().DoubleValue();
  GDAL_LOCK_PARENT_VOID(band);
  CPLErr err = band->get()->SetOffset(input);
  if (err) { throw GDALError::fromGDAL(); }
}


void categoryNamesSetter(const Napi::CallbackInfo &info, const Napi::Value &value) {
  RasterBand *band = node_gdal::unwrap_or_throw<RasterBand>(info.This(), "RasterBand");

  if (!value.IsArray()) {
    Napi::Error::New(node_gdal::napi_env(), "Category names must be an array").ThrowAsJavaScriptException();
    return;
  }
  Napi::Array names = value.As<Napi::Array>();

  char **list = nullptr;
  std::shared_ptr<std::string[]> strlist;

  if (names.Length() > 0) {
    list = new char *[names.Length() + 1];
    strlist = std::shared_ptr<std::string[]>(new std::string[names.Length()]);
    unsigned int i;
    for (i = 0; i < names.Length(); i++) {
      strlist.get()[i] = names.As<Napi::Object>().Get(i).As<Napi::String>().Utf8Value();
      list[i] = (char *)strlist.get()[i].c_str();
    }
    list[i] = nullptr;
  }

  GDAL_LOCK_PARENT_VOID(band);
  int err = band->get()->SetCategoryNames(list);

  if (list) { delete[] list; }

  if (err) { throw GDALError::fromGDAL(); }
}


void colorInterpretationSetter(const Napi::CallbackInfo &info, const Napi::Value &value) {
  RasterBand *band = node_gdal::unwrap_or_throw<RasterBand>(info.This(), "RasterBand");

  GDALColorInterp ci = GCI_Undefined;

  if (value.IsString()) {
    std::string name = value.As<Napi::String>().Utf8Value();
    ci = GDALGetColorInterpretationByName(name.c_str());
  } else if (!value.IsNull() && !value.IsUndefined()) {
    Napi::Error::New(node_gdal::napi_env(), "color interpretation must be a string or undefined").ThrowAsJavaScriptException();
    return;
  }

  GDAL_LOCK_PARENT_VOID(band);
  CPLErr err = band->get()->SetColorInterpretation(ci);
  if (err) { throw GDALError::fromGDAL(); }
}


void colorTableSetter(const Napi::CallbackInfo &info, const Napi::Value &value) {

  RasterBand *band = node_gdal::unwrap_or_throw<RasterBand>(info.This(), "RasterBand");

  GDALColorTable *raw;
  if (value.IsNull() || value.IsUndefined()) {
    raw = nullptr;
  } else if (node_gdal::IsInstanceOf<ColorTable>(value)) {
    ColorTable *ct = node_gdal::unwrap_or_throw<ColorTable>(value.As<Napi::Object>(), "ColorTable");
    GDALColorTable * _raw = node_gdal::raw_or_throw<GDALColorTable *>(ct, "GDALColorTable");
    raw = _raw;
  } else {
    Napi::TypeError::New(node_gdal::napi_env(), "color table must be a gdal.ColorTable object or null").ThrowAsJavaScriptException();
    return;
  }

  GDAL_LOCK_PARENT_VOID(band);
  CPLErr err = band->get()->SetColorTable(raw);
  if (err != CE_None) { throw GDALError::fromGDAL(); }
}


Napi::Value uidGetter(const Napi::CallbackInfo &info) {
  RasterBand *band = node_gdal::UnwrapWrapped<RasterBand>(info.This().As<Napi::Object>());
  return Napi::Number::New(node_gdal::napi_env(), (int)band->uid);
}

} // namespace

void RasterBand::Initialize(Napi::Object target) {
  Napi::Env env = target.Env();
  static constexpr MethodEntry methods[] = {
    method("toString", toString),
    asyncMethod("flush", flush, flushAsync),
    asyncMethod("fill", fill, fillAsync),
#if GDAL_VERSION_MAJOR > 3 || (GDAL_VERSION_MAJOR == 3 && GDAL_VERSION_MINOR >= 1)
    method("asMDArray", asMDArray),
#endif
    method("getStatistics", getStatistics),
    method("setStatistics", setStatistics),
    asyncMethod("computeStatistics", computeStatistics, computeStatisticsAsync),
    method("getMaskBand", getMaskBand),
    method("getMaskFlags", getMaskFlags),
    method("createMaskBand", createMaskBand),
    asyncMethod("getMetadata", getMetadata, getMetadataAsync),
    asyncMethod("setMetadata", setMetadata, setMetadataAsync),
  };

  static constexpr AccessorEntry accessors[] = {
    hiddenAttr("ds", dsGetter, READ_ONLY_SETTER),
    hiddenAttr("_uid", uidGetter, READ_ONLY_SETTER),
    asyncAttr("id", idGetter, READ_ONLY_SETTER, idGetterAsync),
    asyncAttr("description", descriptionGetter, READ_ONLY_SETTER, descriptionGetterAsync),
    asyncAttr("size", sizeGetter, READ_ONLY_SETTER, sizeGetterAsync),
    attr("overviews", overviewsGetter, READ_ONLY_SETTER),
    attr("pixels", pixelsGetter, READ_ONLY_SETTER),
    asyncAttr("blockSize", blockSizeGetter, READ_ONLY_SETTER, blockSizeGetterAsync),
    asyncAttr("minimum", minimumGetter, READ_ONLY_SETTER, minimumGetterAsync),
    asyncAttr("maximum", maximumGetter, READ_ONLY_SETTER, maximumGetterAsync),
    asyncAttr("readOnly", readOnlyGetter, READ_ONLY_SETTER, readOnlyGetterAsync),
    asyncAttr("dataType", dataTypeGetter, READ_ONLY_SETTER, dataTypeGetterAsync),
    asyncAttr("hasArbitraryOverviews", hasArbitraryOverviewsGetter, READ_ONLY_SETTER, hasArbitraryOverviewsGetterAsync),
    asyncAttr("unitType", unitTypeGetter, unitTypeSetter, unitTypeGetterAsync),
    asyncAttr("scale", scaleGetter, scaleSetter, scaleGetterAsync),
    asyncAttr("offset", offsetGetter, offsetSetter, offsetGetterAsync),
    asyncAttr("noDataValue", noDataValueGetter, noDataValueSetter, noDataValueGetterAsync),
    asyncAttr("categoryNames", categoryNamesGetter, categoryNamesSetter, categoryNamesGetterAsync),
    asyncAttr("colorInterpretation", colorInterpretationGetter, colorInterpretationSetter, colorInterpretationGetterAsync),
    asyncAttr("colorTable", colorTableGetter, colorTableSetter, colorTableGetterAsync),
  };

  Napi::Function lcons = GDALDefineClass<RasterBand>(env, "RasterBand", methods, accessors);

  target.Set("RasterBand", lcons);

  constructor() = Napi::Persistent(lcons);
}


} // namespace node_gdal
