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
  node_gdal::set_private(obj, "parent_", parent);
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

/**
 * Returns the number of color entries.
 *
 * @method count
 * @instance
 * @memberof ColorTable
 * @return {number}
 */
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
namespace {

Napi::Value toString(const Napi::CallbackInfo &info) {
  return Napi::String::New(node_gdal::napi_env(), "ColorTable");
}


Napi::Value clone(const Napi::CallbackInfo &info) {
  ColorTable *ct = node_gdal::UnwrapWrapped<ColorTable>(info.This().As<Napi::Object>());
  return ColorTable::New(ct->get()->Clone());
}


Napi::Value isSame(const Napi::CallbackInfo &info) {
  Args args(info);

  ColorTable *self = node_gdal::unwrap_or_throw<ColorTable>(info.This(), "ColorTable");
  GDALColorTable * raw = node_gdal::raw_or_throw<GDALColorTable *>(self, "GDALColorTable");

  ColorTable *other;
  if (!args.getWrapped(0, "other", "ColorTable", other)) return args.failed();
  GDALColorTable * raw_other = node_gdal::raw_or_throw<GDALColorTable *>(other, "GDALColorTable");

  CPLErrorReset();
  return Napi::Boolean::New(node_gdal::napi_env(), raw->IsSame(raw_other));
}


Napi::Value getEntry(const Napi::CallbackInfo &info) {
  Args args(info);

  int index;
  if (!args.get(0, "index", index)) return args.failed();

  ColorTable *self = node_gdal::unwrap_or_throw<ColorTable>(info.This(), "ColorTable");
  GDALColorTable * raw = node_gdal::raw_or_throw<GDALColorTable *>(self, "GDALColorTable");

  Napi::Value parentMaybe = node_gdal::get_private(info.This(), "parent_");
  if (!parentMaybe.IsEmpty() && !parentMaybe.IsUndefined() && !parentMaybe.IsNull()) {
    Napi::Object parent = parentMaybe.As<Napi::Object>();
    RasterBand *band = node_gdal::unwrap_or_throw<RasterBand>(parent, "RasterBand");
  }

  CPLErrorReset();
  const GDALColorEntry *color = raw->GetColorEntry(index);
  if (color == nullptr) {
    throw GDALError::fromGDAL();
    return node_gdal::napi_env().Undefined();
  }

  Napi::Object result = Napi::Object::New(node_gdal::napi_env());
  result.Set( Napi::String::New(node_gdal::napi_env(), "c1"), Napi::Number::New(node_gdal::napi_env(), color->c1));
  result.Set( Napi::String::New(node_gdal::napi_env(), "c2"), Napi::Number::New(node_gdal::napi_env(), color->c2));
  result.Set( Napi::String::New(node_gdal::napi_env(), "c3"), Napi::Number::New(node_gdal::napi_env(), color->c3));
  result.Set( Napi::String::New(node_gdal::napi_env(), "c4"), Napi::Number::New(node_gdal::napi_env(), color->c4));
  return result;
}


Napi::Value set(const Napi::CallbackInfo &info) {
  Args args(info);

  int index;
  if (!args.get(0, "index", index)) return args.failed();

  Napi::Object color_obj;
  if (!args.get(1, "color", color_obj)) return args.failed();

  ColorTable *self = node_gdal::unwrap_or_throw<ColorTable>(info.This(), "ColorTable");
  GDALColorTable * raw = node_gdal::raw_or_throw<GDALColorTable *>(self, "GDALColorTable");

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


Napi::Value ramp(const Napi::CallbackInfo &info) {
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

  ColorTable *self = node_gdal::unwrap_or_throw<ColorTable>(info.This(), "ColorTable");
  GDALColorTable * raw = node_gdal::raw_or_throw<GDALColorTable *>(self, "GDALColorTable");

  GDALColorEntry start_color, end_color;
  COLOR_FROM_OBJ(args, start_color_obj, start_color);
  COLOR_FROM_OBJ(args, end_color_obj, end_color);

  if (node_gdal::is_read_only(info.This())) {
    node_gdal::throw_read_only(info.Env(), "A ColorTable retrieved from a RasterBand");
    return node_gdal::napi_env().Undefined();
  }

  int r = raw->CreateColorRamp(start_index, &start_color, end_index, &end_color);
  if (r == -1) {
    throw GDALError::fromGDAL();
    return node_gdal::napi_env().Undefined();
  }

  return Napi::Number::New(node_gdal::napi_env(), r);
}


Napi::Value count(const Napi::CallbackInfo &info) {

  ColorTable *self = node_gdal::unwrap_or_throw<ColorTable>(info.This(), "ColorTable");
  GDALColorTable * raw = node_gdal::raw_or_throw<GDALColorTable *>(self, "GDALColorTable");

  Napi::Value parentMaybe = node_gdal::get_private(info.This(), "parent_");
  if (!parentMaybe.IsEmpty() && !parentMaybe.IsUndefined() && !parentMaybe.IsNull()) {
    Napi::Object parent = parentMaybe.As<Napi::Object>();
    RasterBand *band = node_gdal::unwrap_or_throw<RasterBand>(parent, "RasterBand");
  }

  CPLErrorReset();
  return Napi::Number::New(node_gdal::napi_env(), raw->GetColorEntryCount());
}


Napi::Value interpretationGetter(const Napi::CallbackInfo &info) {

  ColorTable *self = node_gdal::unwrap_or_throw<ColorTable>(info.This(), "ColorTable");
  GDALColorTable * raw = node_gdal::raw_or_throw<GDALColorTable *>(self, "GDALColorTable");

  Napi::Value parentMaybe = node_gdal::get_private(info.This(), "parent_");
  if (!parentMaybe.IsEmpty() && !parentMaybe.IsUndefined() && !parentMaybe.IsNull()) {
    Napi::Object parent = parentMaybe.As<Napi::Object>();
    RasterBand *band = node_gdal::unwrap_or_throw<RasterBand>(parent, "RasterBand");
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


Napi::Value bandGetter(const Napi::CallbackInfo &info) {
  Napi::Value parentMaybe = node_gdal::get_private(info.This(), "parent_");
  if (!parentMaybe.IsEmpty() && !parentMaybe.IsUndefined() && !parentMaybe.IsNull()) {
    return parentMaybe;
  }
  return node_gdal::napi_env().Undefined();
}

} // namespace

void ColorTable::Initialize(Napi::Object target) {
  Napi::Env env = target.Env();
  static constexpr MethodEntry methods[] = {
    method("toString", toString),
    method("isSame", isSame),
    method("clone", clone),
    method("count", count),
    method("get", getEntry),
    method("set", set),
    method("ramp", ramp),
  };

  static constexpr AccessorEntry accessors[] = {
    attr("interpretation", interpretationGetter, READ_ONLY_SETTER),
    hiddenAttr("band", bandGetter, READ_ONLY_SETTER),
  };

  Napi::Function lcons = GDALDefineClass<ColorTable>(env, "ColorTable", methods, accessors);

  target.Set("ColorTable", lcons);

  constructor() = Napi::Persistent(lcons);
}


} // namespace node_gdal
