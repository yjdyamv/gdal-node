
#include "gdal_feature.hpp"
#include "utils/args.hpp"
#include "collections/feature_fields.hpp"
#include "gdal_common.hpp"
#include "gdal_feature_defn.hpp"
#include "gdal_field_defn.hpp"
#include "geometry/gdal_geometry.hpp"
#include "gdal_layer.hpp"

namespace node_gdal {

namespace {
// The wrapper owns the feature it holds: the factory either destroys the one of
// a layer or is handed a fresh one
void free_feature(void *ptr) {
  OGRFeature::DestroyFeature(static_cast<OGRFeature *>(ptr));
}
} // namespace

Napi::FunctionReference &Feature::constructor() {
  return node_gdal::env_context().constructor<Feature>();
}

//
// What a feature costs, as far as it can be told from the outside: the object
// itself, the geometries it holds and a guess at the fields. It is deliberately
// on the low side, the point is to give the garbage collector a reason to run,
// not to be exact.
//
static int64_t feature_memory(OGRFeature *feature) {
  int64_t size = sizeof(OGRFeature);

  for (int i = 0; i < feature->GetGeomFieldCount(); i++) {
    OGRGeometry *geometry = feature->GetGeomFieldRef(i);
    if (geometry != nullptr) size += geometry->WkbSize();
  }

  return size + static_cast<int64_t>(feature->GetFieldCount()) * 16;
}

/**
 * A vector feature.
 *
 * @constructor
 * @class Feature
 * @param {Layer|FeatureDefn} layer_or_defn The layer or the feature definition
 * the feature will be created from
 */
Feature::Feature(const Napi::CallbackInfo &info) : GDALObject<Feature>(info) {
  Handle<OGRFeature> pending = node_gdal::TakePendingHandle<OGRFeature>();
  if (pending) {
    this_ = std::move(pending);
    LOG("Created Feature[%p]", this_);
  } else {

    if (info.Length() < 1) {
      Napi::Error::New(info.Env(), "Constructor expects Layer or FeatureDefn object").ThrowAsJavaScriptException();
      return;
    }

    OGRFeatureDefn *def;

    if (node_gdal::IsInstanceOf<Layer>(info[0])) {
      Layer *layer = node_gdal::UnwrapWrapped<Layer>(info[0].As<Napi::Object>());
      if (!layer->isAlive()) {
        Napi::Error::New(info.Env(), "Layer object has already been destroyed").ThrowAsJavaScriptException();
        return;
      }
      def = layer->get()->GetLayerDefn();
    } else if (node_gdal::IsInstanceOf<FeatureDefn>(info[0])) {
      FeatureDefn *feature_def = node_gdal::UnwrapWrapped<FeatureDefn>(info[0].As<Napi::Object>());
      if (!feature_def->isAlive()) {
        Napi::Error::New(info.Env(), "FeatureDefn object has already been destroyed").ThrowAsJavaScriptException();
        return;
      }
      def = feature_def->get();
    } else {
      Napi::Error::New(info.Env(), "Constructor expects Layer or FeatureDefn object").ThrowAsJavaScriptException();
      return;
    }

    this_ = Handle<OGRFeature>::owned(new OGRFeature(def), &free_feature);
    LOG("Created Feature[%p]", this_);
  }

  setExternalMemory(feature_memory(this_));

  Napi::Value fields = FeatureFields::New(info.This());
  node_gdal::set_private(info.This(), "fields_", fields);
}

Feature::~Feature() {
  dispose();
}

void Feature::dispose() {
  // The handle destroys the feature, if this wrapper owns it
  LOG("Disposed Feature [%p]", this_.get());
  this_.reset();
  setExternalMemory(0);
}

void Feature::updateMemory() {
  if (!this_) {
    setExternalMemory(0);
    return;
  }
  setExternalMemory(feature_memory(this_));
}

// Currently read-only feature definitions are copied.

Napi::Value Feature::New(OGRFeature *feature) {
  return Feature::New(feature, true);
}

// Features are not tracked in the object store: the same OGRFeature can be
// handed to JS more than once
Napi::Value Feature::New(OGRFeature *feature, bool owned) {
  if (!feature) { return node_gdal::napi_env().Null(); }

  node_gdal::detail::PendingObjectGuard pending(feature, owned ? &free_feature : nullptr);
  std::vector<napi_value> args;
  return Feature::constructor().Value().New(args);
}

/**
 * Returns the geometry of the feature.
 *
 * @method getGeometry
 * @instance
 * @memberof Feature
 * @return {Geometry|null}
 */
#if 0
/*
 * Returns the definition of a particular field at an index.
 *
 * _method getFieldDefn
 * _param {number} index Field index (0-based)
 * _return {FieldDefn}
 */
NAPI_METHOD(Feature::getFieldDefn) {
  Args args(info);
  int field_index;
  if (!args.get(0, "field index", field_index)) return args.failed();

  Feature *feature = node_gdal::unwrap_or_throw<Feature>(info.This().As<Napi::Object>(), "Feature");

  if (field_index < 0 || field_index >= feature->this_->GetFieldCount()) {
    Napi::RangeError::New(node_gdal::napi_env(), "Invalid field index").ThrowAsJavaScriptException();
    return node_gdal::napi_env().Undefined();
  }

  return FieldDefn::New(feature->this_->GetFieldDefnRef(field_index), false);
}
#endif

/**
 * Sets the feature's geometry.
 *
 * @throws {Error}
 * @method setGeometry
 * @instance
 * @memberof Feature
 * @param {Geometry|null} geometry new geometry or null to clear the field
 */
/**
 * Clones the feature.
 *
 * @method clone
 * @instance
 * @memberof Feature
 * @return {Feature}
 */
/**
 * Releases the feature from memory.
 *
 * @method destroy
 * @instance
 * @memberof Feature
 */
/**
 * Set one feature from another. Overwrites the contents of this feature
 * from the geometry and attributes of another.
 *
 * @example
 *
 * var feature1 = new gdal.Feature(defn);
 * var feature2 = new gdal.Feature(defn);
 * feature1.setGeometry(new gdal.Point(5, 10));
 * feature1.fields.set([5, 'test', 3.14]);
 * feature2.setFrom(feature1);
 *
 * @throws {Error}
 * @method setFrom
 * @instance
 * @memberof Feature
 * @param {Feature} feature
 * @param {number[]} [index_map] Array mapping each field from the source feature
 * to the given index in the destination feature. -1 ignores the source field.
 * The field types must still match otherwise the behavior is undefined.
 * @param {boolean} [forgiving=true] `true` if the operation should continue
 * despite lacking output fields matching some of the source fields.
 */
/**
 * @readonly
 * @kind member
 * @name fields
 * @instance
 * @memberof Feature
 * @type {FeatureFields}
 */
/**
 * @kind member
 * @name fid
 * @instance
 * @memberof Feature
 * @type {number}
 */
/**
 * @readonly
 * @kind member
 * @name defn
 * @instance
 * @memberof Feature
 * @type {FeatureDefn}
 */
/**
 * Returns the OGR style string for this feature, if any.
 *
 * @method getStyleString
 * @instance
 * @memberof Feature
 * @return {string|null}
 */
/**
 * Sets the OGR style string for this feature. Pass null/undefined to clear.
 *
 * @throws {Error}
 * @method setStyleString
 * @instance
 * @memberof Feature
 * @param {string|null|undefined} style
 */
namespace {

Napi::Value toString(const Napi::CallbackInfo &info) {
  return Napi::String::New(node_gdal::napi_env(), "Feature");
}


Napi::Value getGeometry(const Napi::CallbackInfo &info) {

  Feature *feature = node_gdal::unwrap_or_throw<Feature>(info.This().As<Napi::Object>(), "Feature");

  OGRGeometry *geom = feature->get()->GetGeometryRef();
  if (!geom) {
    return node_gdal::napi_env().Null();
    return node_gdal::napi_env().Undefined();
  }

  return Geometry::New(geom, false);
}


Napi::Value setGeometry(const Napi::CallbackInfo &info) {
  Args args(info);

  Geometry *geom = nullptr;
  if (!args.optionalWrapped(0, "geometry", "Geometry", geom)) return args.failed();

  Feature *feature = node_gdal::unwrap_or_throw<Feature>(info.This().As<Napi::Object>(), "Feature");

  OGRErr err = feature->get()->SetGeometry(geom ? geom->get() : nullptr);
  if (err) { throw GDALError(getOGRErrMsg(err)); }

  feature->updateMemory();

  return node_gdal::napi_env().Undefined();
}


Napi::Value clone(const Napi::CallbackInfo &info) {
  Feature *feature = node_gdal::unwrap_or_throw<Feature>(info.This().As<Napi::Object>(), "Feature");
  return Feature::New(feature->get()->Clone());
}


Napi::Value destroy(const Napi::CallbackInfo &info) {
  Feature *feature = node_gdal::unwrap_or_throw<Feature>(info.This().As<Napi::Object>(), "Feature");
  feature->dispose();
  return node_gdal::napi_env().Undefined();
}


Napi::Value setFrom(const Napi::CallbackInfo &info) {
  Args args(info);
  Feature *other_feature;
  int forgiving = 1;
  Napi::Array index_map;
  OGRErr err = 0;

  if (!args.getWrapped(0, "feature", "Feature", other_feature)) return args.failed();

  Feature *feature = node_gdal::unwrap_or_throw<Feature>(info.This().As<Napi::Object>(), "Feature");

  if (!info[1].IsArray()) {
    if (!args.optional(1, "forgiving", forgiving)) return args.failed();

    err = feature->get()->SetFrom(other_feature->get(), forgiving ? TRUE : FALSE);
  } else {
    if (!args.get(1, "index map", index_map)) return args.failed();
    if (!args.optional(2, "forgiving", forgiving)) return args.failed();

    if (index_map.Length() < 1) {
      Napi::Error::New(node_gdal::napi_env(), "index map must contain at least 1 index").ThrowAsJavaScriptException();
      return node_gdal::napi_env().Undefined();
    }

    int *index_map_ptr = new int[index_map.Length()];

    for (unsigned index = 0; index < index_map.Length(); index++) {
      Napi::Value field_index(index_map.As<Napi::Object>().Get(Napi::Number::New(node_gdal::napi_env(), index)));

      if (!field_index.IsNumber()) {
        delete[] index_map_ptr;
        Napi::Error::New(node_gdal::napi_env(), "index map must contain only integer values").ThrowAsJavaScriptException();
        return node_gdal::napi_env().Undefined();
      }

      int val = (int)field_index.As<Napi::Number>().Int32Value(); // todo: validate index? perhaps ogr already
                                                                // does this and throws an error

      index_map_ptr[index] = val;
    }

    err = feature->get()->SetFrom(other_feature->get(), index_map_ptr, forgiving ? TRUE : FALSE);

    delete[] index_map_ptr;
  }

  if (err) {
    throw GDALError(getOGRErrMsg(err));
    return node_gdal::napi_env().Undefined();
  }

  feature->updateMemory();

  return node_gdal::napi_env().Undefined();
}


Napi::Value fieldsGetter(const Napi::CallbackInfo &info) {
  node_gdal::requireAlive<Feature>(info.This(), "Feature");
  return node_gdal::get_private(info.This(), "fields_");
}


Napi::Value fidGetter(const Napi::CallbackInfo &info) {
  Feature *feature = node_gdal::unwrap_or_throw<Feature>(info.This().As<Napi::Object>(), "Feature");
  return Napi::Number::New(node_gdal::napi_env(), feature->get()->GetFID());
}


Napi::Value defnGetter(const Napi::CallbackInfo &info) {
  Feature *feature = node_gdal::unwrap_or_throw<Feature>(info.This().As<Napi::Object>(), "Feature");
  return FeatureDefn::New(feature->get()->GetDefnRef());
}


Napi::Value getStyleString(const Napi::CallbackInfo &info) {
  Feature *feature = node_gdal::unwrap_or_throw<Feature>(info.This().As<Napi::Object>(), "Feature");
  const char *psz = feature->get()->GetStyleString();
  if (!psz) {
    return node_gdal::napi_env().Null();
    return node_gdal::napi_env().Undefined();
  }
  return Napi::String::New(node_gdal::napi_env(), psz);
}


Napi::Value setStyleString(const Napi::CallbackInfo &info) {
  Feature *feature = node_gdal::unwrap_or_throw<Feature>(info.This().As<Napi::Object>(), "Feature");

  if (info.Length() < 1 || info[0].IsNull() || info[0].IsUndefined()) {
    // Clear style if null/undefined or no arg
    feature->get()->SetStyleString(nullptr);
    return node_gdal::napi_env().Undefined();
  }

  if (!info[0].IsString()) {
    Napi::TypeError::New(node_gdal::napi_env(), "style must be a string, null or undefined").ThrowAsJavaScriptException();
    return node_gdal::napi_env().Undefined();
  }

  std::string utf8 = info[0].As<Napi::String>().Utf8Value();
  feature->get()->SetStyleString(utf8.c_str());
  return node_gdal::napi_env().Undefined();
}


void fidSetter(const Napi::CallbackInfo &info, const Napi::Value &value) {
  Feature *feature = node_gdal::UnwrapWrapped<Feature>(info.This().As<Napi::Object>());
  if (!feature->isAlive()) {
    Napi::Error::New(node_gdal::napi_env(), "Feature object has already been destroyed").ThrowAsJavaScriptException();
    return;
  }
  if (!value.IsNumber()) {
    Napi::Error::New(node_gdal::napi_env(), "fid must be an integer").ThrowAsJavaScriptException();
    return;
  }
  feature->get()->SetFID(value.As<Napi::Number>().Int64Value());
}

} // namespace

void Feature::Initialize(Napi::Object target) {
  Napi::Env env = target.Env();
  static constexpr MethodEntry methods[] = {
    method("toString", toString),
    method("getGeometry", getGeometry),
    method("setGeometry", setGeometry),
    method("clone", clone),
    method("setFrom", setFrom),
    method("getStyleString", getStyleString),
    method("setStyleString", setStyleString),
    method("destroy", destroy),
  };

  static constexpr AccessorEntry accessors[] = {
    attr("fields", fieldsGetter, READ_ONLY_SETTER),
    attr("defn", defnGetter, READ_ONLY_SETTER),
    attr("fid", fidGetter, fidSetter),
  };

  Napi::Function lcons = GDALDefineClass<Feature>(env, "Feature", methods, accessors);

  // method("setGeometryDirectly", setGeometryDirectly),
  // method("stealGeometry", stealGeometry),
  // method("getFieldDefn", getFieldDefn), (use defn.fields.get() instead)
  // Note: This is used mainly for testing

  target.Set("Feature", lcons);

  constructor() = Napi::Persistent(lcons);
}

} // namespace node_gdal
