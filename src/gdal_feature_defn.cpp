
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

void FeatureDefn::Initialize(Napi::Object target) {
  Napi::Env env = target.Env();
  SELF_CLASS(FeatureDefn);

  // NOTE: the descriptor macros carry their own trailing comma
  Napi::Function lcons = GDALDefineClass<SELF>(env, "FeatureDefn",
    {
        METHOD(toString)
        METHOD(clone)
        ATTR(lcons, "name", nameGetter, READ_ONLY_SETTER)
        ATTR(lcons, "fields", fieldsGetter, READ_ONLY_SETTER)
        ATTR(lcons, "styleIgnored", styleIgnoredGetter, styleIgnoredSetter)
        ATTR(lcons, "geomIgnored", geomIgnoredGetter, geomIgnoredSetter)
        ATTR(lcons, "geomType", geomTypeGetter, geomTypeSetter)
    });

  target.Set("FeatureDefn", lcons);

  constructor() = Napi::Persistent(lcons);
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
  GDAL_SET_PRIVATE(info.This(), "fields_", fields);
}

FeatureDefn::~FeatureDefn() {
  // The handle releases the feature definition, if this wrapper owns it
  LOG("Disposed FeatureDefn [%p]", this_.get());
}

// Currently read-only feature definitions are copied.
// Modifying a feature definition that should have been
// read-only modifies a shadow copy without any real effect.
// TODO: Implement proper read-only objects that throw

Napi::Value FeatureDefn::New(const OGRFeatureDefn *def) {
  if (!def) { return node_gdal::napi_env().Null(); }
  OGRFeatureDefn *copy = def->Clone();
  return FeatureDefn::New(copy, true);
}

Napi::Value FeatureDefn::New(OGRFeatureDefn *def, bool owned) {

  if (!def) { return node_gdal::napi_env().Null(); }
  if (!owned) { def = def->Clone(); }

  node_gdal::detail::PendingObjectGuard pending(def, &free_feature_defn);
  std::vector<napi_value> args;
  return FeatureDefn::constructor().Value().New(args);
}

NAPI_METHOD(FeatureDefn::toString) {
  return Napi::String::New(node_gdal::napi_env(), "FeatureDefn");
}

/**
 * Clones the feature definition.
 *
 * @method clone
 * @instance
 * @memberof FeatureDefn
 * @return {FeatureDefn}
 */
NAPI_METHOD(FeatureDefn::clone) {
  FeatureDefn *def = node_gdal::UnwrapWrapped<FeatureDefn>(info.This().As<Napi::Object>());
  return FeatureDefn::New(def->this_->Clone());
}

/**
 * @readonly
 * @kind member
 * @name name
 * @instance
 * @memberof FeatureDefn
 * @type {string}
 */
NAPI_GETTER(FeatureDefn::nameGetter) {
  FeatureDefn *def = node_gdal::UnwrapWrapped<FeatureDefn>(info.This().As<Napi::Object>());
  return SafeString::New(def->this_->GetName());
}

/**
 * WKB geometry type ({@link wkbGeometryType|see table}
 *
 * @kind member
 * @name geomType
 * @instance
 * @memberof FeatureDefn
 * @type {number}
 */
NAPI_GETTER(FeatureDefn::geomTypeGetter) {
  FeatureDefn *def = node_gdal::UnwrapWrapped<FeatureDefn>(info.This().As<Napi::Object>());
  return Napi::Number::New(node_gdal::napi_env(), def->this_->GetGeomType());
}

/**
 * @kind member
 * @name geomIgnored
 * @instance
 * @memberof FeatureDefn
 * @type {boolean}
 */
NAPI_GETTER(FeatureDefn::geomIgnoredGetter) {
  FeatureDefn *def = node_gdal::UnwrapWrapped<FeatureDefn>(info.This().As<Napi::Object>());
  return Napi::Boolean::New(node_gdal::napi_env(), def->this_->IsGeometryIgnored());
}

/**
 * @kind member
 * @name styleIgnored
 * @instance
 * @memberof FeatureDefn
 * @type {boolean}
 */
NAPI_GETTER(FeatureDefn::styleIgnoredGetter) {
  FeatureDefn *def = node_gdal::UnwrapWrapped<FeatureDefn>(info.This().As<Napi::Object>());
  return Napi::Boolean::New(node_gdal::napi_env(), def->this_->IsStyleIgnored());
}

/**
 * @readonly
 * @kind member
 * @name fields
 * @instance
 * @memberof FeatureDefn
 * @type {FeatureDefnFields}
 */
NAPI_GETTER(FeatureDefn::fieldsGetter) {
  return GDAL_GET_PRIVATE(info.This(), "fields_");
}

NAPI_SETTER(FeatureDefn::geomTypeSetter) {
  FeatureDefn *def = node_gdal::UnwrapWrapped<FeatureDefn>(info.This().As<Napi::Object>());
  if (!value.IsNumber()) {
    Napi::Error::New(node_gdal::napi_env(), "geomType must be an integer").ThrowAsJavaScriptException();
    return;
  }
  def->this_->SetGeomType(OGRwkbGeometryType(value.As<Napi::Number>().Int64Value()));
}

NAPI_SETTER(FeatureDefn::geomIgnoredSetter) {
  FeatureDefn *def = node_gdal::UnwrapWrapped<FeatureDefn>(info.This().As<Napi::Object>());
  if (!value.IsBoolean()) {
    Napi::Error::New(node_gdal::napi_env(), "geomIgnored must be a boolean").ThrowAsJavaScriptException();
    return;
  }
  def->this_->SetGeometryIgnored(value.As<Napi::Boolean>().Value());
}

NAPI_SETTER(FeatureDefn::styleIgnoredSetter) {
  FeatureDefn *def = node_gdal::UnwrapWrapped<FeatureDefn>(info.This().As<Napi::Object>());
  if (!value.IsBoolean()) {
    Napi::Error::New(node_gdal::napi_env(), "styleIgnored must be a boolean").ThrowAsJavaScriptException();
    return;
  }
  def->this_->SetStyleIgnored(value.As<Napi::Boolean>().Value());
}

} // namespace node_gdal
