#include "gdal_mdarray.hpp"
#include "utils/args.hpp"
#include "gdal_group.hpp"
#include "gdal_common.hpp"
#include "gdal_driver.hpp"
#include "collections/array_dimensions.hpp"
#include "collections/array_attributes.hpp"
#include "geometry/gdal_geometry.hpp"
#include "gdal_layer.hpp"
#include "gdal_majorobject.hpp"
#include "gdal_spatial_reference.hpp"
#include "utils/typed_array.hpp"

namespace node_gdal {

#if GDAL_VERSION_MAJOR > 3 || (GDAL_VERSION_MAJOR == 3 && GDAL_VERSION_MINOR >= 1)

Napi::FunctionReference &MDArray::constructor() {
  return node_gdal::env_context().constructor<MDArray>();
}

MDArray::MDArray(const Napi::CallbackInfo &info) : GDALObject<MDArray>(info), uid(0), this_(nullptr) {
  std::shared_ptr<GDALMDArray> pending = node_gdal::TakePendingShared<GDALMDArray>();
  if (pending) {
    this_ = pending;
    LOG("Created MDArray [%p]", this_.get());
  } else {
    Napi::Error::New(info.Env(), "Cannot create MDArray directly").ThrowAsJavaScriptException();
    return;
  }

  Napi::Value parent_ds = info.Length() > 0 ? info[0] : info.Env().Undefined();
  node_gdal::set_private(info.This(), "dims_", ArrayDimensions::New(info.This(), parent_ds));
  node_gdal::set_private(info.This(), "attrs_", ArrayAttributes::New(info.This(), parent_ds));
}

MDArray::~MDArray() {
  dispose();
}

void MDArray::dispose() {
  if (this_) {

    LOG("Disposing array [%p]", this_.get());

    object_store().dispose(uid);

    LOG("Disposed array [%p]", this_.get());
  }
};

/**
 * A representation of an array with access methods.
 *
 * @class MDArray
 */

Napi::Value MDArray::New(std::shared_ptr<GDALMDArray> raw, GDALDataset *parent_ds) {

  if (!raw) { return node_gdal::napi_env().Null(); }
  if (object_store().has(raw)) { return object_store().get(raw); }

  // add reference to datasource so datasource doesnt get GC'ed while group is
  // alive
  Napi::Object ds;
  if (object_store().has(parent_ds)) {
    ds = object_store().get(parent_ds);
  } else {
    LOG("MDArray's parent dataset disappeared from cache (array = %p, dataset = %p)", raw.get(), parent_ds);
    Napi::Error::New(node_gdal::napi_env(), "MDArray's parent dataset disappeared from cache").ThrowAsJavaScriptException();
    return node_gdal::napi_env().Undefined();
  }

  node_gdal::detail::PendingObjectGuard pending(node_gdal::ExportShared(raw));
  std::vector<napi_value> args = {ds};
  Napi::Object obj = MDArray::constructor().Value().New(args);
  MDArray *wrapped = node_gdal::UnwrapWrapped<MDArray>(obj);

  size_t dim = raw->GetDimensionCount();

  Dataset *unwrapped_ds = node_gdal::UnwrapWrapped<Dataset>(ds);
  long parent_uid = unwrapped_ds->uid;

  wrapped->uid = object_store().add(raw, *wrapped, parent_uid);
  wrapped->parent_ds = parent_ds;
  wrapped->parent_uid = parent_uid;
  wrapped->dimensions = dim;


  return obj;
}

/* Find the lowest possible element index for the given spans and strides */
static inline int
findLowest(int dimensions, std::shared_ptr<size_t[]> span, std::shared_ptr<GPtrDiff_t[]> stride, GPtrDiff_t offset) {
  GPtrDiff_t dimStride = 1;
  GPtrDiff_t lowest = 0;
  for (int dim = 0; dim < dimensions; dim++) {
    if (stride != nullptr) {
      // strides are given
      dimStride = stride.get()[dim];
    } else {
      // default strides: 1 on the first dimension -or- (size of the current dimensions) x (stride of the previous dimension)
      if (dim > 0) dimStride = dimStride * span.get()[dim - 1];
    }

    // Lowest address element on this dimension
    size_t element;
    if (dimStride < 0)
      element = span.get()[dim] - 1;
    else
      element = 0;

    lowest += element * dimStride;
  }

  return offset + lowest;
}

/* Find the highest possible element index for the given spans and strides */
static inline int
findHighest(int dimensions, std::shared_ptr<size_t[]> span, std::shared_ptr<GPtrDiff_t[]> stride, GPtrDiff_t offset) {
  GPtrDiff_t dimStride = 1;
  GPtrDiff_t highest = 0;
  for (int dim = 0; dim < dimensions; dim++) {
    if (stride != nullptr) {
      // strides are given
      dimStride = stride.get()[dim];
    } else {
      // default strides: 1 on the first dimension -or- (size of the current dimensions) x (stride of the previous dimension)
      if (dim > 0) dimStride = dimStride * span.get()[dim - 1];
    }

    // Highest address element on this dimension
    size_t element;
    if (dimStride > 0)
      element = span.get()[dim] - 1;
    else
      element = 0;

    highest += element * dimStride;
  }

  return offset + highest;
}

/**
 * @typedef {object} MDArrayOptions<T extends TypedArray<number> | TypedArray<bigint> = TypedArray<number>>
 * @property {number[]} origin
 * @property {number[]} span
 * @property {number[]} [stride]
 * @property {string} [data_type]
 * @property {T} [data]
 * @property {number} [_offset]
 * @property {AbortSignal} [abort_signal] AbortSignal to interrupt the operation
 */

/**
 * Read data from the MDArray.
 *
 * This will extract the context of a (hyper-)rectangle from the array into a buffer.
 * If the buffer can be passed as an argument or it can be allocated by the function.
 * Generalized n-dimensional strides are supported.
 *
 * Although this method can be used in its raw form, it works best when used with the ndarray plugin.
 *
 * @method read<T extends TypedArray<number> | TypedArray<bigint> = TypedArray<number>>
 * @instance
 * @memberof MDArray
 * @throws {Error}
 * @param {MDArrayOptions<T>} options
 * @param {number[]} options.origin An array of the starting indices
 * @param {number[]} options.span An array specifying the number of elements to read in each dimension
 * @param {number[]} [options.stride] An array of strides for the output array, mandatory if the array is specified
 * @param {string} [options.data_type] See {@link GDT|GDT constants}
 * @param {T} [options.data] The `TypedArray` to put the data in. A new array is created if not given.
 * @param {AbortSignal} [options.abort_signal] AbortSignal to interrupt the operation
 * @return {T}
 */

/**
 * Read data from the MDArray.
 * @async
 *
 * This will extract the context of a (hyper-)rectangle from the array into a buffer.
 * If the buffer can be passed as an argument or it can be allocated by the function.
 * Generalized n-dimensional strides are supported.
 *
 * Although this method can be used in its raw form, it works best when used with the ndarray plugin.
 *
 * @method readAsync<T extends TypedArray<number> | TypedArray<bigint> = TypedArray<number>>
 * @instance
 * @memberof MDArray
 * @throws {Error}
 * @param {MDArrayOptions<T>} options
 * @param {number[]} options.origin An array of the starting indices
 * @param {number[]} options.span An array specifying the number of elements to read in each dimension
 * @param {number[]} [options.stride] An array of strides for the output array, mandatory if the array is specified
 * @param {string} [options.data_type] See {@link GDT|GDT constants}
 * @param {T} [options.data] The `TypedArray` to put the data in. A new array is created if not given.
 * @param {AbortSignal} [options.abort_signal] AbortSignal to interrupt the operation
 * @param {callback<T>} [callback=undefined]
 * @return {Promise<T>} A `TypedArray` of values.
 */
namespace {

struct readBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    Args args(info);

