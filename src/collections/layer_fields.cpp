#include "layer_fields.hpp"
#include "../utils/args.hpp"
#include "../gdal_common.hpp"
#include "../gdal_field_defn.hpp"
#include "../gdal_layer.hpp"

namespace node_gdal {

Napi::FunctionReference &LayerFields::constructor() {
  return node_gdal::env_context().constructor<LayerFields>();
}

LayerFields::LayerFields(const Napi::CallbackInfo &info) : GDALObject<LayerFields>(info) {
  if (info.Length() < 1 || !info[0].IsObject()) {
    Napi::Error::New(info.Env(), "Cannot create LayerFields directly").ThrowAsJavaScriptException();
    return;
  }
  node_gdal::set_private(info.This(), "parent_", info[0]);
}

LayerFields::~LayerFields() {
}

/**
 * @class LayerFields
 */

Napi::Value LayerFields::New(Napi::Value layer_obj) {

  std::vector<napi_value> args = {layer_obj};
  Napi::Object obj = LayerFields::constructor().Value().New(args);

  return obj;
}

/**
 * Returns the number of fields.
 *
 * @method count
 * @instance
 * @memberof LayerFields
 * @return {number}
 */
/**
 * Find the index of field in the layer.
 *
 * @method indexOf
 * @instance
 * @memberof LayerFields
 * @param {string} field
 * @return {number} Field index, or -1 if the field doesn't exist
 */
/**
 * Returns a field definition.
 *
 * @throws {Error}
 * @method get
 * @instance
 * @memberof LayerFields
 * @param {string|number} field Field name or index (0-based)
 * @return {FieldDefn}
 */
/**
 * Returns a list of field names.
 *
 * @throws {Error}
 * @method getNames
 * @instance
 * @memberof LayerFields
 * @return {string[]} List of strings.
 */
/**
 * Removes a field.
 *
 * @throws {Error}
 * @method remove
 * @instance
 * @memberof LayerFields
 * @param {string|number} field Field name or index (0-based)
 */
/**
 * Adds field(s).
 *
 * @throws {Error}
 * @method add
 * @instance
 * @memberof LayerFields
 * @param {FieldDefn|FieldDefn[]} defs A field definition, or array of field
 * definitions.
 * @param {boolean} [approx=true]
 */
/**
 * Reorders fields.
 *
 * @example
 *
 * // reverse field order
 * layer.fields.reorder([2,1,0]);
 *
 * @throws {Error}
 * @method reorder
 * @instance
 * @memberof LayerFields
 * @param {number[]} map An array of new indexes (integers)
 */
/**
 * Returns the parent layer.
 *
 * @readonly
 * @kind member
 * @name layer
 * @instance
 * @memberof LayerFields
 * @type {Layer}
 */
namespace {

Napi::Value toString(const Napi::CallbackInfo &info) {
  return Napi::String::New(node_gdal::napi_env(), "LayerFields");
}


Napi::Value count(const Napi::CallbackInfo &info) {

  Napi::Object parent =
    node_gdal::get_private(info.This(), "parent_").As<Napi::Object>();
  Layer *layer = node_gdal::UnwrapWrapped<Layer>(parent);
  if (!layer->isAlive()) {
    Napi::Error::New(node_gdal::napi_env(), "Layer object already destroyed").ThrowAsJavaScriptException();
    return node_gdal::napi_env().Undefined();
  }

  OGRFeatureDefn *def = layer->get()->GetLayerDefn();
  if (!def) {
    Napi::Error::New(node_gdal::napi_env(), "Layer has no layer definition set").ThrowAsJavaScriptException();
    return node_gdal::napi_env().Undefined();
  }

  return Napi::Number::New(node_gdal::napi_env(), def->GetFieldCount());
}


Napi::Value indexOf(const Napi::CallbackInfo &info) {
  Args args(info);

  Napi::Object parent =
    node_gdal::get_private(info.This(), "parent_").As<Napi::Object>();
  Layer *layer = node_gdal::UnwrapWrapped<Layer>(parent);
  if (!layer->isAlive()) {
    Napi::Error::New(node_gdal::napi_env(), "Layer object already destroyed").ThrowAsJavaScriptException();
    return node_gdal::napi_env().Undefined();
  }

  OGRFeatureDefn *def = layer->get()->GetLayerDefn();
  if (!def) {
    Napi::Error::New(node_gdal::napi_env(), "Layer has no layer definition set").ThrowAsJavaScriptException();
    return node_gdal::napi_env().Undefined();
  }

  std::string name("");
  if (!args.get(0, "field name", name)) return args.failed();

  return Napi::Number::New(node_gdal::napi_env(), def->GetFieldIndex(name.c_str()));
}


Napi::Value get(const Napi::CallbackInfo &info) {

  Napi::Object parent =
    node_gdal::get_private(info.This(), "parent_").As<Napi::Object>();
  Layer *layer = node_gdal::UnwrapWrapped<Layer>(parent);
  if (!layer->isAlive()) {
    Napi::Error::New(node_gdal::napi_env(), "Layer object already destroyed").ThrowAsJavaScriptException();
    return node_gdal::napi_env().Undefined();
  }

  if (info.Length() < 1) {
    Napi::Error::New(node_gdal::napi_env(), "Field index or name must be given").ThrowAsJavaScriptException();
    return node_gdal::napi_env().Undefined();
  }

  OGRFeatureDefn *def = layer->get()->GetLayerDefn();
  if (!def) {
    Napi::Error::New(node_gdal::napi_env(), "Layer has no layer definition set").ThrowAsJavaScriptException();
    return node_gdal::napi_env().Undefined();
  }

  int field_index = node_gdal::parse_field_index(info[0], def);

  auto r = def->GetFieldDefn(field_index);
  if (r == nullptr) {
    throw GDALError::fromGDAL();
    return node_gdal::napi_env().Undefined();
  }
  return FieldDefn::New(r);
}


Napi::Value getNames(const Napi::CallbackInfo &info) {

  Napi::Object parent =
    node_gdal::get_private(info.This(), "parent_").As<Napi::Object>();
  Layer *layer = node_gdal::UnwrapWrapped<Layer>(parent);
  if (!layer->isAlive()) {
    Napi::Error::New(node_gdal::napi_env(), "Layer object already destroyed").ThrowAsJavaScriptException();
    return node_gdal::napi_env().Undefined();
  }

  OGRFeatureDefn *def = layer->get()->GetLayerDefn();
  if (!def) {
    Napi::Error::New(node_gdal::napi_env(), "Layer has no layer definition set").ThrowAsJavaScriptException();
    return node_gdal::napi_env().Undefined();
  }

  int n = def->GetFieldCount();
  Napi::Array result = Napi::Array::New(node_gdal::napi_env(), n);

  for (int i = 0; i < n; i++) {
    OGRFieldDefn *field_def = def->GetFieldDefn(i);
    result.Set( i, SafeString::New(field_def->GetNameRef()));
  }

  return result;
}


Napi::Value remove(const Napi::CallbackInfo &info) {

  Napi::Object parent =
    node_gdal::get_private(info.This(), "parent_").As<Napi::Object>();
  Layer *layer = node_gdal::UnwrapWrapped<Layer>(parent);
  if (!layer->isAlive()) {
    Napi::Error::New(node_gdal::napi_env(), "Layer object already destroyed").ThrowAsJavaScriptException();
    return node_gdal::napi_env().Undefined();
  }

  if (info.Length() < 1) {
    Napi::Error::New(node_gdal::napi_env(), "Field index or name must be given").ThrowAsJavaScriptException();
    return node_gdal::napi_env().Undefined();
  }

  OGRFeatureDefn *def = layer->get()->GetLayerDefn();
  if (!def) {
    Napi::Error::New(node_gdal::napi_env(), "Layer has no layer definition set").ThrowAsJavaScriptException();
    return node_gdal::napi_env().Undefined();
  }

  int field_index = node_gdal::parse_field_index(info[0], def);

  int err = layer->get()->DeleteField(field_index);
  if (err) {
    throw GDALError(getOGRErrMsg(err));
    return node_gdal::napi_env().Undefined();
  }

  return node_gdal::napi_env().Undefined();
}


Napi::Value add(const Napi::CallbackInfo &info) {
  Args args(info);

  Napi::Object parent =
    node_gdal::get_private(info.This(), "parent_").As<Napi::Object>();
  Layer *layer = node_gdal::UnwrapWrapped<Layer>(parent);
  if (!layer->isAlive()) {
    Napi::Error::New(node_gdal::napi_env(), "Layer object already destroyed").ThrowAsJavaScriptException();
    return node_gdal::napi_env().Undefined();
  }
  if (info.Length() < 1) {
    Napi::Error::New(node_gdal::napi_env(), "field definition(s) must be given").ThrowAsJavaScriptException();
    return node_gdal::napi_env().Undefined();
  }

  FieldDefn *field_def;
  int err;
  int approx = 1;
  if (!args.optional(1, "approx", approx)) return args.failed();

  if (info[0].IsArray()) {
    Napi::Array array = info[0].As<Napi::Array>();
    int n = array.Length();
    for (int i = 0; i < n; i++) {
      Napi::Value element = array.As<Napi::Object>().Get(i);
      if (node_gdal::IsInstanceOf<FieldDefn>(element)) {
        field_def = node_gdal::UnwrapWrapped<FieldDefn>(element.As<Napi::Object>());
        err = layer->get()->CreateField(field_def->get(), approx);
        if (err) {
          throw GDALError(getOGRErrMsg(err));
          return node_gdal::napi_env().Undefined();
        }
      } else {
        Napi::Error::New(node_gdal::napi_env(), "All array elements must be FieldDefn objects").ThrowAsJavaScriptException();
        return node_gdal::napi_env().Undefined();
      }
    }
  } else if (node_gdal::IsInstanceOf<FieldDefn>(info[0])) {
    field_def = node_gdal::UnwrapWrapped<FieldDefn>(info[0].As<Napi::Object>());
    err = layer->get()->CreateField(field_def->get(), approx);
    if (err) {
      throw GDALError(getOGRErrMsg(err));
      return node_gdal::napi_env().Undefined();
    }
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
  Layer *layer = node_gdal::UnwrapWrapped<Layer>(parent);
  if (!layer->isAlive()) {
    Napi::Error::New(node_gdal::napi_env(), "Layer object already destroyed").ThrowAsJavaScriptException();
    return node_gdal::napi_env().Undefined();
  }

  OGRFeatureDefn *def = layer->get()->GetLayerDefn();
  if (!def) {
    Napi::Error::New(node_gdal::napi_env(), "Layer has no layer definition set").ThrowAsJavaScriptException();
    return node_gdal::napi_env().Undefined();
  }

  Napi::Array field_map = Napi::Array::New(node_gdal::napi_env(), 0);
  if (!args.get(0, "field map", field_map)) return args.failed();

  int n = def->GetFieldCount();
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

  err = layer->get()->ReorderFields(field_map_array);

  delete[] field_map_array;

  if (err) {
    throw GDALError(getOGRErrMsg(err));
    return node_gdal::napi_env().Undefined();
  }
  return node_gdal::napi_env().Undefined();
}


Napi::Value layerGetter(const Napi::CallbackInfo &info) {
  return node_gdal::get_private(info.This(), "parent_");
}

} // namespace

void LayerFields::Initialize(Napi::Object target) {
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
    hiddenAttr("layer", layerGetter, READ_ONLY_SETTER),
  };

  Napi::Function lcons = GDALDefineClass<LayerFields>(env, "LayerFields", methods, accessors);

  // method("alter", alter),

  target.Set("LayerFields", lcons);

  constructor() = Napi::Persistent(lcons);
}

} // namespace node_gdal
