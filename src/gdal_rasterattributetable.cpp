#include <memory>
#include <vector>

#include "gdal_rasterattributetable.hpp"
#include "gdal_common.hpp"
#include "gdal_dataset.hpp"
#include "gdal_rasterband.hpp"
#include "utils/args.hpp"

namespace node_gdal {

namespace {

// A RAT retrieved from a band is owned by the band; the ones created by JS or by
// a factory without a parent belong to the wrapper
void free_rat(void *ptr) {
  delete static_cast<GDALRasterAttributeTable *>(ptr);
}

const char *fieldTypeName(GDALRATFieldType type) {
  switch (type) {
    case GFT_Integer: return "Integer";
    case GFT_Real: return "Real";
    case GFT_String: return "String";
#if GDAL_VERSION_MAJOR > 3 || (GDAL_VERSION_MAJOR == 3 && GDAL_VERSION_MINOR >= 12)
    case GFT_Boolean: return "Boolean";
    case GFT_DateTime: return "DateTime";
    case GFT_WKBGeometry: return "WKBGeometry";
#endif
    default: return "Unknown";
  }
}

bool parseFieldType(const std::string &name, GDALRATFieldType &out) {
  if (name == "Integer") {
    out = GFT_Integer;
  } else if (name == "Real") {
    out = GFT_Real;
  } else if (name == "String") {
    out = GFT_String;
#if GDAL_VERSION_MAJOR > 3 || (GDAL_VERSION_MAJOR == 3 && GDAL_VERSION_MINOR >= 12)
  } else if (name == "Boolean") {
    out = GFT_Boolean;
  } else if (name == "DateTime") {
    out = GFT_DateTime;
  } else if (name == "WKBGeometry") {
    out = GFT_WKBGeometry;
#endif
  } else {
    return false;
  }
  return true;
}

const char *fieldUsageName(GDALRATFieldUsage usage) {
  switch (usage) {
    case GFU_Generic: return "Generic";
    case GFU_PixelCount: return "PixelCount";
    case GFU_Name: return "Name";
    case GFU_Min: return "Min";
    case GFU_Max: return "Max";
    case GFU_MinMax: return "MinMax";
    case GFU_Red: return "Red";
    case GFU_Green: return "Green";
    case GFU_Blue: return "Blue";
    case GFU_Alpha: return "Alpha";
    case GFU_RedMin: return "RedMin";
    case GFU_GreenMin: return "GreenMin";
    case GFU_BlueMin: return "BlueMin";
    case GFU_AlphaMin: return "AlphaMin";
    case GFU_RedMax: return "RedMax";
    case GFU_GreenMax: return "GreenMax";
    case GFU_BlueMax: return "BlueMax";
    case GFU_AlphaMax: return "AlphaMax";
    case GFU_MaxCount: return "MaxCount";
    default: return "Generic";
  }
}

bool parseFieldUsage(const std::string &name, GDALRATFieldUsage &out) {
  static const struct {
    const char *name;
    GDALRATFieldUsage usage;
  } usages[] = {
    {"Generic", GFU_Generic}, {"PixelCount", GFU_PixelCount}, {"Name", GFU_Name},         {"Min", GFU_Min},
    {"Max", GFU_Max},         {"MinMax", GFU_MinMax},         {"Red", GFU_Red},           {"Green", GFU_Green},
    {"Blue", GFU_Blue},       {"Alpha", GFU_Alpha},           {"RedMin", GFU_RedMin},     {"GreenMin", GFU_GreenMin},
    {"BlueMin", GFU_BlueMin}, {"AlphaMin", GFU_AlphaMin},     {"RedMax", GFU_RedMax},     {"GreenMax", GFU_GreenMax},
    {"BlueMax", GFU_BlueMax}, {"AlphaMax", GFU_AlphaMax},     {"MaxCount", GFU_MaxCount},
  };
  for (const auto &u : usages) {
    if (name == u.name) {
      out = u.usage;
      return true;
    }
  }
  return false;
}

} // namespace

Napi::FunctionReference &RasterAttributeTable::constructor() {
  return node_gdal::env_context().constructor<RasterAttributeTable>();
}

RasterAttributeTable::RasterAttributeTable(const Napi::CallbackInfo &info)
  : GDALObject<RasterAttributeTable>(info), parent_uid(0) {
  Handle<GDALRasterAttributeTable> pending = node_gdal::TakePendingHandle<GDALRasterAttributeTable>();
  if (pending) {
    this_ = std::move(pending);
    // the factory passes the owning dataset's uid as the first argument
    if (info.Length() > 0 && info[0].IsNumber()) parent_uid = info[0].As<Napi::Number>().Int64Value();
  } else {
    // Constructed from JS: a standalone, empty table
    if (info.Length() > 0 && !info[0].IsUndefined() && !info[0].IsNull()) {
      Napi::Error::New(
        info.Env(), "RasterAttributeTable cannot be created from JavaScript, use RasterBand.rasterAttributeTable")
        .ThrowAsJavaScriptException();
      return;
    }
    this_ = Handle<GDALRasterAttributeTable>::owned(new GDALDefaultRasterAttributeTable(), &free_rat);
  }

  uid = object_store().add(this_.get(), *this, parent_uid);
}

RasterAttributeTable::~RasterAttributeTable() {
  dispose();
}

void RasterAttributeTable::dispose() {
  if (this_) {
    LOG("Disposing RasterAttributeTable [%p]", this_.get());
    object_store().dispose(uid, false);
    // The handle deletes the table, unless the band it came from owns it
    this_.reset();
  }
}

/**
 * A raster attribute table: a table of columns, one per row of a
 * {@link RasterBand} with a thematic classification.
 *
 * @example
 * const rat = band.rasterAttributeTable;
 * console.log(rat.rowCount, rat.columnCount);
 *
 * @class RasterAttributeTable
 */
Napi::Value RasterAttributeTable::New(GDALRasterAttributeTable *rat, Napi::Value parent) {
  if (!rat) { return node_gdal::napi_env().Null(); }
  if (object_store().has(rat)) { return object_store().get(rat); }

  RasterBand *band = node_gdal::UnwrapWrapped<RasterBand>(parent.As<Napi::Object>());

  node_gdal::detail::PendingObjectGuard pending(rat);
  std::vector<napi_value> args = {Napi::Number::New(node_gdal::napi_env(), band->parent_uid)};
  Napi::Object obj = RasterAttributeTable::constructor().Value().New(args);
  node_gdal::set_private(obj, "parent_", parent);
  // The table belongs to the band: writing to the table handed out here would
  // change the table of the band behind its back
  node_gdal::set_read_only(obj);

  return obj;
}

Napi::Value RasterAttributeTable::New(GDALRasterAttributeTable *rat) {
  if (!rat) { return node_gdal::napi_env().Null(); }
  if (object_store().has(rat)) { return object_store().get(rat); }

  node_gdal::detail::PendingObjectGuard pending(rat, &free_rat);
  std::vector<napi_value> args = {Napi::Number::New(node_gdal::napi_env(), 0)};
  return RasterAttributeTable::constructor().Value().New(args);
}

/**
 * Returns the name of the {@link RasterBand} this table belongs to.
 *
 * @readonly
 * @kind member
 * @name band
 * @instance
 * @memberof RasterAttributeTable
 * @type {RasterBand}
 */
static NAPI_GETTER(bandGetter) {
  RasterAttributeTable *rat = node_gdal::unwrap_or_throw<RasterAttributeTable>(info.This(), "RasterAttributeTable");
  if (!rat) { return info.Env().Undefined(); }
  return node_gdal::get_private(info.This(), "parent_");
}

/**
 * The number of rows in the table.
 *
 * @readonly
 * @kind member
 * @name rowCount
 * @instance
 * @memberof RasterAttributeTable
 * @type {number}
 */
static NAPI_GETTER(rowCountGetter) {
  RasterAttributeTable *rat = node_gdal::unwrap_or_throw<RasterAttributeTable>(info.This(), "RasterAttributeTable");
  if (!rat) { return info.Env().Undefined(); }
  GDAL_LOCK_PARENT(rat);
  return Napi::Number::New(info.Env(), rat->get()->GetRowCount());
}

/**
 * The number of columns in the table.
 *
 * @readonly
 * @kind member
 * @name columnCount
 * @instance
 * @memberof RasterAttributeTable
 * @type {number}
 */
static NAPI_GETTER(columnCountGetter) {
  RasterAttributeTable *rat = node_gdal::unwrap_or_throw<RasterAttributeTable>(info.This(), "RasterAttributeTable");
  if (!rat) { return info.Env().Undefined(); }
  GDAL_LOCK_PARENT(rat);
  return Napi::Number::New(info.Env(), rat->get()->GetColumnCount());
}

/**
 * The names of the columns of the table, in order.
 *
 * @readonly
 * @kind member
 * @name columnNames
 * @instance
 * @memberof RasterAttributeTable
 * @type {string[]}
 */
static NAPI_GETTER(columnNamesGetter) {
  RasterAttributeTable *rat = node_gdal::unwrap_or_throw<RasterAttributeTable>(info.This(), "RasterAttributeTable");
  if (!rat) { return info.Env().Undefined(); }
  GDAL_LOCK_PARENT(rat);
  GDALRasterAttributeTable *raw = rat->get();
  int count = raw->GetColumnCount();
  Napi::Array result = Napi::Array::New(info.Env(), count);
  for (int i = 0; i < count; i++) { result.Set(i, SafeString::New(raw->GetNameOfCol(i))); }
  return result;
}

/**
 * The types of the columns of the table, in order. Each one is one of
 * `Integer`, `Real`, `String`, `Boolean`, `DateTime` or `WKBGeometry`.
 *
 * @readonly
 * @kind member
 * @name columnTypes
 * @instance
 * @memberof RasterAttributeTable
 * @type {string[]}
 */
static NAPI_GETTER(columnTypesGetter) {
  RasterAttributeTable *rat = node_gdal::unwrap_or_throw<RasterAttributeTable>(info.This(), "RasterAttributeTable");
  if (!rat) { return info.Env().Undefined(); }
  GDAL_LOCK_PARENT(rat);
  GDALRasterAttributeTable *raw = rat->get();
  int count = raw->GetColumnCount();
  Napi::Array result = Napi::Array::New(info.Env(), count);
  for (int i = 0; i < count; i++) { result.Set(i, SafeString::New(fieldTypeName(raw->GetTypeOfCol(i)))); }
  return result;
}

/**
 * The usages of the columns of the table, in order.
 *
 * @readonly
 * @kind member
 * @name columnUsages
 * @instance
 * @memberof RasterAttributeTable
 * @type {string[]}
 */
static NAPI_GETTER(columnUsagesGetter) {
  RasterAttributeTable *rat = node_gdal::unwrap_or_throw<RasterAttributeTable>(info.This(), "RasterAttributeTable");
  if (!rat) { return info.Env().Undefined(); }
  GDAL_LOCK_PARENT(rat);
  GDALRasterAttributeTable *raw = rat->get();
  int count = raw->GetColumnCount();
  Napi::Array result = Napi::Array::New(info.Env(), count);
  for (int i = 0; i < count; i++) { result.Set(i, SafeString::New(fieldUsageName(raw->GetUsageOfCol(i)))); }
  return result;
}

/**
 * Whether the changes made to this table are written to the dataset directly.
 * When this is `false`, the table has to be assigned back to the band with
 * `RasterBand.rasterAttributeTable` for the changes to be kept.
 *
 * @readonly
 * @kind member
 * @name changesAreWrittenToFile
 * @instance
 * @memberof RasterAttributeTable
 * @type {boolean}
 */
static NAPI_GETTER(changesAreWrittenToFileGetter) {
  RasterAttributeTable *rat = node_gdal::unwrap_or_throw<RasterAttributeTable>(info.This(), "RasterAttributeTable");
  if (!rat) { return info.Env().Undefined(); }
  GDAL_LOCK_PARENT(rat);
  return Napi::Boolean::New(info.Env(), rat->get()->ChangesAreWrittenToFile() != 0);
}

/**
 * The type of the table: `Thematic` or `Athematic`.
 *
 * @kind member
 * @name tableType
 * @instance
 * @memberof RasterAttributeTable
 * @type {string}
 */
static NAPI_GETTER(tableTypeGetter) {
  RasterAttributeTable *rat = node_gdal::unwrap_or_throw<RasterAttributeTable>(info.This(), "RasterAttributeTable");
  if (!rat) { return info.Env().Undefined(); }
  GDAL_LOCK_PARENT(rat);
  return SafeString::New(rat->get()->GetTableType() == GRTT_ATHEMATIC ? "Athematic" : "Thematic");
}

static void tableTypeSetter(const Napi::CallbackInfo &info, const Napi::Value &value) {
  RasterAttributeTable *rat = node_gdal::unwrap_or_throw<RasterAttributeTable>(info.This(), "RasterAttributeTable");
  if (!rat) { return; }
  if (node_gdal::is_read_only(info.This())) {
    node_gdal::throw_read_only(info.Env(), "A RasterAttributeTable retrieved from a RasterBand");
    return;
  }
  if (!value.IsString()) {
    Napi::Error::New(info.Env(), "Table type must be a string").ThrowAsJavaScriptException();
    return;
  }
  std::string type = value.As<Napi::String>().Utf8Value();
  GDALRATTableType gdal_type;
  if (type == "Thematic") {
    gdal_type = GRTT_THEMATIC;
  } else if (type == "Athematic") {
    gdal_type = GRTT_ATHEMATIC;
  } else {
    Napi::RangeError::New(info.Env(), "Table type must be either Thematic or Athematic").ThrowAsJavaScriptException();
    return;
  }
  GDAL_LOCK_PARENT_VOID(rat);
  CPLErrorReset();
  if (rat->get()->SetTableType(gdal_type) != CE_None) { throw GDALError::fromGDAL(); }
}

/**
 * Returns the index of the first column with the given usage, or -1 when there
 * is none.
 *
 * @method getColumnOfUsage
 * @instance
 * @memberof RasterAttributeTable
 * @param {string} usage
 * @return {number}
 */
static Napi::Value getColumnOfUsage(const Napi::CallbackInfo &info) {
  RasterAttributeTable *rat = node_gdal::unwrap_or_throw<RasterAttributeTable>(info.This(), "RasterAttributeTable");
  if (!rat) { return info.Env().Undefined(); }
  Args args(info);
  std::string usage_name;
  if (!args.get(0, "usage", usage_name)) { return args.failed(); }
  GDALRATFieldUsage usage;
  if (!parseFieldUsage(usage_name, usage)) {
    Napi::RangeError::New(info.Env(), "Invalid column usage: " + usage_name).ThrowAsJavaScriptException();
    return info.Env().Undefined();
  }
  GDAL_LOCK_PARENT(rat);
  return Napi::Number::New(info.Env(), rat->get()->GetColOfUsage(usage));
}

/**
 * Returns the index of the row which corresponds to the given value of the
 * column which carries the `Min` usage, or -1 when there is none.
 *
 * @method getRowOfValue
 * @instance
 * @memberof RasterAttributeTable
 * @param {number} value
 * @return {number}
 */
static Napi::Value getRowOfValue(const Napi::CallbackInfo &info) {
  RasterAttributeTable *rat = node_gdal::unwrap_or_throw<RasterAttributeTable>(info.This(), "RasterAttributeTable");
  if (!rat) { return info.Env().Undefined(); }
  Args args(info);
  double value;
  if (!args.get(0, "value", value)) { return args.failed(); }
  GDAL_LOCK_PARENT(rat);
  return Napi::Number::New(info.Env(), rat->get()->GetRowOfValue(value));
}

/**
 * Resizes the table to the given number of rows.
 *
 * @method setRowCount
 * @instance
 * @memberof RasterAttributeTable
 * @throws {Error}
 * @param {number} count
 */
static Napi::Value setRowCount(const Napi::CallbackInfo &info) {
  RasterAttributeTable *rat = node_gdal::unwrap_or_throw<RasterAttributeTable>(info.This(), "RasterAttributeTable");
  if (!rat) { return info.Env().Undefined(); }

  if (node_gdal::is_read_only(info.This())) {
    node_gdal::throw_read_only(info.Env(), "A RasterAttributeTable retrieved from a RasterBand");
    return info.Env().Undefined();
  }
  Args args(info);
  int count;
  if (!args.get(0, "count", count)) { return args.failed(); }
  if (count < 0) {
    Napi::RangeError::New(info.Env(), "count must not be negative").ThrowAsJavaScriptException();
    return info.Env().Undefined();
  }
  GDAL_LOCK_PARENT(rat);
  rat->get()->SetRowCount(count);
  return info.Env().Undefined();
}

/**
 * Creates a new column in the table.
 *
 * @method createColumn
 * @instance
 * @memberof RasterAttributeTable
 * @throws {Error}
 * @param {string} name
 * @param {string} type One of `Integer`, `Real`, `String`, `Boolean`,
 * `DateTime` or `WKBGeometry`
 * @param {string} [usage=Generic] One of `Generic`, `PixelCount`, `Name`,
 * `Min`, `Max`, `MinMax`, `Red`, `Green`, `Blue`, `Alpha`, `RedMin`,
 * `GreenMin`, `BlueMin`, `AlphaMin`, `RedMax`, `GreenMax`, `BlueMax`,
 * `AlphaMax` or `MaxCount`
 * @return {number} The index of the new column
 */
static Napi::Value createColumn(const Napi::CallbackInfo &info) {
  RasterAttributeTable *rat = node_gdal::unwrap_or_throw<RasterAttributeTable>(info.This(), "RasterAttributeTable");
  if (!rat) { return info.Env().Undefined(); }

  if (node_gdal::is_read_only(info.This())) {
    node_gdal::throw_read_only(info.Env(), "A RasterAttributeTable retrieved from a RasterBand");
    return info.Env().Undefined();
  }
  Args args(info);
  std::string name, type_name, usage_name = "Generic";
  if (!args.get(0, "name", name)) { return args.failed(); }
  if (!args.get(1, "type", type_name)) { return args.failed(); }
  if (!args.optional(2, "usage", usage_name)) { return args.failed(); }

  GDALRATFieldType type;
  if (!parseFieldType(type_name, type)) {
    Napi::RangeError::New(info.Env(), "Invalid column type: " + type_name).ThrowAsJavaScriptException();
    return info.Env().Undefined();
  }
  GDALRATFieldUsage usage;
  if (!parseFieldUsage(usage_name, usage)) {
    Napi::RangeError::New(info.Env(), "Invalid column usage: " + usage_name).ThrowAsJavaScriptException();
    return info.Env().Undefined();
  }

  GDAL_LOCK_PARENT(rat);
  GDALRasterAttributeTable *raw = rat->get();
  int index = raw->GetColumnCount();
  CPLErrorReset();
  if (raw->CreateColumn(name.c_str(), type, usage) != CE_None) { throw GDALError::fromGDAL(); }
  return Napi::Number::New(info.Env(), index);
}

/**
 * Reads the values of one column.
 *
 * @method getValues
 * @instance
 * @memberof RasterAttributeTable
 * @throws {Error}
 * @param {number} column The index of the column
 * @param {number} [start=0] The first row to read
 * @param {number} [length] The number of rows to read, the rest of the table
 * by default
 * @return {number[]|string[]} An array of numbers for the numeric columns and
 * an array of strings for the string ones
 */
static Napi::Value getValues(const Napi::CallbackInfo &info) {
  RasterAttributeTable *rat = node_gdal::unwrap_or_throw<RasterAttributeTable>(info.This(), "RasterAttributeTable");
  if (!rat) { return info.Env().Undefined(); }
  Args args(info);
  int column, start = 0, length = 0;
  if (!args.get(0, "column", column)) { return args.failed(); }
  if (!args.optional(1, "start", start)) { return args.failed(); }
  if (!args.optional(2, "length", length)) { return args.failed(); }

  GDAL_LOCK_PARENT(rat);
  GDALRasterAttributeTable *raw = rat->get();
  int rows = raw->GetRowCount();
  if (column < 0 || column >= raw->GetColumnCount()) {
    Napi::RangeError::New(info.Env(), "column must be a valid column index").ThrowAsJavaScriptException();
    return info.Env().Undefined();
  }
  if (start < 0 || start > rows) {
    Napi::RangeError::New(info.Env(), "start must be a valid row index").ThrowAsJavaScriptException();
    return info.Env().Undefined();
  }
  if (length == 0) { length = rows - start; }
  if (length < 0 || start + length > rows) {
    Napi::RangeError::New(info.Env(), "length must fit in the table").ThrowAsJavaScriptException();
    return info.Env().Undefined();
  }

  GDALRATFieldType type = raw->GetTypeOfCol(column);
  Napi::Array result = Napi::Array::New(info.Env(), length);
  for (int i = 0; i < length; i++) {
    int row = start + i;
    switch (type) {
      case GFT_Real: result.Set(i, Napi::Number::New(info.Env(), raw->GetValueAsDouble(row, column))); break;
      case GFT_String: result.Set(i, SafeString::New(raw->GetValueAsString(row, column))); break;
#if GDAL_VERSION_MAJOR > 3 || (GDAL_VERSION_MAJOR == 3 && GDAL_VERSION_MINOR >= 12)
      case GFT_Boolean: result.Set(i, Napi::Boolean::New(info.Env(), raw->GetValueAsBoolean(row, column))); break;
#endif
      default: result.Set(i, Napi::Number::New(info.Env(), raw->GetValueAsInt(row, column))); break;
    }
  }
  return result;
}

/**
 * Writes the values of one column.
 *
 * @method setValues
 * @instance
 * @memberof RasterAttributeTable
 * @throws {Error}
 * @param {number} column The index of the column
 * @param {number[]|string[]} values
 * @param {number} [start=0] The first row to write
 */
static Napi::Value setValues(const Napi::CallbackInfo &info) {
  RasterAttributeTable *rat = node_gdal::unwrap_or_throw<RasterAttributeTable>(info.This(), "RasterAttributeTable");
  if (!rat) { return info.Env().Undefined(); }

  if (node_gdal::is_read_only(info.This())) {
    node_gdal::throw_read_only(info.Env(), "A RasterAttributeTable retrieved from a RasterBand");
    return info.Env().Undefined();
  }
  Args args(info);
  int column, start = 0;
  Napi::Array values;
  if (!args.get(0, "column", column)) { return args.failed(); }
  if (!args.get(1, "values", values)) { return args.failed(); }
  if (!args.optional(2, "start", start)) { return args.failed(); }

  GDAL_LOCK_PARENT(rat);
  GDALRasterAttributeTable *raw = rat->get();
  if (column < 0 || column >= raw->GetColumnCount()) {
    Napi::RangeError::New(info.Env(), "column must be a valid column index").ThrowAsJavaScriptException();
    return info.Env().Undefined();
  }
  int rows = raw->GetRowCount();
  int length = (int)values.Length();
  if (start < 0 || start + length > rows) {
    Napi::RangeError::New(info.Env(), "values must fit in the table").ThrowAsJavaScriptException();
    return info.Env().Undefined();
  }

  GDALRATFieldType type = raw->GetTypeOfCol(column);
  CPLErrorReset();
  for (int i = 0; i < length; i++) {
    Napi::Value value = values.Get(i);
    int row = start + i;
    CPLErr err;
    if (type == GFT_String) {
      if (!value.IsString()) {
        Napi::TypeError::New(info.Env(), "String columns must be assigned strings").ThrowAsJavaScriptException();
        return info.Env().Undefined();
      }
      err = raw->SetValue(row, column, value.As<Napi::String>().Utf8Value().c_str());
    } else if (type == GFT_Real) {
      if (!value.IsNumber()) {
        Napi::TypeError::New(info.Env(), "Real columns must be assigned numbers").ThrowAsJavaScriptException();
        return info.Env().Undefined();
      }
      err = raw->SetValue(row, column, value.As<Napi::Number>().DoubleValue());
    } else {
      if (!value.IsNumber()) {
        Napi::TypeError::New(info.Env(), "Integer columns must be assigned numbers").ThrowAsJavaScriptException();
        return info.Env().Undefined();
      }
      err = raw->SetValue(row, column, value.As<Napi::Number>().Int32Value());
    }
    if (err != CE_None) { throw GDALError::fromGDAL(); }
  }
  return info.Env().Undefined();
}

/**
 * Returns a copy of the table which is not owned by any {@link RasterBand}.
 *
 * @method clone
 * @instance
 * @memberof RasterAttributeTable
 * @return {RasterAttributeTable}
 */
static Napi::Value clone(const Napi::CallbackInfo &info) {
  RasterAttributeTable *rat = node_gdal::unwrap_or_throw<RasterAttributeTable>(info.This(), "RasterAttributeTable");
  if (!rat) { return info.Env().Undefined(); }
  GDAL_LOCK_PARENT(rat);
  CPLErrorReset();
  GDALRasterAttributeTable *copy = rat->get()->Clone();
  if (!copy) {
    Napi::Error::New(info.Env(), CPLGetLastErrorMsg()).ThrowAsJavaScriptException();
    return info.Env().Undefined();
  }
  return RasterAttributeTable::New(copy);
}

Napi::Value toString(const Napi::CallbackInfo &info) {
  return Napi::String::New(node_gdal::napi_env(), "RasterAttributeTable");
}

void RasterAttributeTable::Initialize(Napi::Object target) {
  Napi::Env env = target.Env();
  static constexpr MethodEntry methods[] = {
    method("toString", toString),
    method("getColumnOfUsage", getColumnOfUsage),
    method("getRowOfValue", getRowOfValue),
    method("setRowCount", setRowCount),
    method("createColumn", createColumn),
    method("getValues", getValues),
    method("setValues", setValues),
    method("clone", clone),
  };

  static constexpr AccessorEntry accessors[] = {
    hiddenAttr("band", bandGetter, READ_ONLY_SETTER),
    attr("rowCount", rowCountGetter, READ_ONLY_SETTER),
    attr("columnCount", columnCountGetter, READ_ONLY_SETTER),
    attr("columnNames", columnNamesGetter, READ_ONLY_SETTER),
    attr("columnTypes", columnTypesGetter, READ_ONLY_SETTER),
    attr("columnUsages", columnUsagesGetter, READ_ONLY_SETTER),
    attr("changesAreWrittenToFile", changesAreWrittenToFileGetter, READ_ONLY_SETTER),
    attr("tableType", tableTypeGetter, tableTypeSetter),
  };

  Napi::Function lcons = GDALDefineClass<RasterAttributeTable>(env, "RasterAttributeTable", methods, accessors);

  target.Set("RasterAttributeTable", lcons);

  constructor() = Napi::Persistent(lcons);
}

} // namespace node_gdal