    MDArray *self = node_gdal::unwrap_or_throw<MDArray>(info.This(), "MDArray");

    Napi::Object options;
    Napi::Array origin, span, stride;
    std::string type_name;
    GDALDataType type = GDT_Byte;
    GPtrDiff_t offset = 0;

    if (!args.get(0, "options", options)) return args.failed();
    if (!args.from(options, "origin", origin)) return args.failed();
    if (!args.from(options, "span", span)) return args.failed();
    if (!args.from(options, "stride", stride)) return args.failed();
    if (!args.from(options, "data_type", type_name)) return args.failed();
    NODE_INT64_FROM_OBJ_OPT(options, "_offset", offset);
    if (!type_name.empty()) { type = GDALGetDataTypeByName(type_name.c_str()); }

    std::shared_ptr<GUInt64[]> gdal_origin;
    std::shared_ptr<size_t[]> gdal_span;
    std::shared_ptr<GPtrDiff_t[]> gdal_stride;
    try {
      gdal_origin = NumberArrayToSharedPtr<int64_t, GUInt64>(origin, self->dimensions);
      gdal_span = NumberArrayToSharedPtr<int64_t, size_t>(span, self->dimensions);
      gdal_stride = NumberArrayToSharedPtr<int64_t, GPtrDiff_t>(stride, self->dimensions);
    } catch (const char *e) {
      Napi::Error::New(node_gdal::napi_env(), e).ThrowAsJavaScriptException();
      return node_gdal::napi_env().Undefined();
    }
    // A missing property leaves the pointer empty, and every step from here on
    // reads both of them: the length of the read is a function of the span and
    // GDALMDArray::Read() takes the origin as its first argument, so an
    // options object without them crashed the process instead of being
    // refused
    if (gdal_origin == nullptr) {
      Napi::TypeError::New(node_gdal::napi_env(), "origin is required").ThrowAsJavaScriptException();
      return node_gdal::napi_env().Undefined();
    }
    if (gdal_span == nullptr) {
      Napi::TypeError::New(node_gdal::napi_env(), "span is required").ThrowAsJavaScriptException();
      return node_gdal::napi_env().Undefined();
    }
    GPtrDiff_t highest = findHighest(self->dimensions, gdal_span, gdal_stride, offset);
    GPtrDiff_t lowest = findLowest(self->dimensions, gdal_span, gdal_stride, offset);
    size_t length = (highest - (lowest < 0 ? lowest : 0)) + 1;

