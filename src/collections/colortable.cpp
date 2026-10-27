#include <memory>
#include "colortable.hpp"
#include "../utils/args.hpp"
#include "../gdal_common.hpp"
#include "../gdal_dataset.hpp"
#include "../gdal_rasterband.hpp"
#include "../utils/string_list.hpp"

namespace node_gdal {

namespace {
// A color table retrieved from a band is owned by the band; the ones created by
// JS or by a factory without a parent belong to the wrapper
void free_colortable(void *ptr) {
  delete static_cast<GDALColorTable *>(ptr);
}
} // namespace

Napi::FunctionReference &ColorTable::constructor() {
  return node_gdal::env_context().constructor<ColorTable>();
}

void ColorTable::Initialize(Napi::Object target) {
  Napi::Env env = target.Env();
  SELF_CLASS(ColorTable);

  // NOTE: the descriptor macros carry their own trailing comma
  Napi::Function lcons = GDALDefineClass<SELF>(env, "ColorTable",
    {
        METHOD(toString)
        METHOD(isSame)
        METHOD(clone)
        METHOD(count)
        METHOD(get)
        METHOD(set)
        METHOD(ramp)
        ATTR(lcons, "interpretation", interpretationGetter, READ_ONLY_SETTER)
        ATTR_DONT_ENUM(lcons, "band", bandGetter, READ_ONLY_SETTER)
    });

  target.Set("ColorTable", lcons);

  constructor() = Napi::Persistent(lcons);
}


ColorTable::ColorTable(const Napi::CallbackInfo &info) : GDALObject<ColorTable>(info), parent_uid(0) {
  Handle<GDALColorTable> pending = node_gdal::TakePendingHandle<GDALColorTable>();
  if (pending) {
    this_ = std::move(pending);
    // the factory passes the owning dataset's uid as the first argument
    if (info.Length() > 0 && info[0].IsNumber()) parent_uid = info[0].As<Napi::Number>().Int64Value();
  } else {
    // Constructed from JS: ColorTable(palette interpretation)
    if (info.Length() < 1 || !info[0].IsString()) {
      Napi::Error::New(info.Env(), "palette interpretation must be given").ThrowAsJavaScriptException();
      return;
    }
    std::string pi = info[0].As<Napi::String>().Utf8Value();
    GDALPaletteInterp gpi;
    if (pi == "Gray")
      gpi = GPI_Gray;
    else if (pi == "RGB")
      gpi = GPI_RGB;
    else if (pi == "CMYK")
      gpi = GPI_CMYK;
    else if (pi == "HLS")
      gpi = GPI_HLS;
    else {
      Napi::RangeError::New(info.Env(), "Invalid palette interpretation").ThrowAsJavaScriptException();
      return;
    }
    this_ = Handle<GDALColorTable>::owned(new GDALColorTable(gpi), &free_colortable);
  }

  uid = object_store().add(this_.get(), *this, parent_uid);
}

ColorTable::~ColorTable() {
  dispose();
}

void ColorTable::dispose() {
  if (this_) {
    LOG("Disposing ColorTable [%p]", this_.get());
    object_store().dispose(uid, false);
    // The handle deletes the table, unless the band it came from owns it
    this_.reset();
  }
}
/**
 * An encapsulation of a {@link RasterBand}
 * color table.
 *
 * @example
 * var colorTable = band.colorTable;
 *
 * band.colorTable = new gdal.ColorTable(gdal.GPI_RGB);
 *
 * @class ColorTable
 * @param {string} interpretation palette interpretation
 */

/*
 * Create a color table owned by a gdal.RasterBand
 */
Napi::Value ColorTable::New(GDALColorTable *raw, Napi::Value parent) {

  if (!raw) { return node_gdal::napi_env().Null(); }
  if (object_store().has(raw)) { return object_store().get(raw); }

  RasterBand *band = node_gdal::UnwrapWrapped<RasterBand>(parent.As<Napi::Object>());

  node_gdal::detail::PendingObjectGuard pending(raw);
  std::vector<napi_value> args = {Napi::Number::New(node_gdal::napi_env(), band->parent_uid)};
  Napi::Object obj = ColorTable::constructor().Value().New(args);
  GDAL_SET_PRIVATE(obj, "parent_", parent);
  // The table belongs to the band: writing to the table handed out here would
  // change the table of the band behind its back
  node_gdal::set_read_only(obj);

  return obj;
}

/*
 * Create a standalone color table.
 */
Napi::Value ColorTable::New(GDALColorTable *raw) {

  if (!raw) { return node_gdal::napi_env().Null(); }
  if (object_store().has(raw)) { return object_store().get(raw); }

  node_gdal::detail::PendingObjectGuard pending(raw, &free_colortable);
  std::vector<napi_value> args = {Napi::Number::New(node_gdal::napi_env(), 0)};
  Napi::Object obj = ColorTable::constructor().Value().New(args);

  return obj;
}

NAPI_METHOD(ColorTable::toString) {
  return Napi::String::New(node_gdal::napi_env(), "ColorTable");
}

/**
 * @typedef {object} Color
 * @memberof ColorTable
 * @property {number} c1
 * @property {number} c2
 * @property {number} c3
 * @property {number} c4
 */

/**
 * Clones the instance.
 * The newly created ColorTable is not owned by any RasterBand.
 *
 * @method clone
 * @instance
 * @memberof ColorTable
 * @return {ColorTable}
 */
NAPI_METHOD(ColorTable::clone) {
  ColorTable *ct = node_gdal::UnwrapWrapped<ColorTable>(info.This().As<Napi::Object>());
  return ColorTable::New(ct->this_->Clone());
}

/**
 * Compares two ColorTable objects for equality.
 *
 * @method isSame
 * @instance
 * @memberof ColorTable
 * @param {ColorTable} other
 * @throws {Error}
 * @return {boolean}
 */
NAPI_METHOD(ColorTable::isSame) {
  Args args(info);

  NODE_UNWRAP_CHECK(ColorTable, info.This(), self);
  GDAL_RAW_CHECK(GDALColorTable *, self, raw);

  ColorTable *other;
  if (!args.getWrapped(0, "other", "ColorTable", other)) return args.failed();
  GDAL_RAW_CHECK(GDALColorTable *, other, raw_other);

  CPLErrorReset();
  return Napi::Boolean::New(node_gdal::napi_env(), raw->IsSame(raw_other));
}

/**
 * Returns the color with the given ID.
 *
 * @method get
 * @instance
 * @memberof ColorTable
 * @param {number} index
 * @throws {Error}
 * @return {Color}
 */
NAPI_METHOD(ColorTable::get) {
  Args args(info);

  int index;
  if (!args.get(0, "index", index)) return args.failed();

  NODE_UNWRAP_CHECK(ColorTable, info.This(), self);
  GDAL_RAW_CHECK(GDALColorTable *, self, raw);

  Napi::Value parentMaybe = GDAL_GET_PRIVATE(info.This(), "parent_");
  if (!parentMaybe.IsEmpty() && !parentMaybe.IsUndefined() && !parentMaybe.IsNull()) {
    Napi::Object parent = parentMaybe.As<Napi::Object>();
    NODE_UNWRAP_CHECK(RasterBand, parent, band);
  }

  CPLErrorReset();
  const GDALColorEntry *color = raw->GetColorEntry(index);
  if (color == nullptr) {
    NODE_THROW_LAST_CPLERR;
    return node_gdal::napi_env().Undefined();
  }

  Napi::Object result = Napi::Object::New(node_gdal::napi_env());
  result.Set( Napi::String::New(node_gdal::napi_env(), "c1"), Napi::Number::New(node_gdal::napi_env(), color->c1));
  result.Set( Napi::String::New(node_gdal::napi_env(), "c2"), Napi::Number::New(node_gdal::napi_env(), color->c2));
  result.Set( Napi::String::New(node_gdal::napi_env(), "c3"), Napi::Number::New(node_gdal::napi_env(), color->c3));
  result.Set( Napi::String::New(node_gdal::napi_env(), "c4"), Napi::Number::New(node_gdal::napi_env(), color->c4));
  return result;
}

#define COLOR_FROM_OBJ(args, obj, color)                                                                          \
  if (!args.from(obj, "c1", color.c1)) return args.failed();                                                           \
  if (!args.from(obj, "c2", color.c2)) return args.failed();                                                           \
  if (!args.from(obj, "c3", color.c3)) return args.failed();                                                           \
  if (!args.from(obj, "c4", color.c4)) return args.failed();

/**
 * Sets the color entry with the given ID.
 *
 * @method set
 * @instance
 * @memberof ColorTable
 * @throws {Error}
 * @param {number} index
 * @param {Color} color
 * @return {void}
 */

NAPI_METHOD(ColorTable::set) {
  Args args(info);

  int index;
  if (!args.get(0, "index", index)) return args.failed();

  Napi::Object color_obj;
  if (!args.get(1, "color", color_obj)) return args.failed();

  NODE_UNWRAP_CHECK(ColorTable, info.This(), self);
  GDAL_RAW_CHECK(GDALColorTable *, self, raw);

  if (node_gdal::is_read_only(info.This())) {
    node_gdal::throw_read_only(info.Env(), "A ColorTable retrieved from a RasterBand");
    return node_gdal::napi_env().Undefined();
  }

  GDALColorEntry color;
  COLOR_FROM_OBJ(args, color_obj, color);

  CPLErrorReset();
  raw->SetColorEntry(index, &color);
  return node_gdal::napi_env().Undefined();
}

/**
 * Creates a color ramp from one color entry to another.
 *
 * @method ramp
 * @instance
 * @memberof ColorTable
 * @throws {Error}
 * @param {number} start_index
 * @param {Color} start_color
 * @param {number} end_index
 * @param {Color} end_color
 * @return {number} total number of color entries
 */

NAPI_METHOD(ColorTable::ramp) {
  Args args(info);

  int start_index, end_index;
  if (!args.get(0, "start_index", start_index)) return args.failed();
  if (!args.get(2, "end_index", end_index)) return args.failed();
  if (start_index < 0 || end_index < 0 || end_index < start_index) {
    Napi::RangeError::New(node_gdal::napi_env(), "Invalid color interval").ThrowAsJavaScriptException();
    return node_gdal::napi_env().Undefined();
  }

  Napi::Object start_color_obj, end_color_obj;
  if (!args.get(1, "start_color", start_color_obj)) return args.failed();
  if (!args.get(3, "start_color", end_color_obj)) return args.failed();

  NODE_UNWRAP_CHECK(ColorTable, info.This(), self);
  GDAL_RAW_CHECK(GDALColorTable *, self, raw);

  GDALColorEntry start_color, end_color;
  COLOR_FROM_OBJ(args, start_color_obj, start_color);
  COLOR_FROM_OBJ(args, end_color_obj, end_color);

  if (node_gdal::is_read_only(info.This())) {
    node_gdal::throw_read_only(info.Env(), "A ColorTable retrieved from a RasterBand");
    return node_gdal::napi_env().Undefined();
  }

  int r = raw->CreateColorRamp(start_index, &start_color, end_index, &end_color);
  if (r == -1) {
    NODE_THROW_LAST_CPLERR;
    return node_gdal::napi_env().Undefined();
  }

  return Napi::Number::New(node_gdal::napi_env(), r);
}

/**
 * Returns the number of color entries.
 *
 * @method count
 * @instance
 * @memberof ColorTable
 * @return {number}
 */
NAPI_METHOD(ColorTable::count) {

  NODE_UNWRAP_CHECK(ColorTable, info.This(), self);
  GDAL_RAW_CHECK(GDALColorTable *, self, raw);

  Napi::Value parentMaybe = GDAL_GET_PRIVATE(info.This(), "parent_");
  if (!parentMaybe.IsEmpty() && !parentMaybe.IsUndefined() && !parentMaybe.IsNull()) {
    Napi::Object parent = parentMaybe.As<Napi::Object>();
    NODE_UNWRAP_CHECK(RasterBand, parent, band);
  }

  CPLErrorReset();
  return Napi::Number::New(node_gdal::napi_env(), raw->GetColorEntryCount());
}

/**
 * Color interpretation of the palette.
 *
 * @readonly
 * @kind member
 * @name interpretation
 * @instance
 * @memberof ColorTable
 * @type {string}
 */
NAPI_GETTER(ColorTable::interpretationGetter) {

  NODE_UNWRAP_CHECK(ColorTable, info.This(), self);
  GDAL_RAW_CHECK(GDALColorTable *, self, raw);

  Napi::Value parentMaybe = GDAL_GET_PRIVATE(info.This(), "parent_");
  if (!parentMaybe.IsEmpty() && !parentMaybe.IsUndefined() && !parentMaybe.IsNull()) {
    Napi::Object parent = parentMaybe.As<Napi::Object>();
    NODE_UNWRAP_CHECK(RasterBand, parent, band);
  }

  CPLErrorReset();
  GDALPaletteInterp interp = raw->GetPaletteInterpretation();
  std::string r;
  switch (interp) {
    case GPI_Gray: r = "Gray"; break;
    case GPI_RGB: r = "RGB"; break;
    case GPI_CMYK: r = "CMYK"; break;
    case GPI_HLS: r = "HLS"; break;
    default: r = "invalid"; break;
  }
  return SafeString::New(r.c_str());
}

/**
 * Returns the parent band.
 *
 * @readonly
 * @kind member
 * @name band
 * @instance
 * @memberof ColorTable
 * @type {RasterBand|undefined}
 */
NAPI_GETTER(ColorTable::bandGetter) {
  Napi::Value parentMaybe = GDAL_GET_PRIVATE(info.This(), "parent_");
  if (!parentMaybe.IsEmpty() && !parentMaybe.IsUndefined() && !parentMaybe.IsNull()) {
    return parentMaybe;
  }
  return node_gdal::napi_env().Undefined();
}

} // namespace node_gdal
