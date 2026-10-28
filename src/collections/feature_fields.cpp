#include "../gdal_common.hpp"
#include "../utils/args.hpp"
#include "../gdal_feature.hpp"
#include "feature_fields.hpp"

namespace node_gdal {

Napi::FunctionReference &FeatureFields::constructor() {
  return node_gdal::env_context().constructor<FeatureFields>();
}

FeatureFields::FeatureFields(const Napi::CallbackInfo &info) : GDALObject<FeatureFields>(info) {
  if (info.Length() < 1 || !info[0].IsObject()) {
    Napi::Error::New(info.Env(), "Cannot create FeatureFields directly").ThrowAsJavaScriptException();
    return;
  }
  node_gdal::set_private(info.This(), "parent_", info[0]);
}

FeatureFields::~FeatureFields() {
}

/**
 * An encapsulation of all field data that makes up a {@link Feature}.
 *
 * @class FeatureFields
 */

Napi::Value FeatureFields::New(Napi::Value layer_obj) {

  std::vector<napi_value> args = {layer_obj};
  Napi::Object obj = FeatureFields::constructor().Value().New(args);

  return obj;
}

// Reads the numeric values of a JS array or of a typed array
static bool toDoubleArray(Napi::Value val, std::vector<double> &values) {
  if (val.IsArray()) {
    Napi::Array array = val.As<Napi::Array>();
    for (uint32_t i = 0; i < array.Length(); i++) {
      Napi::Value el = array.Get(i);
      // anything which is not a number would be converted into one, into the
      // minimum of the type when the conversion is not finite, so it is
      // refused instead
      if (!el.IsNumber()) { return false; }
      values.push_back(el.As<Napi::Number>().DoubleValue());
    }
    return true;
  }

  if (val.IsTypedArray()) {
    napi_typedarray_type type;
    size_t length, offset;
    void *data;
    napi_value buffer;
    if (napi_get_typedarray_info(node_gdal::napi_env(), val, &type, &length, &data, &buffer, &offset) != napi_ok) {
      return false;
    }
    for (size_t i = 0; i < length; i++) {
      switch (type) {
        case napi_int8_array:
          values.push_back(static_cast<int8_t *>(data)[i]);
          break;
        case napi_uint8_array:
        case napi_uint8_clamped_array:
          values.push_back(static_cast<uint8_t *>(data)[i]);
          break;
        case napi_int16_array:
          values.push_back(static_cast<int16_t *>(data)[i]);
          break;
        case napi_uint16_array:
          values.push_back(static_cast<uint16_t *>(data)[i]);
          break;
        case napi_int32_array:
          values.push_back(static_cast<int32_t *>(data)[i]);
          break;
        case napi_uint32_array:
          values.push_back(static_cast<uint32_t *>(data)[i]);
          break;
        case napi_float32_array:
          values.push_back(static_cast<float *>(data)[i]);
          break;
        case napi_float64_array:
          values.push_back(static_cast<double *>(data)[i]);
          break;
        default:
          return false;
      }
    }
    return true;
  }

  return false;
}

// Sets a field of one of the list types from a JS array or typed array
inline bool setListField(OGRFeature *f, int field_index, Napi::Value val) {
  OGRFieldType type = f->GetFieldDefnRef(field_index)->GetType();

  if (type == OFTStringList) {
    if (!val.IsArray()) { return true; }
    Napi::Array array = val.As<Napi::Array>();
    // the strings have to outlive the call to OGR, which keeps the pointers
    std::vector<std::string> strings;
    std::vector<const char *> values;
    for (uint32_t i = 0; i < array.Length(); i++) {
      Napi::Value el = array.Get(i);
      if (!el.IsString()) { return true; }
      strings.push_back(el.As<Napi::String>().Utf8Value());
    }
    for (size_t i = 0; i < strings.size(); i++) { values.push_back(strings[i].c_str()); }
    // the list is null terminated and the count comes from it
    values.push_back(nullptr);
    f->SetField(field_index, values.data());
    return false;
  }

  std::vector<double> doubles;
  if (!toDoubleArray(val, doubles)) { return true; }

  switch (type) {
    case OFTIntegerList: {
      std::vector<int> values;
      for (size_t i = 0; i < doubles.size(); i++) { values.push_back(static_cast<int>(doubles[i])); }
      f->SetField(field_index, static_cast<int>(values.size()), values.data());
      return false;
    }
    case OFTInteger64List: {
      std::vector<GIntBig> values;
      for (size_t i = 0; i < doubles.size(); i++) { values.push_back(static_cast<GIntBig>(doubles[i])); }
      f->SetField(field_index, static_cast<int>(values.size()), values.data());
      return false;
    }
    case OFTRealList:
      f->SetField(field_index, static_cast<int>(doubles.size()), doubles.data());
      return false;
    default:
      return true;
  }
}

inline bool setField(OGRFeature *f, int field_index, Napi::Value val) {
  if (val.IsNumber()) {
    // V8's IsInt32 (what the original used) is "the value survives a round trip
    // through int32"; OGR keeps whatever precision the field type allows
    const double d = val.As<Napi::Number>().DoubleValue();
    const int32_t i = val.As<Napi::Number>().Int32Value();
    if (static_cast<double>(i) == d) {
      f->SetField(field_index, i);
    } else {
      f->SetField(field_index, d);
    }
  } else if (val.IsString()) {
    std::string str = val.As<Napi::String>().Utf8Value();
    f->SetField(field_index, str.c_str());
  } else if (val.IsNull() || val.IsUndefined()) {
    f->UnsetField(field_index);
  } else if (val.IsArray() || val.IsTypedArray()) {
    return setListField(f, field_index, val);
  } else {
    return true;
  }
  return false;
}

/**
 * Sets feature field(s).
 *
 * @example
 *
 * // most-efficient, least flexible. requires you to know the ordering of the
 * fields: feature.fields.set(['Something']); feature.fields.set(0,
 * 'Something');
 *
 * // most flexible.
 * feature.fields.set({name: 'Something'});
 * feature.fields.set('name', 'Something');
 *
 * // the list types take an array of values
 * feature.fields.set('names', [ 'Something', 'Else' ]);
 *
 *
 * @method set
 * @instance
 * @memberof FeatureFields
 * @throws {Error}
 * @param {string|number} key Field name or index
 * @param {any} value A number, a string, null or a list of them for a field of
 * one of the list types
 */

/**
 * @method set
 * @instance
 * @memberof FeatureFields
 * @throws {Error}
 * @param {object} fields
 */
/**
 * Resets all fields.
 *
 * @example
 *
 * feature.fields.reset();
 *
 * @method reset
 * @instance
 * @memberof FeatureFields
 * @throws {Error}
 * @param {object} [values]
 * @return {void}
 */
/**
 * Returns the number of fields.
 *
 * @example
 *
 * feature.fields.count();
 *
 * @method count
 * @instance
 * @memberof FeatureFields
 * @return {number}
 */
/**
 * Returns the index of a field, given its name.
 *
 * @example
 *
 * var index = feature.fields.indexOf('field');
 *
 * @method indexOf
 * @instance
 * @memberof FeatureFields
 * @param {string} name
 * @return {number} Index or, `-1` if it cannot be found.
 */
/**
 * Outputs the field data as a pure JS object.
 *
 * @throws {Error}
 * @method toObject
 * @instance
 * @memberof FeatureFields
 * @return {any}
 */
/**
 * Outputs the field values as a pure JS array.
 *
 * @throws {Error}
 * @method toArray
 * @instance
 * @memberof FeatureFields
 * @return {any[]}
 */
Napi::Value FeatureFields::get(OGRFeature *f, int field_index) {
  //throws

  if (field_index < 0 || field_index >= f->GetFieldCount()) throw GDALError("Invalid field");
  if (!f->IsFieldSet(field_index)) return node_gdal::napi_env().Null();
  if (f->IsFieldNull(field_index)) return node_gdal::napi_env().Null();

  const OGRFieldDefn *field_def = f->GetFieldDefnRef(field_index);
  switch (field_def->GetType()) {
    case OFTInteger: return Napi::Number::New(node_gdal::napi_env(), f->GetFieldAsInteger(field_index));
    case OFTInteger64: return Napi::Number::New(node_gdal::napi_env(), f->GetFieldAsInteger64(field_index));
    case OFTInteger64List: return getFieldAsInteger64List(f, field_index);
    case OFTReal: return Napi::Number::New(node_gdal::napi_env(), f->GetFieldAsDouble(field_index));
    case OFTString: return SafeString::New(f->GetFieldAsString(field_index));
    case OFTIntegerList: return getFieldAsIntegerList(f, field_index);
    case OFTRealList: return getFieldAsDoubleList(f, field_index);
    case OFTStringList: return getFieldAsStringList(f, field_index);
    case OFTBinary: return getFieldAsBinary(f, field_index);
    case OFTDate:
    case OFTTime:
    case OFTDateTime: return getFieldAsDateTime(f, field_index);
    default: throw GDALError("Unsupported field type");
  }
}

/**
 * Returns a field's value.
 *
 * @example
 *
 * value = feature.fields.get(0);
 * value = feature.fields.get('field');
 *
 * @method get
 * @instance
 * @memberof FeatureFields
 * @param {string|number} key Feature name or index.
 * @throws {Error}
 * @return {any}
 */
/**
 * Returns a list of field name.
 *
 * @method getNames
 * @instance
 * @memberof FeatureFields
 * @throws {Error}
 * @return {string[]} List of field names.
 */
Napi::Value FeatureFields::getFieldAsIntegerList(OGRFeature *feature, int field_index) {

  int count_of_values = 0;

  const int *values = feature->GetFieldAsIntegerList(field_index, &count_of_values);

  Napi::Array return_array = Napi::Array::New(node_gdal::napi_env(), count_of_values);

  for (int index = 0; index < count_of_values; index++) {
    return_array.Set( index, Napi::Number::New(node_gdal::napi_env(), values[index]));
  }

  return return_array;
}

Napi::Value FeatureFields::getFieldAsInteger64List(OGRFeature *feature, int field_index) {

  int count_of_values = 0;

  const long long *values = feature->GetFieldAsInteger64List(field_index, &count_of_values);

  Napi::Array return_array = Napi::Array::New(node_gdal::napi_env(), count_of_values);

  for (int index = 0; index < count_of_values; index++) {
    return_array.Set( index, Napi::Number::New(node_gdal::napi_env(), values[index]));
  }

  return return_array;
}

Napi::Value FeatureFields::getFieldAsDoubleList(OGRFeature *feature, int field_index) {

  int count_of_values = 0;

  const double *values = feature->GetFieldAsDoubleList(field_index, &count_of_values);

  Napi::Array return_array = Napi::Array::New(node_gdal::napi_env(), count_of_values);

  for (int index = 0; index < count_of_values; index++) {
    return_array.Set( index, Napi::Number::New(node_gdal::napi_env(), values[index]));
  }

  return return_array;
}

Napi::Value FeatureFields::getFieldAsStringList(OGRFeature *feature, int field_index) {
  char **values = feature->GetFieldAsStringList(field_index);

  int count_of_values = CSLCount(values);

  Napi::Array return_array = Napi::Array::New(node_gdal::napi_env(), count_of_values);

  for (int index = 0; index < count_of_values; index++) {
    return_array.Set( index, SafeString::New(values[index]));
  }

  return return_array;
}

Napi::Value FeatureFields::getFieldAsBinary(OGRFeature *feature, int field_index) {

  int count_of_bytes = 0;

  char *data = (char *)feature->GetFieldAsBinary(field_index, &count_of_bytes);

  if (count_of_bytes > 0) {
    // GDAL Feature->GetFieldAsBinary returns a pointer to an internal buffer
    // that should not be freed
    // The lifetime of this internal buffer does not match the lifetime of
    // the returned buffer
    // So we copy
    return Napi::Buffer<char>::Copy(node_gdal::napi_env(), data, count_of_bytes);
  }

  return node_gdal::napi_env().Undefined();
}

Napi::Value FeatureFields::getFieldAsDateTime(OGRFeature *feature, int field_index) {

  int year, month, day, hour, minute, second, timezone;

  year = month = day = hour = minute = second = timezone = 0;

  int result = feature->GetFieldAsDateTime(field_index, &year, &month, &day, &hour, &minute, &second, &timezone);

  if (result == TRUE) {
    Napi::Object hash = Napi::Object::New(node_gdal::napi_env());

    if (year) { hash.Set( Napi::String::New(node_gdal::napi_env(), "year"), Napi::Number::New(node_gdal::napi_env(), year)); }
    if (month) { hash.Set( Napi::String::New(node_gdal::napi_env(), "month"), Napi::Number::New(node_gdal::napi_env(), month)); }
    if (day) { hash.Set( Napi::String::New(node_gdal::napi_env(), "day"), Napi::Number::New(node_gdal::napi_env(), day)); }
    if (hour) { hash.Set( Napi::String::New(node_gdal::napi_env(), "hour"), Napi::Number::New(node_gdal::napi_env(), hour)); }
    if (minute) { hash.Set( Napi::String::New(node_gdal::napi_env(), "minute"), Napi::Number::New(node_gdal::napi_env(), minute)); }
    if (second) { hash.Set( Napi::String::New(node_gdal::napi_env(), "second"), Napi::Number::New(node_gdal::napi_env(), second)); }
    if (timezone) { hash.Set( Napi::String::New(node_gdal::napi_env(), "timezone"), Napi::Number::New(node_gdal::napi_env(), timezone)); }

    return hash;
  } else {
    return node_gdal::napi_env().Undefined();
  }
}

/**
 * Returns the parent feature.
 *
 * @readonly
 * @kind member
 * @name feature
 * @instance
 * @memberof FeatureFields
 * @type {Feature}
 */
namespace {

Napi::Value toString(const Napi::CallbackInfo &info) {
  return Napi::String::New(node_gdal::napi_env(), "FeatureFields");
}


Napi::Value set(const Napi::CallbackInfo &info) {
  int field_index;
  unsigned int i, n, n_fields_set;

  Napi::Object parent =
    node_gdal::get_private(info.This(), "parent_").As<Napi::Object>();
  Feature *f = node_gdal::unwrap_or_throw<Feature>(parent, "Feature");

  if (info.Length() == 1) {
    if (info[0].IsArray()) {
      // set([])
      Napi::Array values = info[0].As<Napi::Array>();

      n = f->get()->GetFieldCount();
      if (values.Length() < n) { n = values.Length(); }

      for (i = 0; i < n; i++) {
        Napi::Value val = values.As<Napi::Object>().Get(i);
        if (setField(f->get(), i, val)) {
          Napi::Error::New(node_gdal::napi_env(), "Unsupported type of field value").ThrowAsJavaScriptException();
          return node_gdal::napi_env().Undefined();
        }
      }

      return Napi::Number::New(node_gdal::napi_env(), n);
      return node_gdal::napi_env().Undefined();
    } else if (info[0].IsObject()) {
      // set({})
      Napi::Object values = info[0].As<Napi::Object>();

      n = f->get()->GetFieldCount();
      n_fields_set = 0;

      for (i = 0; i < n; i++) {
        // iterate through field names from field defn,
        // grabbing values from passed object, if not undefined

        const OGRFieldDefn *field_def = f->get()->GetFieldDefnRef(i);

        const char *field_name = field_def->GetNameRef();

        field_index = f->get()->GetFieldIndex(field_name);

        // skip value if field name doesnt exist
        // both in the feature definition and the passed object
        if (field_index == -1 || !values.As<Napi::Object>().HasOwnProperty(Napi::String::New(node_gdal::napi_env(), field_name))) {
          continue;
        }

        Napi::Value val = values.As<Napi::Object>().Get(Napi::String::New(node_gdal::napi_env(), field_name));
        if (setField(f->get(), field_index, val)) {
          Napi::Error::New(node_gdal::napi_env(), "Unsupported type of field value").ThrowAsJavaScriptException();
          return node_gdal::napi_env().Undefined();
        }

        n_fields_set++;
      }

      return Napi::Number::New(node_gdal::napi_env(), n_fields_set);
      return node_gdal::napi_env().Undefined();
    } else {
      Napi::Error::New(node_gdal::napi_env(), "Method expected an object or array").ThrowAsJavaScriptException();
      return node_gdal::napi_env().Undefined();
    }

  } else if (info.Length() == 2) {
    // set(name|index, value)
    field_index = node_gdal::parse_field_index(info[0], f->get());

    // set field value
    if (setField(f->get(), field_index, info[1])) {
      Napi::Error::New(node_gdal::napi_env(), "Unsupported type of field value").ThrowAsJavaScriptException();
      return node_gdal::napi_env().Undefined();
    }

    return Napi::Number::New(node_gdal::napi_env(), 1);
    return node_gdal::napi_env().Undefined();
  } else {
    Napi::Error::New(node_gdal::napi_env(), "Invalid number of arguments").ThrowAsJavaScriptException();
    return node_gdal::napi_env().Undefined();
  }
}


Napi::Value reset(const Napi::CallbackInfo &info) {
  int field_index;
  unsigned int i, n;

  Napi::Object parent =
    node_gdal::get_private(info.This(), "parent_").As<Napi::Object>();
  Feature *f = node_gdal::unwrap_or_throw<Feature>(parent, "Feature");

  n = f->get()->GetFieldCount();

  if (info.Length() == 0) {
    for (i = 0; i < n; i++) { f->get()->UnsetField(i); }
    return Napi::Number::New(node_gdal::napi_env(), n);
    return node_gdal::napi_env().Undefined();
  }

  if (!info[0].IsObject()) {
    Napi::Error::New(node_gdal::napi_env(), "fields must be an object").ThrowAsJavaScriptException();
    return node_gdal::napi_env().Undefined();
  }

  Napi::Object values = info[0].As<Napi::Object>();

  for (i = 0; i < n; i++) {
    // iterate through field names from field defn,
    // grabbing values from passed object

    const OGRFieldDefn *field_def = f->get()->GetFieldDefnRef(i);

    const char *field_name = field_def->GetNameRef();

    field_index = f->get()->GetFieldIndex(field_name);
    if (field_index == -1) continue;

    Napi::Value val = values.As<Napi::Object>().Get(Napi::String::New(node_gdal::napi_env(), field_name));
    if (setField(f->get(), field_index, val)) {
      Napi::Error::New(node_gdal::napi_env(), "Unsupported type of field value").ThrowAsJavaScriptException();
      return node_gdal::napi_env().Undefined();
    }
  }

  return Napi::Number::New(node_gdal::napi_env(), n);
}


Napi::Value count(const Napi::CallbackInfo &info) {

  Napi::Object parent =
    node_gdal::get_private(info.This(), "parent_").As<Napi::Object>();
  Feature *f = node_gdal::unwrap_or_throw<Feature>(parent, "Feature");

  return Napi::Number::New(node_gdal::napi_env(), f->get()->GetFieldCount());
}


Napi::Value indexOf(const Napi::CallbackInfo &info) {
  Args args(info);

  Napi::Object parent =
    node_gdal::get_private(info.This(), "parent_").As<Napi::Object>();
  Feature *f = node_gdal::unwrap_or_throw<Feature>(parent, "Feature");

  std::string name("");
  if (!args.get(0, "field name", name)) return args.failed();

  return Napi::Number::New(node_gdal::napi_env(), f->get()->GetFieldIndex(name.c_str()));
}


Napi::Value toObject(const Napi::CallbackInfo &info) {

  Napi::Object parent =
    node_gdal::get_private(info.This(), "parent_").As<Napi::Object>();
  Feature *f = node_gdal::unwrap_or_throw<Feature>(parent, "Feature");

  Napi::Object obj = Napi::Object::New(node_gdal::napi_env());

  int n = f->get()->GetFieldCount();
  for (int i = 0; i < n; i++) {

    // get field name
    const OGRFieldDefn *field_def = f->get()->GetFieldDefnRef(i);
    const char *key = field_def->GetNameRef();
    if (!key) {
      Napi::Error::New(node_gdal::napi_env(), "Error getting field name").ThrowAsJavaScriptException();
      return node_gdal::napi_env().Undefined();
    }

    // get field value
    try {
      Napi::Value val = FeatureFields::get(f->get(), i);
      obj.Set( Napi::String::New(node_gdal::napi_env(), key), val);
    } catch (const char *err) {
      Napi::Error::New(node_gdal::napi_env(), err).ThrowAsJavaScriptException();
      return node_gdal::napi_env().Undefined();
    }
  }
  return obj;
}


Napi::Value toArray(const Napi::CallbackInfo &info) {

  Napi::Object parent =
    node_gdal::get_private(info.This(), "parent_").As<Napi::Object>();
  Feature *f = node_gdal::unwrap_or_throw<Feature>(parent, "Feature");

  int n = f->get()->GetFieldCount();
  Napi::Array array = Napi::Array::New(node_gdal::napi_env(), n);

  for (int i = 0; i < n; i++) {
    // get field value
    try {
      Napi::Value val = FeatureFields::get(f->get(), i);
      array.Set( i, val);
    } catch (const char *err) {
      Napi::Error::New(node_gdal::napi_env(), err).ThrowAsJavaScriptException();
      return node_gdal::napi_env().Undefined();
    }
  }
  return array;
}


Napi::Value getField(const Napi::CallbackInfo &info) {

  Napi::Object parent =
    node_gdal::get_private(info.This(), "parent_").As<Napi::Object>();
  Feature *f = node_gdal::unwrap_or_throw<Feature>(parent, "Feature");

  if (info.Length() < 1) {
    Napi::Error::New(node_gdal::napi_env(), "Field index or name must be given").ThrowAsJavaScriptException();
    return node_gdal::napi_env().Undefined();
  }

  int field_index = node_gdal::parse_field_index(info[0], f->get());

  try {
    Napi::Value result = FeatureFields::get(f->get(), field_index);
    return result;
  } catch (const char *err) { Napi::Error::New(node_gdal::napi_env(), err).ThrowAsJavaScriptException(); }
  return node_gdal::napi_env().Undefined();
}


Napi::Value getNames(const Napi::CallbackInfo &info) {

  Napi::Object parent =
    node_gdal::get_private(info.This(), "parent_").As<Napi::Object>();
  Feature *f = node_gdal::unwrap_or_throw<Feature>(parent, "Feature");

  int n = f->get()->GetFieldCount();
  Napi::Array result = Napi::Array::New(node_gdal::napi_env(), n);

  for (int i = 0; i < n; i++) {

    // get field name
    const OGRFieldDefn *field_def = f->get()->GetFieldDefnRef(i);
    const char *field_name = field_def->GetNameRef();
    if (!field_name) {
      Napi::Error::New(node_gdal::napi_env(), "Error getting field name").ThrowAsJavaScriptException();
      return node_gdal::napi_env().Undefined();
    }
    result.Set( i, Napi::String::New(node_gdal::napi_env(), field_name));
  }

  return result;
}


Napi::Value featureGetter(const Napi::CallbackInfo &info) {
  Napi::Value parent = node_gdal::get_private(info.This(), "parent_");
  node_gdal::requireAlive<Feature>(parent, "Feature");
  return parent;
}

} // namespace

void FeatureFields::Initialize(Napi::Object target) {
  Napi::Env env = target.Env();
  static constexpr MethodEntry methods[] = {
    method("toString", toString),
    method("toObject", toObject),
    method("toArray", toArray),
    method("count", count),
    method("get", getField),
    method("getNames", getNames),
    method("set", set),
    method("reset", reset),
    method("indexOf", indexOf),
  };

  static constexpr AccessorEntry accessors[] = {
    hiddenAttr("feature", featureGetter, READ_ONLY_SETTER),
  };

  Napi::Function lcons = GDALDefineClass<FeatureFields>(env, "FeatureFields", methods, accessors);

  target.Set("FeatureFields", lcons);

  constructor() = Napi::Persistent(lcons);
}

} // namespace node_gdal