    Napi::String sym = Napi::String::New(node_gdal::napi_env(), "data");
    Napi::Value data;
    Napi::Object array;
    if (options.As<Napi::Object>().HasOwnProperty(sym)) {
      data = options.As<Napi::Object>().Get(sym);
      if (!data.IsUndefined() && !data.IsNull()) {
        array = data.As<Napi::Object>();
        type = node_gdal::TypedArray::Identify(array);
        if (type == GDT_Unknown) {
          Napi::Error::New(node_gdal::napi_env(), "Invalid array").ThrowAsJavaScriptException();
          return node_gdal::napi_env().Undefined();
        }
      }
    }

    std::shared_ptr<GDALMDArray> gdal_mdarray = node_gdal::raw_or_throw<std::shared_ptr<GDALMDArray>>(self, "std::shared_ptr<GDALMDArray>");

    // create array if no array was passed
    if (data.IsEmpty()) {
      if (type_name.empty()) {
        auto exType = gdal_mdarray->GetDataType();
        if (exType.GetClass() != GEDTC_NUMERIC) {
          Napi::TypeError::New(node_gdal::napi_env(), "Reading of extended data types is not supported yet").ThrowAsJavaScriptException();
          return node_gdal::napi_env().Undefined();
        }
        type = exType.GetNumericDataType();
      }
      data = node_gdal::TypedArray::New(type, length);
      if (data.IsEmpty() || !data.IsObject()) {
        node_gdal::ThrowOverPending(node_gdal::napi_env(), "Failed to allocate array");
        return node_gdal::napi_env().Undefined(); // TypedArray::New threw an error
      }
      array = data.As<Napi::Object>();
    }

