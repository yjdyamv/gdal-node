#include "gdal_attribute.hpp"
#include "gdal_common.hpp"
#include "gdal_driver.hpp"
#include "gdal_group.hpp"
#include "collections/group_groups.hpp"
#include "collections/group_arrays.hpp"
#include "geometry/gdal_geometry.hpp"
#include "gdal_layer.hpp"
#include "gdal_majorobject.hpp"
#include "gdal_rasterband.hpp"
#include "gdal_spatial_reference.hpp"

namespace node_gdal {

#if GDAL_VERSION_MAJOR > 3 || (GDAL_VERSION_MAJOR == 3 && GDAL_VERSION_MINOR >= 1)

Napi::FunctionReference &Attribute::constructor() {
  return node_gdal::env_context().constructor<Attribute>();
}

Attribute::Attribute(const Napi::CallbackInfo &info) : GDALObject<Attribute>(info), uid(0), this_(nullptr), parent_ds(0) {
  std::shared_ptr<GDALAttribute> pending = node_gdal::TakePendingShared<GDALAttribute>();
  if (pending) {
    this_ = pending;
    LOG("Created attribute [%p]", this_.get());
    return;
  }
  Napi::Error::New(info.Env(), "Cannot create Attribute directly").ThrowAsJavaScriptException();
}

Attribute::~Attribute() {
  dispose();
}

void Attribute::dispose() {
  if (this_) {

    LOG("Disposing attribute [%p]", this_.get());

    object_store().dispose(uid);

    LOG("Disposed attribute [%p]", this_.get());
  }
};

/**
 * A representation of a group with access methods.
 *
 * @class Attribute
 */

Napi::Value Attribute::New(std::shared_ptr<GDALAttribute> raw, GDALDataset *parent_ds) {

  if (!raw) { return node_gdal::napi_env().Null(); }
  if (object_store().has(raw)) { return object_store().get(raw); }

  node_gdal::detail::PendingObjectGuard pending(node_gdal::ExportShared(raw));
  std::vector<napi_value> args;
  Napi::Object obj = Attribute::constructor().Value().New(args);
  Attribute *wrapped = node_gdal::UnwrapWrapped<Attribute>(obj);

  Dataset *unwrapped_ds = node_gdal::UnwrapWrapped<Dataset>(object_store().get(parent_ds));
  long parent_uid = unwrapped_ds->uid;

  wrapped->uid = object_store().add(raw, *wrapped, parent_uid);
  wrapped->parent_ds = parent_ds;
  wrapped->parent_uid = parent_uid;


  return obj;
}

/**
 * Complex GDAL data types introduced in 3.1 are not yet supported
 * @readonly
 * @kind member
 * @name value
 * @instance
 * @memberof Attribute
 * @throws {Error}
 * @type {string|number}
 */
/**
 * @readonly
 * @kind member
 * @name dataType
 * @instance
 * @memberof Attribute
 * @type {string}
 */
#endif

namespace {

Napi::Value toString(const Napi::CallbackInfo &info) {
  return Napi::String::New(node_gdal::napi_env(), "Attribute");
}


Napi::Value valueGetter(const Napi::CallbackInfo &info) {
  Attribute *attribute = node_gdal::unwrap_or_throw<Attribute>(info.This(), "Attribute");
  std::shared_ptr<GDALAttribute> raw = node_gdal::raw_or_throw<std::shared_ptr<GDALAttribute>>(attribute, "std::shared_ptr<GDALAttribute>");
  GDAL_LOCK_PARENT(attribute);
  GDALExtendedDataType type = raw->GetDataType();
  Napi::Value r;
  switch (type.GetClass()) {
    case GEDTC_NUMERIC: r = Napi::Number::New(node_gdal::napi_env(), raw->ReadAsDouble()); break;
    case GEDTC_STRING: r = SafeString::New(raw->ReadAsString()); break;
    default: Napi::Error::New(node_gdal::napi_env(), "Compound attributes are not supported yet").ThrowAsJavaScriptException(); return node_gdal::napi_env().Undefined();
  }

  return r;
}


Napi::Value typeGetter(const Napi::CallbackInfo &info) {
  Attribute *attribute = node_gdal::unwrap_or_throw<Attribute>(info.This(), "Attribute");
  std::shared_ptr<GDALAttribute> raw = node_gdal::raw_or_throw<std::shared_ptr<GDALAttribute>>(attribute, "std::shared_ptr<GDALAttribute>");
  GDAL_LOCK_PARENT(attribute);
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


Napi::Value uidGetter(const Napi::CallbackInfo &info) {
  Attribute *group = node_gdal::UnwrapWrapped<Attribute>(info.This().As<Napi::Object>());
  return Napi::Number::New(node_gdal::napi_env(), (int)group->uid);
}

} // namespace

void Attribute::Initialize(Napi::Object target) {
  Napi::Env env = target.Env();
  static constexpr MethodEntry methods[] = {
    method("toString", toString),
  };

  static constexpr AccessorEntry accessors[] = {
    hiddenAttr("_uid", uidGetter, READ_ONLY_SETTER),
    attr("dataType", typeGetter, READ_ONLY_SETTER),
    attr("value", valueGetter, READ_ONLY_SETTER),
  };

  Napi::Function lcons = GDALDefineClass<Attribute>(env, "Attribute", methods, accessors);

  target.Set("Attribute", lcons);

  constructor() = Napi::Persistent(lcons);
}

} // namespace node_gdal
