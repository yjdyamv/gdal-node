
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

void Feature::Initialize(Napi::Object target) {
  Napi::Env env = target.Env();
  SELF_CLASS(Feature);

  // NOTE: the descriptor macros carry their own trailing comma
  Napi::Function lcons = GDALDefineClass<SELF>(env, "Feature",
    {
        METHOD(toString)
        METHOD(getGeometry)
        METHOD(setGeometry)
        METHOD(clone)
        METHOD(setFrom)
        METHOD(getStyleString)
        METHOD(setStyleString)
        METHOD(destroy)
        ATTR(lcons, "fields", fieldsGetter, READ_ONLY_SETTER)
        ATTR(lcons, "defn", defnGetter, READ_ONLY_SETTER)
        ATTR(lcons, "fid", fidGetter, fidSetter)
    });

  // METHOD(setGeometryDirectly);
  // METHOD(stealGeometry);
  // METHOD(getFieldDefn); (use defn.fields.get() instead)
  // Note: This is used mainly for testing

  target.Set("Feature", lcons);

  constructor() = Napi::Persistent(lcons);
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

    if (IS_WRAPPED(info[0], Layer)) {
      Layer *layer = node_gdal::UnwrapWrapped<Layer>(info[0].As<Napi::Object>());
      if (!layer->isAlive()) {
        Napi::Error::New(info.Env(), "Layer object already destroyed").ThrowAsJavaScriptException();
        return;
      }
      def = layer->get()->GetLayerDefn();
    } else if (IS_WRAPPED(info[0], FeatureDefn)) {
      FeatureDefn *feature_def = node_gdal::UnwrapWrapped<FeatureDefn>(info[0].As<Napi::Object>());
      if (!feature_def->isAlive()) {
        Napi::Error::New(info.Env(), "FeatureDefn object already destroyed").ThrowAsJavaScriptException();
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
  GDAL_SET_PRIVATE(info.This(), "fields_", fields);
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

NAPI_METHOD(Feature::toString) {
  return Napi::String::New(node_gdal::napi_env(), "Feature");
}

/**
 * Returns the geometry of the feature.
 *
 * @method getGeometry
 * @instance
 * @memberof Feature
 * @return {Geometry|null}
 */
NAPI_METHOD(Feature::getGeometry) {

  Feature *feature = node_gdal::UnwrapWrapped<Feature>(info.This().As<Napi::Object>());
  if (!feature->isAlive()) {
    Napi::Error::New(node_gdal::napi_env(), "Feature object already destroyed").ThrowAsJavaScriptException();
    return node_gdal::napi_env().Undefined();
  }

  OGRGeometry *geom = feature->this_->GetGeometryRef();
  if (!geom) {
    return node_gdal::napi_env().Null();
    return node_gdal::napi_env().Undefined();
  }

  return Geometry::New(geom, false);
}

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

  Feature *feature = node_gdal::UnwrapWrapped<Feature>(info.This().As<Napi::Object>());
  if (!feature->isAlive()) {
    Napi::Error::New(node_gdal::napi_env(), "Feature object already destroyed").ThrowAsJavaScriptException();
    return node_gdal::napi_env().Undefined();
  }

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
NAPI_METHOD(Feature::setGeometry) {
  Args args(info);

  Geometry *geom = NULL;
  if (!args.optionalWrapped(0, "geometry", "Geometry", geom)) return args.failed();

  Feature *feature = node_gdal::UnwrapWrapped<Feature>(info.This().As<Napi::Object>());
  if (!feature->isAlive()) {
    Napi::Error::New(node_gdal::napi_env(), "Feature object already destroyed").ThrowAsJavaScriptException();
    return node_gdal::napi_env().Undefined();
  }

  OGRErr err = feature->this_->SetGeometry(geom ? geom->get() : NULL);
  if (err) { NODE_THROW_OGRERR(err); }

  feature->updateMemory();

  return node_gdal::napi_env().Undefined();
}

/**
 * Clones the feature.
 *
 * @method clone
 * @instance
 * @memberof Feature
 * @return {Feature}
 */
NAPI_METHOD(Feature::clone) {
  Feature *feature = node_gdal::UnwrapWrapped<Feature>(info.This().As<Napi::Object>());
  if (!feature->isAlive()) {
    Napi::Error::New(node_gdal::napi_env(), "Feature object already destroyed").ThrowAsJavaScriptException();
    return node_gdal::napi_env().Undefined();
  }
  return Feature::New(feature->this_->Clone());
}

/**
 * Releases the feature from memory.
 *
 * @method destroy
 * @instance
 * @memberof Feature
 */
NAPI_METHOD(Feature::destroy) {
  Feature *feature = node_gdal::UnwrapWrapped<Feature>(info.This().As<Napi::Object>());
  if (!feature->isAlive()) {
    Napi::Error::New(node_gdal::napi_env(), "Feature object already destroyed").ThrowAsJavaScriptException();
    return node_gdal::napi_env().Undefined();
  }
  feature->dispose();
  return node_gdal::napi_env().Undefined();
}

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
NAPI_METHOD(Feature::setFrom) {
  Args args(info);
  Feature *other_feature;
  int forgiving = 1;
  Napi::Array index_map;
  OGRErr err = 0;

  if (!args.getWrapped(0, "feature", "Feature", other_feature)) return args.failed();

  Feature *feature = node_gdal::UnwrapWrapped<Feature>(info.This().As<Napi::Object>());
  if (!feature->isAlive()) {
    Napi::Error::New(node_gdal::napi_env(), "Feature object already destroyed").ThrowAsJavaScriptException();
    return node_gdal::napi_env().Undefined();
  }

  if (!info[1].IsArray()) {
    if (!args.optional(1, "forgiving", forgiving)) return args.failed();

    err = feature->this_->SetFrom(other_feature->this_, forgiving ? TRUE : FALSE);
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

    err = feature->this_->SetFrom(other_feature->this_, index_map_ptr, forgiving ? TRUE : FALSE);

    delete[] index_map_ptr;
  }

  if (err) {
    NODE_THROW_OGRERR(err);
    return node_gdal::napi_env().Undefined();
  }

  feature->updateMemory();

  return node_gdal::napi_env().Undefined();
}

/**
 * @readonly
 * @kind member
 * @name fields
 * @instance
 * @memberof Feature
 * @type {FeatureFields}
 */
NAPI_GETTER(Feature::fieldsGetter) {
  return GDAL_GET_PRIVATE(info.This(), "fields_");
}

/**
 * @kind member
 * @name fid
 * @instance
 * @memberof Feature
 * @type {number}
 */
NAPI_GETTER(Feature::fidGetter) {
  Feature *feature = node_gdal::UnwrapWrapped<Feature>(info.This().As<Napi::Object>());
  if (!feature->isAlive()) {
    Napi::Error::New(node_gdal::napi_env(), "Feature object already destroyed").ThrowAsJavaScriptException();
    return node_gdal::napi_env().Undefined();
  }
  return Napi::Number::New(node_gdal::napi_env(), feature->this_->GetFID());
}

/**
 * @readonly
 * @kind member
 * @name defn
 * @instance
 * @memberof Feature
 * @type {FeatureDefn}
 */
NAPI_GETTER(Feature::defnGetter) {
  Feature *feature = node_gdal::UnwrapWrapped<Feature>(info.This().As<Napi::Object>());
  if (!feature->isAlive()) {
    Napi::Error::New(node_gdal::napi_env(), "Feature object already destroyed").ThrowAsJavaScriptException();
    return node_gdal::napi_env().Undefined();
  }
  return FeatureDefn::New(feature->this_->GetDefnRef());
}

/**
 * Returns the OGR style string for this feature, if any.
 *
 * @method getStyleString
 * @instance
 * @memberof Feature
 * @return {string|null}
 */
NAPI_METHOD(Feature::getStyleString) {
  Feature *feature = node_gdal::UnwrapWrapped<Feature>(info.This().As<Napi::Object>());
  if (!feature->isAlive()) {
    Napi::Error::New(node_gdal::napi_env(), "Feature object already destroyed").ThrowAsJavaScriptException();
    return node_gdal::napi_env().Undefined();
  }
  const char *psz = feature->this_->GetStyleString();
  if (!psz) {
    return node_gdal::napi_env().Null();
    return node_gdal::napi_env().Undefined();
  }
  return Napi::String::New(node_gdal::napi_env(), psz);
}

/**
 * Sets the OGR style string for this feature. Pass null/undefined to clear.
 *
 * @throws {Error}
 * @method setStyleString
 * @instance
 * @memberof Feature
 * @param {string|null|undefined} style
 */
NAPI_METHOD(Feature::setStyleString) {
  Feature *feature = node_gdal::UnwrapWrapped<Feature>(info.This().As<Napi::Object>());
  if (!feature->isAlive()) {
    Napi::Error::New(node_gdal::napi_env(), "Feature object already destroyed").ThrowAsJavaScriptException();
    return node_gdal::napi_env().Undefined();
  }

  if (info.Length() < 1 || info[0].IsNull() || info[0].IsUndefined()) {
    // Clear style if null/undefined or no arg
    feature->this_->SetStyleString(nullptr);
    return node_gdal::napi_env().Undefined();
  }

  if (!info[0].IsString()) {
    Napi::TypeError::New(node_gdal::napi_env(), "style must be a string, null or undefined").ThrowAsJavaScriptException();
    return node_gdal::napi_env().Undefined();
  }

  std::string utf8 = info[0].As<Napi::String>().Utf8Value();
  feature->this_->SetStyleString(utf8.c_str());
  return node_gdal::napi_env().Undefined();
}

NAPI_SETTER(Feature::fidSetter) {
  Feature *feature = node_gdal::UnwrapWrapped<Feature>(info.This().As<Napi::Object>());
  if (!feature->isAlive()) {
    Napi::Error::New(node_gdal::napi_env(), "Feature object already destroyed").ThrowAsJavaScriptException();
    return;
  }
  if (!value.IsNumber()) {
    Napi::Error::New(node_gdal::napi_env(), "fid must be an integer").ThrowAsJavaScriptException();
    return;
  }
  feature->this_->SetFID(value.As<Napi::Number>().Int64Value());
}

} // namespace node_gdal