    if (lowest < 0) {
      Napi::RangeError::New(node_gdal::napi_env(), "Will have to read before the start of the array").ThrowAsJavaScriptException();
      return node_gdal::napi_env().Undefined();
    }

    void *buffer = node_gdal::TypedArray::Validate(array, type, length);
    if (!buffer) {
      node_gdal::ThrowOverPending(node_gdal::napi_env(), "Failed to allocate array");
      return node_gdal::napi_env().Undefined(); // TypedArray::Validate threw an error
    }

    GDALAsyncableJob<bool> job(self->parent_uid);
    job.persist("array", array);
    // GDALMDArray::Read() takes no progress callback, so an operation which is
    // already aborted can only be refused before it is started
    Napi::Value abort_signal;
    if (!args.from(options, "abort_signal", abort_signal)) return args.failed();
    if (!job.setAbortSignal(abort_signal)) return node_gdal::napi_env().Undefined();

    job.main =
      [buffer, gdal_mdarray, gdal_origin, gdal_span, gdal_stride, type, length, offset](const GDALExecutionProgress &) {
        int bytes_per_pixel = GDALGetDataTypeSizeBytes(type);
        if (bytes_per_pixel == 0) { throw GDALError("Invalid GDAL data type"); }
        CPLErrorReset();
        GDALExtendedDataType gdal_type = GDALExtendedDataType::Create(type);
        bool success = gdal_mdarray->Read(
          gdal_origin.get(),
          gdal_span.get(),
          nullptr,
          gdal_stride.get(),
          gdal_type,
          (void *)((uint8_t *)buffer + offset * bytes_per_pixel),
          buffer,
          length * bytes_per_pixel);
        if (!success) { throw GDALError::fromGDAL(); }
        return success;
      };
    job.rval = [](bool success, const GetFromPersistentFunc &getter) { return getter("array"); };
    return job.run(info, node_gdal::is_async<Async>, 1);
  }
};

} // namespace

GDAL_ASYNC_METHOD(MDArray::read, readBody)

/**
 * Get a partial view of the MDArray.
 *
 * The slice expression uses the same syntax as NumPy basic slicing and indexing. See (https://www.numpy.org/devdocs/reference/arrays.indexing.html#basic-slicing-and-indexing). Or it can use field access by name. See (https://www.numpy.org/devdocs/reference/arrays.indexing.html#field-access).
 *
 * @method getView
 * @instance
 * @memberof MDArray
 * @throws {Error}
 * @param {string} view
 * @return {MDArray}
 */
/**
 * Return an array that is a mask for the current array.
 *
 * This array will be of type Byte, with values set to 0 to indicate invalid pixels of the current array, and values set to 1 to indicate valid pixels.
 *
 * The generic implementation honours the NoDataValue, as well as various netCDF CF attributes: missing_value, _FillValue, valid_min, valid_max and valid_range.
 *
 * @method getMask
 * @instance
 * @memberof MDArray
 * @throws {Error}
 * @return {MDArray}
 */
/**
 * Return a view of this array as a gdal.Dataset (ie 2D)
 *
 * In the case of > 2D arrays, additional dimensions will be represented as raster bands.
 *
 * @method asDataset
 * @instance
 * @memberof MDArray
 * @param {number|string} x dimension to be used as X axis
 * @param {number|string} y dimension to be used as Y axis
 * @throws {Error}
 * @return {Dataset}
 */
