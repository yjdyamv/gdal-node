
#include "gdal_field_defn.hpp"
#include "gdal_common.hpp"
#include "utils/field_types.hpp"

namespace node_gdal {

namespace {
// The wrapper owns the field definition it holds, the factory either copies
// the one of a layer or is handed a fresh one
void free_field_defn(void *ptr) {
  delete static_cast<OGRFieldDefn *>(ptr);
}
} // namespace

Napi::FunctionReference &FieldDefn::constructor() {
  return node_gdal::env_context().constructor<FieldDefn>();
}

/**
 * A field definition of a {@link FeatureDefn}.
 *
 * @constructor
 * @class FieldDefn
 * @param {string} name The name of the field
 * @param {string} type The type of the field (see {@link OFT|OFT constants})
 */
FieldDefn::FieldDefn(const Napi::CallbackInfo &info) : GDALObject<FieldDefn>(info) {
  Handle<OGRFieldDefn> pending = node_gdal::TakePendingHandle<OGRFieldDefn>();
  if (pending) {
    this_ = std::move(pending);
    LOG("Created FieldDefn [%p]", this_);
    return;
  }

  // Constructed from JS: FieldDefn(name, type). The NODE_ARG_* macros cannot be
  // used here - they return a value on error, which a constructor cannot do
  if (info.Length() < 2 || !info[0].IsString() || !info[1].IsString()) {
    Napi::Error::New(info.Env(), "FieldDefn requires a field name and a field type").ThrowAsJavaScriptException();
    return;
  }
  std::string field_name = info[0].As<Napi::String>().Utf8Value();
  std::string type_name = info[1].As<Napi::String>().Utf8Value();

  int field_type = getFieldTypeByName(type_name);
  if (field_type < 0) {
    Napi::Error::New(info.Env(), "Unrecognized field type").ThrowAsJavaScriptException();
    return;
  }

  this_ = Handle<OGRFieldDefn>::owned(
    new OGRFieldDefn(field_name.c_str(), static_cast<OGRFieldType>(field_type)), &free_field_defn);
}

FieldDefn::~FieldDefn() {
  // The handle frees the field definition, if this wrapper owns it
  LOG("Disposed FieldDefn [%p]", this_.get());
}

// The field definition of a layer or of a feature cannot be handed out: it
// belongs to its parent and modifying it would modify the parent without the
// parent knowing about it. The factory hands out a copy which is read-only
Napi::Value FieldDefn::New(const OGRFieldDefn *def) {
  if (!def) { return node_gdal::napi_env().Null(); }
  OGRFieldDefn *copy = new OGRFieldDefn(def);
  Napi::Value obj = FieldDefn::New(copy, true);
  node_gdal::set_read_only(obj.As<Napi::Object>());
  return obj;
}

Napi::Value FieldDefn::New(OGRFieldDefn *def, bool owned) {

  if (!def) { return node_gdal::napi_env().Null(); }
  if (!owned) { def = new OGRFieldDefn(def); }

  // The pointer is always ours: it is either the copy made above or the one the
  // caller handed over
  node_gdal::detail::PendingObjectGuard pending(def, &free_field_defn);
  std::vector<napi_value> args;
  return FieldDefn::constructor().Value().New(args);
}

/**
 * @kind member
 * @name name
 * @instance
 * @memberof FieldDefn
 * @type {string}
 */
/**
 * Data type (see {@link OFT|OFT constants}
 *
 * @kind member
 * @name type
 * @instance
 * @memberof FieldDefn
 * @type {string}
 */
/**
 * @kind member
 * @name ignored
 * @instance
 * @memberof FieldDefn
 * @type {boolean}
 */
/**
 * Field justification (see {@link OJ|OJ constants})
 *
 * @kind member
 * @name justification
 * @instance
 * @memberof FieldDefn
 * @type {string}
 */
/**
 * @kind member
 * @name width
 * @instance
 * @memberof FieldDefn
 * @type {number}
 */
/**
 * @kind member
 * @name precision
 * @instance
 * @memberof FieldDefn
 * @type {number}
 */
namespace {

Napi::Value toString(const Napi::CallbackInfo &info) {
  return Napi::String::New(node_gdal::napi_env(), "FieldDefn");
}


Napi::Value nameGetter(const Napi::CallbackInfo &info) {
  FieldDefn *def = node_gdal::UnwrapWrapped<FieldDefn>(info.This().As<Napi::Object>());
  return SafeString::New(def->get()->GetNameRef());
}


Napi::Value typeGetter(const Napi::CallbackInfo &info) {
  FieldDefn *def = node_gdal::UnwrapWrapped<FieldDefn>(info.This().As<Napi::Object>());
  return SafeString::New(getFieldTypeName(def->get()->GetType()));
}


Napi::Value ignoredGetter(const Napi::CallbackInfo &info) {
  FieldDefn *def = node_gdal::UnwrapWrapped<FieldDefn>(info.This().As<Napi::Object>());
  return Napi::Boolean::New(node_gdal::napi_env(), def->get()->IsIgnored());
}


Napi::Value justificationGetter(const Napi::CallbackInfo &info) {
  FieldDefn *def = node_gdal::UnwrapWrapped<FieldDefn>(info.This().As<Napi::Object>());
  OGRJustification justification = def->get()->GetJustify();
  if (justification == OJRight) {
    return Napi::String::New(node_gdal::napi_env(), "Right");
    return node_gdal::napi_env().Undefined();
  }
  if (justification == OJLeft) {
    return Napi::String::New(node_gdal::napi_env(), "Left");
    return node_gdal::napi_env().Undefined();
  }
  return node_gdal::napi_env().Undefined();
}


Napi::Value widthGetter(const Napi::CallbackInfo &info) {
  FieldDefn *def = node_gdal::UnwrapWrapped<FieldDefn>(info.This().As<Napi::Object>());
  return Napi::Number::New(node_gdal::napi_env(), def->get()->GetWidth());
}


Napi::Value precisionGetter(const Napi::CallbackInfo &info) {
  FieldDefn *def = node_gdal::UnwrapWrapped<FieldDefn>(info.This().As<Napi::Object>());
  return Napi::Number::New(node_gdal::napi_env(), def->get()->GetPrecision());
}


void nameSetter(const Napi::CallbackInfo &info, const Napi::Value &value) {
  FieldDefn *def = node_gdal::UnwrapWrapped<FieldDefn>(info.This().As<Napi::Object>());
  if (node_gdal::is_read_only(info.This())) {
    node_gdal::throw_read_only(info.Env(), "A FieldDefn retrieved from a layer, a feature definition or a feature");
    return;
  }
  if (!value.IsString()) {
    Napi::Error::New(node_gdal::napi_env(), "Name must be string").ThrowAsJavaScriptException();
    return;
  }
  std::string name = value.As<Napi::String>().Utf8Value();
  def->get()->SetName(name.c_str());
}


void typeSetter(const Napi::CallbackInfo &info, const Napi::Value &value) {
  FieldDefn *def = node_gdal::UnwrapWrapped<FieldDefn>(info.This().As<Napi::Object>());
  if (node_gdal::is_read_only(info.This())) {
    node_gdal::throw_read_only(info.Env(), "A FieldDefn retrieved from a layer, a feature definition or a feature");
    return;
  }
  if (!value.IsString()) {
    Napi::Error::New(node_gdal::napi_env(), "type must be a string").ThrowAsJavaScriptException();
    return;
  }
  std::string name = value.As<Napi::String>().Utf8Value();
  int type = getFieldTypeByName(name.c_str());
  if (type < 0) {
    Napi::Error::New(node_gdal::napi_env(), "Unrecognized field type").ThrowAsJavaScriptException();
  } else {
    def->get()->SetType(OGRFieldType(type));
  }
}


void justificationSetter(const Napi::CallbackInfo &info, const Napi::Value &value) {
  FieldDefn *def = node_gdal::UnwrapWrapped<FieldDefn>(info.This().As<Napi::Object>());
  if (node_gdal::is_read_only(info.This())) {
    node_gdal::throw_read_only(info.Env(), "A FieldDefn retrieved from a layer, a feature definition or a feature");
    return;
  }

  OGRJustification justification;
  if (value.IsString()) {
    const std::string str = value.As<Napi::String>().Utf8Value();
    if (str == "Left") {
      justification = OJLeft;
    } else if (str == "Right") {
      justification = OJRight;
    } else if (str == "Undefined") {
      justification = OJUndefined;
    } else {
      Napi::Error::New(node_gdal::napi_env(), "Unrecognized justification").ThrowAsJavaScriptException();
      return;
    }
  } else if (value.IsNull() || value.IsUndefined()) {
    justification = OJUndefined;
  } else {
    Napi::Error::New(node_gdal::napi_env(), "justification must be a string or undefined").ThrowAsJavaScriptException();
    return;
  }

  def->get()->SetJustify(justification);
}


void widthSetter(const Napi::CallbackInfo &info, const Napi::Value &value) {
  FieldDefn *def = node_gdal::UnwrapWrapped<FieldDefn>(info.This().As<Napi::Object>());
  if (node_gdal::is_read_only(info.This())) {
    node_gdal::throw_read_only(info.Env(), "A FieldDefn retrieved from a layer, a feature definition or a feature");
    return;
  }
  if (!value.IsNumber()) {
    Napi::Error::New(node_gdal::napi_env(), "width must be an integer").ThrowAsJavaScriptException();
    return;
  }
  def->get()->SetWidth(value.As<Napi::Number>().Int64Value());
}


void precisionSetter(const Napi::CallbackInfo &info, const Napi::Value &value) {
  FieldDefn *def = node_gdal::UnwrapWrapped<FieldDefn>(info.This().As<Napi::Object>());
  if (node_gdal::is_read_only(info.This())) {
    node_gdal::throw_read_only(info.Env(), "A FieldDefn retrieved from a layer, a feature definition or a feature");
    return;
  }
  if (!value.IsNumber()) {
    Napi::Error::New(node_gdal::napi_env(), "precision must be an integer").ThrowAsJavaScriptException();
    return;
  }
  def->get()->SetPrecision(value.As<Napi::Number>().Int64Value());
}


void ignoredSetter(const Napi::CallbackInfo &info, const Napi::Value &value) {
  FieldDefn *def = node_gdal::UnwrapWrapped<FieldDefn>(info.This().As<Napi::Object>());
  if (node_gdal::is_read_only(info.This())) {
    node_gdal::throw_read_only(info.Env(), "A FieldDefn retrieved from a layer, a feature definition or a feature");
    return;
  }
  if (!value.IsBoolean()) {
    Napi::Error::New(node_gdal::napi_env(), "ignored must be a boolean").ThrowAsJavaScriptException();
    return;
  }
  def->get()->SetIgnored(value.As<Napi::Boolean>().Value());
}

} // namespace

void FieldDefn::Initialize(Napi::Object target) {
  Napi::Env env = target.Env();
  static constexpr MethodEntry methods[] = {
    method("toString", toString),
  };

  static constexpr AccessorEntry accessors[] = {
    attr("name", nameGetter, nameSetter),
    attr("type", typeGetter, typeSetter),
    attr("justification", justificationGetter, justificationSetter),
    attr("width", widthGetter, widthSetter),
    attr("precision", precisionGetter, precisionSetter),
    attr("ignored", ignoredGetter, ignoredSetter),
  };

  Napi::Function lcons = GDALDefineClass<FieldDefn>(env, "FieldDefn", methods, accessors);

  target.Set("FieldDefn", lcons);

  constructor() = Napi::Persistent(lcons);
}

} // namespace node_gdal
