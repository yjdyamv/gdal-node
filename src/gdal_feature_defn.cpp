
#include "gdal_feature_defn.hpp"
#include "collections/feature_defn_fields.hpp"
#include "gdal_common.hpp"
#include "gdal_field_defn.hpp"

namespace node_gdal {

namespace {
// The wrapper owns the feature definition it holds: the factory clones the one
// of a layer or is handed a fresh one
void free_feature_defn(void *ptr) {
  static_cast<OGRFeatureDefn *>(ptr)->Release();
}
} // namespace

Napi::FunctionReference &FeatureDefn::constructor() {
  return node_gdal::env_context().constructor<FeatureDefn>();
}

FeatureDefn::FeatureDefn(const Napi::CallbackInfo &info) : GDALObject<FeatureDefn>(info) {
  Handle<OGRFeatureDefn> pending = node_gdal::TakePendingHandle<OGRFeatureDefn>();
  if (pending) {
    this_ = std::move(pending);
  } else if (info.Length() == 0) {
    // constructed from JS: new gdal.FeatureDefn()
    this_ = Handle<OGRFeatureDefn>::owned(new OGRFeatureDefn(), &free_feature_defn);
    this_->Reference();
  } else {
    Napi::Error::New(info.Env(), "FeatureDefn constructor doesn't take any arguments").ThrowAsJavaScriptException();
    return;
  }
  LOG("Created FeatureDefn [%p]", this_);

  // the fields collection is kept as a private (JS-invisible) property
  Napi::Value fields = FeatureDefnFields::New(info.This());
  node_gdal::set_private(info.This(), "fields_", fields);
}

FeatureDefn::~FeatureDefn() {
  // The handle releases the feature definition, if this wrapper owns it
  LOG("Disposed FeatureDefn [%p]", this_.get());
}

// The feature definition of a feature cannot be handed out: it belongs to the
// layer the feature came from. The factory hands out a read-only clone
Napi::Value FeatureDefn::New(const OGRFeatureDefn *def) {
  if (!def) { return node_gdal::napi_env().Null(); }
  OGRFeatureDefn *copy = def->Clone();
  Napi::Value obj = FeatureDefn::New(copy, true);
  node_gdal::set_read_only(obj.As<Napi::Object>());
  return obj;
}

Napi::Value FeatureDefn::New(OGRFeatureDefn *def, bool owned) {

  if (!def) { return node_gdal::napi_env().Null(); }
  if (!owned) { def = def->Clone(); }

  node_gdal::detail::PendingObjectGuard pending(def, &free_feature_defn);
  std::vector<napi_value> args;
  return FeatureDefn::constructor().Value().New(args);
}

/**
 * Clones the feature definition.
 *
 * @method clone
 * @instance
 * @memberof FeatureDefn
 * @return {FeatureDefn}
 */
/**
 * @readonly
 * @kind member
 * @name name
 * @instance
 * @memberof FeatureDefn
 * @type {string}
 */
/**
 * WKB geometry type ({@link wkbGeometryType|see table}
 *
 * @kind member
 * @name geomType
 * @instance
 * @memberof FeatureDefn
 * @type {number}
 */
/**
 * @kind member
 * @name geomIgnored
 * @instance
 * @memberof FeatureDefn
 * @type {boolean}
 */
/**
 * @kind member
 * @name styleIgnored
 * @instance
 * @memberof FeatureDefn
 * @type {boolean}
 */
/**
 * @readonly
 * @kind member
 * @name fields
 * @instance
 * @memberof FeatureDefn
 * @type {FeatureDefnFields}
 */
namespace {

Napi::Value toString(const Napi::CallbackInfo &info) {
  return Napi::String::New(node_gdal::napi_env(), "FeatureDefn");
}


Napi::Value clone(const Napi::CallbackInfo &info) {
  FeatureDefn *def = node_gdal::UnwrapWrapped<FeatureDefn>(info.This().As<Napi::Object>());
  // the clone is ours, so it is not read-only
  return FeatureDefn::New(def->get()->Clone(), true);
}


Napi::Value nameGetter(const Napi::CallbackInfo &info) {
  FeatureDefn *def = node_gdal::UnwrapWrapped<FeatureDefn>(info.This().As<Napi::Object>());
  return SafeString::New(def->get()->GetName());
}


Napi::Value geomTypeGetter(const Napi::CallbackInfo &info) {
  FeatureDefn *def = node_gdal::UnwrapWrapped<FeatureDefn>(info.This().As<Napi::Object>());
  return Napi::Number::New(node_gdal::napi_env(), def->get()->GetGeomType());
}


Napi::Value geomIgnoredGetter(const Napi::CallbackInfo &info) {
  FeatureDefn *def = node_gdal::UnwrapWrapped<FeatureDefn>(info.This().As<Napi::Object>());
  return Napi::Boolean::New(node_gdal::napi_env(), def->get()->IsGeometryIgnored());
}


Napi::Value styleIgnoredGetter(const Napi::CallbackInfo &info) {
  FeatureDefn *def = node_gdal::UnwrapWrapped<FeatureDefn>(info.This().As<Napi::Object>());
  return Napi::Boolean::New(node_gdal::napi_env(), def->get()->IsStyleIgnored());
}


Napi::Value fieldsGetter(const Napi::CallbackInfo &info) {
  return node_gdal::get_private(info.This(), "fields_");
}


void geomTypeSetter(const Napi::CallbackInfo &info, const Napi::Value &value) {
  FeatureDefn *def = node_gdal::UnwrapWrapped<FeatureDefn>(info.This().As<Napi::Object>());
  if (node_gdal::is_read_only(info.This())) {
    node_gdal::throw_read_only(info.Env(), "A FeatureDefn retrieved from a feature");
    return;
  }
  if (!value.IsNumber()) {
    Napi::Error::New(node_gdal::napi_env(), "geomType must be an integer").ThrowAsJavaScriptException();
    return;
  }
  def->get()->SetGeomType(OGRwkbGeometryType(value.As<Napi::Number>().Int64Value()));
}


void geomIgnoredSetter(const Napi::CallbackInfo &info, const Napi::Value &value) {
  FeatureDefn *def = node_gdal::UnwrapWrapped<FeatureDefn>(info.This().As<Napi::Object>());
  if (node_gdal::is_read_only(info.This())) {
    node_gdal::throw_read_only(info.Env(), "A FeatureDefn retrieved from a feature");
    return;
  }
  if (!value.IsBoolean()) {
    Napi::Error::New(node_gdal::napi_env(), "geomIgnored must be a boolean").ThrowAsJavaScriptException();
    return;
  }
  def->get()->SetGeometryIgnored(value.As<Napi::Boolean>().Value());
}


void styleIgnoredSetter(const Napi::CallbackInfo &info, const Napi::Value &value) {
  FeatureDefn *def = node_gdal::UnwrapWrapped<FeatureDefn>(info.This().As<Napi::Object>());
  if (node_gdal::is_read_only(info.This())) {
    node_gdal::throw_read_only(info.Env(), "A FeatureDefn retrieved from a feature");
    return;
  }
  if (!value.IsBoolean()) {
    Napi::Error::New(node_gdal::napi_env(), "styleIgnored must be a boolean").ThrowAsJavaScriptException();
    return;
  }
  def->get()->SetStyleIgnored(value.As<Napi::Boolean>().Value());
}

} // namespace

void FeatureDefn::Initialize(Napi::Object target) {
  Napi::Env env = target.Env();
  static constexpr MethodEntry methods[] = {
    method("toString", toString),
    method("clone", clone),
  };

  static constexpr AccessorEntry accessors[] = {
    attr("name", nameGetter, READ_ONLY_SETTER),
    attr("fields", fieldsGetter, READ_ONLY_SETTER),
    attr("styleIgnored", styleIgnoredGetter, styleIgnoredSetter),
    attr("geomIgnored", geomIgnoredGetter, geomIgnoredSetter),
    attr("geomType", geomTypeGetter, geomTypeSetter),
  };

  Napi::Function lcons = GDALDefineClass<FeatureDefn>(env, "FeatureDefn", methods, accessors);

  target.Set("FeatureDefn", lcons);

  constructor() = Napi::Persistent(lcons);
}

} // namespace node_gdal