/**
 * Spatial reference associated with MDArray.
 *
 * @throws {Error}
 * @kind member
 * @name srs
 * @instance
 * @memberof MDArray
 * @type {SpatialReference|null}
 */
/**
 * Raster value offset.
 *
 * @kind member
 * @name offset
 * @instance
 * @memberof MDArray
 * @type {number}
 */
/**
 * Raster value scale.
 *
 * @kind member
 * @name scale
 * @instance
 * @memberof MDArray
 * @type {number}
 */
/**
 * No data value for this array.
 *
 * @kind member
 * @name noDataValue
 * @instance
 * @memberof MDArray
 * @type {number|null}
 */
/**
 * Raster unit type (name for the units of this raster's values).
 * For instance, it might be `"m"` for an elevation model in meters,
 * or `"ft"` for feet. If no units are available, a value of `""`
 * will be returned.
 *
 * @kind member
 * @name unitType
 * @instance
 * @memberof MDArray
 * @type {string}
 */
/**
 * @readonly
 * @kind member
 * @name dataType
 * @instance
 * @memberof MDArray
 * @type {string}
 */
/**
 * @readonly
 * @kind member
 * @name dimensions
 * @instance
 * @memberof MDArray
 * @type {ArrayDimensions}
 */
/**
 * @readonly
 * @kind member
 * @name attributes
 * @instance
 * @memberof MDArray
 * @type {ArrayAttributes}
 */
/**
 * @readonly
 * @kind member
 * @name description
 * @instance
 * @memberof MDArray
 * @type {string}
 */
/**
 * The flattened length of the array.
 *
 * @readonly
 * @kind member
 * @name length
 * @instance
 * @memberof MDArray
 * @type {number}
 */
namespace {
// MDArray::lengthGetter()
struct lengthGetterBody : node_gdal::SyncMethodBody {
  static Napi::Value run(const Napi::CallbackInfo &info) {
    auto obj = node_gdal::unwrapThis<MDArray>(info, "MDArray");
    if (!obj) { return obj.failure; }
    GDAL_LOCK_PARENT(obj);
    return Number::New(info.Env(), obj->get()->GetTotalElementsCount());
  }
};
} // namespace

GDAL_SYNC_GETTER(MDArray::lengthGetter, lengthGetterBody)

#endif

