#include "feature_defn_fields.hpp"
#include "../utils/args.hpp"
#include "../gdal_common.hpp"
#include "../gdal_feature_defn.hpp"
#include "../gdal_field_defn.hpp"

namespace node_gdal {

Napi::FunctionReference &FeatureDefnFields::constructor() {
  return node_gdal::env_context().constructor<FeatureDefnFields>();
}

FeatureDefnFields::FeatureDefnFields(const Napi::CallbackInfo &info) : GDALObject<FeatureDefnFields>(info) {
  if (info.Length() < 1 || !info[0].IsObject()) {
    Napi::Error::New(info.Env(), "Cannot create FeatureDefnFields directly").ThrowAsJavaScriptException();
    return;
  }
  node_gdal::set_private(info.This(), "parent_", info[0]);
}

FeatureDefnFields::~FeatureDefnFields() {
}

/**
 * An encapsulation of a {@link FeatureDefn}'s fields.
 *
 * @class FeatureDefnFields
 */

Napi::Value FeatureDefnFields::New(Napi::Value feature_defn) {

  std::vector<napi_value> args = {feature_defn};
  Napi::Object obj = FeatureDefnFields::constructor().Value().New(args);

  return obj;
}

/**
 * Returns the number of fields.
 *
 * @method count
 * @instance
 * @memberof FeatureDefnFields
 * @return {number}
 */
/**
 * Returns the index of field definition.
 *
 * @method indexOf
 * @instance
 * @memberof FeatureDefnFields
 * @param {string} name
 * @return {number} Index or `-1` if not found.
 */
/**
 * Returns a field definition.
 *
 * @method get
 * @instance
 * @memberof FeatureDefnFields
 * @param {string|number} key Field name or index
 * @throws {Error}
 * @return {FieldDefn}
 */
/**
 * Returns a list of field names.
 *
 * @method getNames
 * @instance
 * @memberof FeatureDefnFields
 * @return {string[]} List of field names.
 */
/**
 * Removes a field definition.
 *
 * @method remove
 * @instance
 * @memberof FeatureDefnFields
 * @throws {Error}
 * @param {string|number} key Field name or index
 */
/**
 * Adds field definition(s).
 *
 * @method add
 * @instance
 * @memberof FeatureDefnFields
 * @throws {Error}
 * @param {FieldDefn|FieldDefn[]} fields
 */
/**
 * Reorders the fields.
 *
 * @example
 *
 * // reverse fields:
 * featureDef.fields.reorder([2, 1, 0]);
 *
 * @method reorder
 * @instance
 * @memberof FeatureDefnFields
 * @throws {Error}
 * @param {number[]} map An array representing the new field order.
 */
/**
 * Returns the parent feature definition.
 *
 * @readonly
 * @kind member
 * @name featureDefn
 * @instance
 * @memberof FeatureDefnFields
 * @type {FeatureDefn}
 */
namespace {

Napi::Value toString(const Napi::CallbackInfo &info) {
  return Napi::String::New(node_gdal::napi_env(), "FeatureDefnFields");
}


Napi::Value count(const Napi::CallbackInfo &info) {

  Napi::Object parent =
    node_gdal::get_private(info.This(), "parent_").As<Napi::Object>();
  FeatureDefn *feature_def = node_gdal::unwrap_or_throw<FeatureDefn>(parent, "FeatureDefn");

  return Napi::Number::New(node_gdal::napi_env(), feature_def->get()->GetFieldCount());
}


Napi::Value indexOf(const Napi::CallbackInfo &info) {
  Args args(info);

  Napi::Object parent =
    node_gdal::get_private(info.This(), "parent_").As<Napi::Object>();
  FeatureDefn *feature_def = node_gdal::unwrap_or_throw<FeatureDefn>(parent, "FeatureDefn");

  std::string name("");
  if (!args.get(0, "field name", name)) return args.failed();

  return Napi::Number::New(node_gdal::napi_env(), feature_def->get()->GetFieldIndex(name.c_str()));
}


Napi::Value get(const Napi::CallbackInfo &info) {

  Napi::Object parent =
    node_gdal::get_private(info.This(), "parent_").As<Napi::Object>();
  FeatureDefn *feature_def = node_gdal::unwrap_or_throw<FeatureDefn>(parent, "FeatureDefn");

  if (info.Length() < 1) {
    Napi::Error::New(node_gdal::napi_env(), "Field index or name must be given").ThrowAsJavaScriptException();
    return node_gdal::napi_env().Undefined();
  }

  int field_index = node_gdal::parse_field_index(info[0], feature_def->get());

  CPLErrorReset();
  auto r = feature_def->get()->GetFieldDefn(field_index);
  if (r == nullptr) { throw GDALError::fromGDAL(); }
  return FieldDefn::New(r);
}


Napi::Value getNames(const Napi::CallbackInfo &info) {

  Napi::Object parent =
    node_gdal::get_private(info.This(), "parent_").As<Napi::Object>();
  FeatureDefn *feature_def = node_gdal::unwrap_or_throw<FeatureDefn>(parent, "FeatureDefn");

  int n = feature_def->get()->GetFieldCount();
  Napi::Array result = Napi::Array::New(node_gdal::napi_env(), n);

  for (int i = 0; i < n; i++) {
    OGRFieldDefn *field_def = feature_def->get()->GetFieldDefn(i);
    result.Set( i, SafeString::New(field_def->GetNameRef()));
  }

  return result;
}


Napi::Value remove(const Napi::CallbackInfo &info) {

  Napi::Object parent =
    node_gdal::get_private(info.This(), "parent_").As<Napi::Object>();
  FeatureDefn *feature_def = node_gdal::unwrap_or_throw<FeatureDefn>(parent, "FeatureDefn");
  if (node_gdal::is_read_only(parent)) {
    node_gdal::throw_read_only(info.Env(), "A FeatureDefn retrieved from a feature");
    return node_gdal::napi_env().Undefined();
  }

  if (info.Length() < 1) {
    Napi::Error::New(node_gdal::napi_env(), "Field index or name must be given").ThrowAsJavaScriptException();
    return node_gdal::napi_env().Undefined();
  }

  int field_index = node_gdal::parse_field_index(info[0], feature_def->get());

  int err = feature_def->get()->DeleteFieldDefn(field_index);
  if (err) {
    throw GDALError(getOGRErrMsg(err));
    return node_gdal::napi_env().Undefined();
  }

  return node_gdal::napi_env().Undefined();
}


Napi::Value add(const Napi::CallbackInfo &info) {

  Napi::Object parent =
    node_gdal::get_private(info.This(), "parent_").As<Napi::Object>();
  FeatureDefn *feature_def = node_gdal::unwrap_or_throw<FeatureDefn>(parent, "FeatureDefn");
  if (node_gdal::is_read_only(parent)) {
    node_gdal::throw_read_only(info.Env(), "A FeatureDefn retrieved from a feature");
    return node_gdal::napi_env().Undefined();
  }
  if (info.Length() < 1) {
    Napi::Error::New(node_gdal::napi_env(), "field definition(s) must be given").ThrowAsJavaScriptException();
    return node_gdal::napi_env().Undefined();
  }

  FieldDefn *field_def;

  if (info[0].IsArray()) {
    Napi::Array array = info[0].As<Napi::Array>();
    int n = array.Length();
    for (int i = 0; i < n; i++) {
      Napi::Value element = array.As<Napi::Object>().Get(i);
      if (node_gdal::IsInstanceOf<FieldDefn>(element)) {
        field_def = node_gdal::UnwrapWrapped<FieldDefn>(element.As<Napi::Object>());
        feature_def->get()->AddFieldDefn(field_def->get());
      } else {
        Napi::Error::New(node_gdal::napi_env(), "All array elements must be FieldDefn objects").ThrowAsJavaScriptException();
        return node_gdal::napi_env().Undefined();
      }
    }
  } else if (node_gdal::IsInstanceOf<FieldDefn>(info[0])) {
    field_def = node_gdal::UnwrapWrapped<FieldDefn>(info[0].As<Napi::Object>());
    feature_def->get()->AddFieldDefn(field_def->get());
  } else {
    Napi::Error::New(node_gdal::napi_env(), "field definition(s) must be a FieldDefn object or array of FieldDefn objects").ThrowAsJavaScriptException();
    return node_gdal::napi_env().Undefined();
  }

  return node_gdal::napi_env().Undefined();
}


Napi::Value reorder(const Napi::CallbackInfo &info) {
  Args args(info);

  Napi::Object parent =
    node_gdal::get_private(info.This(), "parent_").As<Napi::Object>();
  FeatureDefn *feature_def = node_gdal::unwrap_or_throw<FeatureDefn>(parent, "FeatureDefn");
  if (node_gdal::is_read_only(parent)) {
    node_gdal::throw_read_only(info.Env(), "A FeatureDefn retrieved from a feature");
    return node_gdal::napi_env().Undefined();
  }

  Napi::Array field_map = Napi::Array::New(node_gdal::napi_env(), 0);
  if (!args.get(0, "field map", field_map)) return args.failed();

  int n = feature_def->get()->GetFieldCount();
  OGRErr err = 0;

  if ((int)field_map.Length() != n) {
    Napi::Error::New(node_gdal::napi_env(), "Array length must match field count").ThrowAsJavaScriptException();
    return node_gdal::napi_env().Undefined();
  }

  int *field_map_array = new int[n];

  for (int i = 0; i < n; i++) {
    Napi::Value val = field_map.As<Napi::Object>().Get(i);
    if (!val.IsNumber()) {
      delete[] field_map_array;
      Napi::Error::New(node_gdal::napi_env(), "Array must only contain integers").ThrowAsJavaScriptException();
      return node_gdal::napi_env().Undefined();
    }

    int key = val.As<Napi::Number>().Int64Value();
    if (key < 0 || key >= n) {
      delete[] field_map_array;
      Napi::Error::New(node_gdal::napi_env(), "Values must be between 0 and field count - 1").ThrowAsJavaScriptException();
      return node_gdal::napi_env().Undefined();
    }

    field_map_array[i] = key;
  }

  err = feature_def->get()->ReorderFieldDefns(field_map_array);

  delete[] field_map_array;

  if (err) {
    throw GDALError(getOGRErrMsg(err));
    return node_gdal::napi_env().Undefined();
  }
  return node_gdal::napi_env().Undefined();
}


Napi::Value featureDefnGetter(const Napi::CallbackInfo &info) {
  Napi::Value parent = node_gdal::get_private(info.This(), "parent_");
  node_gdal::requireAlive<FeatureDefn>(parent, "FeatureDefn");
  return parent;
}

} // namespace

void FeatureDefnFields::Initialize(Napi::Object target) {
  Napi::Env env = target.Env();
  static constexpr MethodEntry methods[] = {
    method("toString", toString),
    method("count", count),
    method("get", get),
    method("remove", remove),
    method("getNames", getNames),
    method("indexOf", indexOf),
    method("reorder", reorder),
    method("add", add),
  };

  static constexpr AccessorEntry accessors[] = {
    hiddenAttr("featureDefn", featureDefnGetter, READ_ONLY_SETTER),
  };

  Napi::Function lcons = GDALDefineClass<FeatureDefnFields>(env, "FeatureDefnFields", methods, accessors);

  // method("alter", alter),

  target.Set("FeatureDefnFields", lcons);

  constructor() = Napi::Persistent(lcons);
}

} // namespace node_gdal