namespace {

Napi::Value toString(const Napi::CallbackInfo &info) {
  return Napi::String::New(node_gdal::napi_env(), "MDArray");
}


Napi::Value getView(const Napi::CallbackInfo &info) {
  Args args(info);
  MDArray *array = node_gdal::unwrap_or_throw<MDArray>(info.This(), "MDArray");
  std::shared_ptr<GDALMDArray> raw = node_gdal::raw_or_throw<std::shared_ptr<GDALMDArray>>(array, "std::shared_ptr<GDALMDArray>");

  std::string viewExpr;
  if (!args.get(0, "view", viewExpr)) return args.failed();
  GDAL_LOCK_PARENT(array);
  CPLErrorReset();
  std::shared_ptr<GDALMDArray> view = raw->GetView(viewExpr);
  if (view == nullptr) {
    Napi::Error::New(node_gdal::napi_env(), CPLGetLastErrorMsg()).ThrowAsJavaScriptException();
    return node_gdal::napi_env().Undefined();
  }
  Napi::Value obj = MDArray::New(view, array->getParent());
  return obj;
}


Napi::Value getMask(const Napi::CallbackInfo &info) {
  MDArray *array = node_gdal::unwrap_or_throw<MDArray>(info.This(), "MDArray");
  std::shared_ptr<GDALMDArray> raw = node_gdal::raw_or_throw<std::shared_ptr<GDALMDArray>>(array, "std::shared_ptr<GDALMDArray>");

  GDAL_LOCK_PARENT(array);
  CPLErrorReset();
  std::shared_ptr<GDALMDArray> mask = raw->GetMask(nullptr);
  if (mask == nullptr) {
    Napi::Error::New(node_gdal::napi_env(), CPLGetLastErrorMsg()).ThrowAsJavaScriptException();
    return node_gdal::napi_env().Undefined();
  }
  Napi::Value obj = MDArray::New(mask, array->getParent());
  return obj;
}


Napi::Value asDataset(const Napi::CallbackInfo &info) {
  Args args(info);
  MDArray *array = node_gdal::unwrap_or_throw<MDArray>(info.This(), "MDArray");
  std::shared_ptr<GDALMDArray> raw = node_gdal::raw_or_throw<std::shared_ptr<GDALMDArray>>(array, "std::shared_ptr<GDALMDArray>");

  int x = -1, y = -1;
  std::string dim;
  bool isXString = false;
  if (!args.getStringOrInt(0, "x", dim, x, isXString)) return args.failed();
  if (isXString) x = ArrayDimensions::__getIdx(raw, dim);
  bool isYString = false;
  if (!args.getStringOrInt(1, "y", dim, y, isYString)) return args.failed();
  if (isYString) y = ArrayDimensions::__getIdx(raw, dim);

  GDAL_LOCK_PARENT(array);
  CPLErrorReset();
  GDALDataset *ds = raw->AsClassicDataset(x, y);
  if (ds == nullptr) {
    Napi::Error::New(node_gdal::napi_env(), CPLGetLastErrorMsg()).ThrowAsJavaScriptException();
    return node_gdal::napi_env().Undefined();
  }
  Napi::Value obj = Dataset::New(ds, array->getParent());
  return obj;
}


Napi::Value srsGetter(const Napi::CallbackInfo &info) {
  MDArray *array = node_gdal::unwrap_or_throw<MDArray>(info.This(), "MDArray");
  std::shared_ptr<GDALMDArray> raw = node_gdal::raw_or_throw<std::shared_ptr<GDALMDArray>>(array, "std::shared_ptr<GDALMDArray>");
  GDAL_LOCK_PARENT(array);
  std::shared_ptr<OGRSpatialReference> srs = raw->GetSpatialRef();
  if (srs == nullptr) {
    return node_gdal::napi_env().Null();
    return node_gdal::napi_env().Undefined();
  }

  return SpatialReference::New(srs.get(), false);
}


Napi::Value offsetGetter(const Napi::CallbackInfo &info) {
  MDArray *array = node_gdal::unwrap_or_throw<MDArray>(info.This(), "MDArray");
  bool hasOffset = false;
  GDAL_LOCK_PARENT(array);
  double result = array->get()->GetOffset(&hasOffset);
  if (hasOffset)
    return Napi::Number::New(node_gdal::napi_env(), result);
  else
    return Napi::Number::New(node_gdal::napi_env(), 0);
}


Napi::Value scaleGetter(const Napi::CallbackInfo &info) {
  MDArray *array = node_gdal::unwrap_or_throw<MDArray>(info.This(), "MDArray");
  bool hasScale = false;
  GDAL_LOCK_PARENT(array);
  double result = array->get()->GetScale(&hasScale);
  if (hasScale)
    return Napi::Number::New(node_gdal::napi_env(), result);
  else
    return Napi::Number::New(node_gdal::napi_env(), 1);
}


Napi::Value noDataValueGetter(const Napi::CallbackInfo &info) {
  MDArray *array = node_gdal::unwrap_or_throw<MDArray>(info.This(), "MDArray");
  bool hasNoData = false;
  GDAL_LOCK_PARENT(array);
  double result = array->get()->GetNoDataValueAsDouble(&hasNoData);

  if (hasNoData && !std::isnan(result)) {
    return Napi::Number::New(node_gdal::napi_env(), result);
    return node_gdal::napi_env().Undefined();
  } else {
    return node_gdal::napi_env().Null();
    return node_gdal::napi_env().Undefined();
  }
}


Napi::Value unitTypeGetter(const Napi::CallbackInfo &info) {
  MDArray *array = node_gdal::unwrap_or_throw<MDArray>(info.This(), "MDArray");
  GDAL_LOCK_PARENT(array);
  std::string unit = array->get()->GetUnit();
  return SafeString::New(unit.c_str());
}


Napi::Value typeGetter(const Napi::CallbackInfo &info) {
  MDArray *array = node_gdal::unwrap_or_throw<MDArray>(info.This(), "MDArray");
  std::shared_ptr<GDALMDArray> raw = node_gdal::raw_or_throw<std::shared_ptr<GDALMDArray>>(array, "std::shared_ptr<GDALMDArray>");
  GDAL_LOCK_PARENT(array);
  GDALExtendedDataType type = raw->GetDataType();
  const char *r;
  switch (type.GetClass()) {
    case GEDTC_NUMERIC: r = GDALGetDataTypeName(type.GetNumericDataType()); break;
    case GEDTC_STRING: r = "String"; break;
    case GEDTC_COMPOUND: r = "Compound"; break;
    default: Napi::Error::New(node_gdal::napi_env(), "Invalid attribute type").ThrowAsJavaScriptException(); return node_gdal::napi_env().Undefined();
  }
  return SafeString::New(r);
}


Napi::Value dimensionsGetter(const Napi::CallbackInfo &info) {
  node_gdal::requireAlive<MDArray>(info.This(), "MDArray");
  return node_gdal::get_private(info.This(), "dims_");
}


Napi::Value attributesGetter(const Napi::CallbackInfo &info) {
  node_gdal::requireAlive<MDArray>(info.This(), "MDArray");
  return node_gdal::get_private(info.This(), "attrs_");
}


Napi::Value descriptionGetter(const Napi::CallbackInfo &info) {
  MDArray *array = node_gdal::unwrap_or_throw<MDArray>(info.This(), "MDArray");
  std::shared_ptr<GDALMDArray> raw = node_gdal::raw_or_throw<std::shared_ptr<GDALMDArray>>(array, "std::shared_ptr<GDALMDArray>");
  GDAL_LOCK_PARENT(array);
  std::string description = raw->GetFullName();
  return SafeString::New(description.c_str());
}


Napi::Value uidGetter(const Napi::CallbackInfo &info) {
  MDArray *ds = node_gdal::UnwrapWrapped<MDArray>(info.This().As<Napi::Object>());
  return Napi::Number::New(node_gdal::napi_env(), (int)ds->uid);
}

} // namespace

void MDArray::Initialize(Napi::Object target) {
  Napi::Env env = target.Env();
  static constexpr MethodEntry methods[] = {
    method("toString", toString),
    asyncMethod("read", read, readAsync),
    method("getView", getView),
    method("getMask", getMask),
    method("asDataset", asDataset),
  };

  static constexpr AccessorEntry accessors[] = {
    hiddenAttr("_uid", uidGetter, READ_ONLY_SETTER),
    attr("srs", srsGetter, READ_ONLY_SETTER),
    attr("dataType", typeGetter, READ_ONLY_SETTER),
    attr("length", lengthGetter, READ_ONLY_SETTER),
    attr("unitType", unitTypeGetter, READ_ONLY_SETTER),
    attr("scale", scaleGetter, READ_ONLY_SETTER),
    attr("offset", offsetGetter, READ_ONLY_SETTER),
    attr("noDataValue", noDataValueGetter, READ_ONLY_SETTER),
    attr("description", descriptionGetter, READ_ONLY_SETTER),
    attr("dimensions", dimensionsGetter, READ_ONLY_SETTER),
    attr("attributes", attributesGetter, READ_ONLY_SETTER),
  };

  Napi::Function lcons = GDALDefineClass<MDArray>(env, "MDArray", methods, accessors);

  target.Set("MDArray", lcons);

  constructor() = Napi::Persistent(lcons);
}

} // namespace node_gdal
