#include "args.hpp"

namespace node_gdal {

// ----- the two shapes of a rejection -----

bool Args::rejectValue(const Napi::Value &value, const char *what, const char *expected) {
  failed_ = true;
  Napi::TypeError::New(env_, std::string(what) + " must be " + expected).ThrowAsJavaScriptException();
  (void) value;
  return false;
}

bool Args::rejectArgument(size_t num, const char *name, const char *expected) {
  return rejectType((*info_)[num], name, expected);
}

// "X must be an instance of Y", for the arguments which have to be one of the
// wrapped objects or one of the values of an enum
bool Args::rejectType(const Napi::Value &value, const char *what, const char *expected) {
  (void) value;
  failed_ = true;
  Napi::TypeError::New(env_, std::string(what) + " must be an instance of " + expected)
    .ThrowAsJavaScriptException();
  return false;
}

bool Args::rejectProperty(const char *key, const char *expected) {
  failed_ = true;
  Napi::TypeError::New(env_, std::string(key) + " must be an instance of " + expected)
    .ThrowAsJavaScriptException();
  return false;
}

bool Args::require(size_t num, const char *name) {
  if (failed_) { return false; }
  if (num < info_->Length()) { return true; }
  failed_ = true;
  Napi::Error::New(env_, std::string(name) + " must be given").ThrowAsJavaScriptException();
  return false;
}

// ----- required positional arguments -----

bool Args::get(size_t num, const char *name, int &out) {
  if (!require(num, name)) { return false; }
  // A flag may be given as a boolean even when it is kept in an int
  if ((*info_)[num].IsBoolean()) {
    out = (*info_)[num].As<Napi::Boolean>().Value() ? 1 : 0;
    return true;
  }
  if (!(*info_)[num].IsNumber()) { return rejectValue((*info_)[num], name, "an integer"); }
  out = static_cast<int>((*info_)[num].As<Napi::Number>().Int64Value());
  return true;
}

bool Args::get(size_t num, const char *name, unsigned int &out) {
  if (!require(num, name)) { return false; }
  if (!(*info_)[num].IsNumber()) { return rejectValue((*info_)[num], name, "an integer"); }
  out = (*info_)[num].As<Napi::Number>().Uint32Value();
  return true;
}

bool Args::get(size_t num, const char *name, int64_t &out) {
  if (!require(num, name)) { return false; }
  if (!(*info_)[num].IsNumber()) { return rejectValue((*info_)[num], name, "an integer"); }
  out = (*info_)[num].As<Napi::Number>().Int64Value();
  return true;
}
bool Args::get(size_t num, const char *name, short &out) {
  if (!require(num, name)) { return false; }
  if (!(*info_)[num].IsNumber()) { return rejectValue((*info_)[num], name, "an integer"); }
  out = static_cast<short>((*info_)[num].As<Napi::Number>().Int32Value());
  return true;
}
bool Args::get(size_t num, const char *name, double &out) {
  if (!require(num, name)) { return false; }
  if (!(*info_)[num].IsNumber()) { return rejectValue((*info_)[num], name, "a number"); }
  out = (*info_)[num].As<Napi::Number>().DoubleValue();
  return true;
}

bool Args::get(size_t num, const char *name, bool &out) {
  if (!require(num, name)) { return false; }
  if (!(*info_)[num].IsBoolean()) { return rejectValue((*info_)[num], name, "a boolean"); }
  out = (*info_)[num].As<Napi::Boolean>().Value();
  return true;
}

bool Args::get(size_t num, const char *name, std::string &out) {
  if (!require(num, name)) { return false; }
  if (!(*info_)[num].IsString()) { return rejectValue((*info_)[num], name, "a string"); }
  out = (*info_)[num].As<Napi::String>().Utf8Value();
  return true;
}

bool Args::get(size_t num, const char *name, Napi::Value &out) {
  if (!require(num, name)) { return false; }
  out = (*info_)[num];
  return true;
}

bool Args::get(size_t num, const char *name, Napi::Object &out) {
  if (!require(num, name)) { return false; }
  if (!(*info_)[num].IsObject()) { return rejectValue((*info_)[num], name, "an object"); }
  out = (*info_)[num].As<Napi::Object>();
  return true;
}

bool Args::get(size_t num, const char *name, Napi::Array &out) {
  if (!require(num, name)) { return false; }
  if (!(*info_)[num].IsArray()) { return rejectValue((*info_)[num], name, "an array"); }
  out = (*info_)[num].As<Napi::Array>();
  return true;
}

bool Args::getBuffer(size_t num, const char *name, Napi::Value &out) {
  if (!require(num, name)) { return false; }
  if (!(*info_)[num].IsBuffer() && !(*info_)[num].IsArrayBuffer()) { return rejectValue((*info_)[num], name, "a buffer"); }
  out = (*info_)[num];
  return true;
}

bool Args::getCallback(size_t num, const char *name, Napi::FunctionReference *&out) {
  if (!require(num, name)) { return false; }
  if (!(*info_)[num].IsFunction()) { return rejectValue((*info_)[num], name, "a function"); }
  out = new Napi::FunctionReference(Napi::Persistent((*info_)[num].As<Napi::Function>()));
  out->SuppressDestruct();
  return true;
}

bool Args::getStringOrInt(
  size_t num, const char *name, std::string &out_string, int &out_int, bool &is_string) {
  if (!require(num, name)) { return false; }
  if ((*info_)[num].IsString()) {
    out_string = (*info_)[num].As<Napi::String>().Utf8Value();
    is_string = true;
    return true;
  }
  if ((*info_)[num].IsNumber()) {
    out_int = static_cast<int>((*info_)[num].As<Napi::Number>().Int64Value());
    is_string = false;
    return true;
  }
  return rejectValue((*info_)[num], name, "a string or a number");
}

// ----- optional positional arguments -----

bool Args::optional(size_t num, const char *name, int &out) {
  if (!present(num)) { return !failed_; }
  return get(num, name, out);
}

bool Args::optional(size_t num, const char *name, unsigned int &out) {
  if (!present(num)) { return !failed_; }
  return get(num, name, out);
}

bool Args::optional(size_t num, const char *name, int64_t &out) {
  if (!present(num)) { return !failed_; }
  return get(num, name, out);
}
bool Args::optional(size_t num, const char *name, short &out) {
  if (!present(num)) { return !failed_; }
  return get(num, name, out);
}
bool Args::optional(size_t num, const char *name, double &out) {
  if (!present(num)) { return !failed_; }
  return get(num, name, out);
}

bool Args::optional(size_t num, const char *name, bool &out) {
  if (!present(num)) { return !failed_; }
  return get(num, name, out);
}

bool Args::optional(size_t num, const char *name, std::string &out) {
  if (!present(num)) { return !failed_; }
  return get(num, name, out);
}

bool Args::optional(size_t num, const char *name, Napi::Object &out) {
  if (!present(num)) { return !failed_; }
  return get(num, name, out);
}

bool Args::optional(size_t num, const char *name, Napi::Array &out) {
  if (!present(num)) { return !failed_; }
  return get(num, name, out);
}

bool Args::optionalCallback(size_t num, const char *name, Napi::FunctionReference *&out) {
  // `num` is the index of the callback itself, so it is not affected by the
  // trailing function rule of present()
  if (num >= info_->Length() || (*info_)[num].IsUndefined() || (*info_)[num].IsNull()) { return !failed_; }
  return getCallback(num, name, out);
}

// ----- the properties of an options object -----

bool Args::from(const Napi::Object &obj, const char *key, int &out) {
  if (failed_) { return false; }
  if (obj.IsEmpty() || !obj.Has(key)) { return true; }
  Napi::Value value = obj.Get(key);
  if (value.IsUndefined() || value.IsNull()) { return true; }
  if (!value.IsNumber()) { return rejectValue(value, key, "an integer"); }
  out = static_cast<int>(value.As<Napi::Number>().Int64Value());
  return true;
}

bool Args::from(const Napi::Object &obj, const char *key, int64_t &out) {
  if (failed_) { return false; }
  if (obj.IsEmpty() || !obj.Has(key)) { return true; }
  Napi::Value value = obj.Get(key);
  if (value.IsUndefined() || value.IsNull()) { return true; }
  if (!value.IsNumber()) { return rejectValue(value, key, "an integer"); }
  out = value.As<Napi::Number>().Int64Value();
  return true;
}

bool Args::from(const Napi::Object &obj, const char *key, short &out) {
  if (failed_) { return false; }
  if (obj.IsEmpty() || !obj.Has(key)) { return true; }
  Napi::Value value = obj.Get(key);
  if (value.IsUndefined() || value.IsNull()) { return true; }
  if (!value.IsNumber()) { return rejectValue(value, key, "an integer"); }
  out = static_cast<short>(value.As<Napi::Number>().Int32Value());
  return true;
}
bool Args::from(const Napi::Object &obj, const char *key, double &out) {
  if (failed_) { return false; }
  if (obj.IsEmpty() || !obj.Has(key)) { return true; }
  Napi::Value value = obj.Get(key);
  if (value.IsUndefined() || value.IsNull()) { return true; }
  if (!value.IsNumber()) { return rejectValue(value, key, "a number"); }
  out = value.As<Napi::Number>().DoubleValue();
  return true;
}

bool Args::from(const Napi::Object &obj, const char *key, std::string &out) {
  if (failed_) { return false; }
  if (obj.IsEmpty() || !obj.Has(key)) { return true; }
  Napi::Value value = obj.Get(key);
  if (value.IsUndefined() || value.IsNull()) { return true; }
  if (!value.IsString()) { return rejectValue(value, key, "a string"); }
  out = value.As<Napi::String>().Utf8Value();
  return true;
}

bool Args::from(const Napi::Object &obj, const char *key, Napi::Value &out) {
  if (failed_) { return false; }
  if (obj.IsEmpty() || !obj.Has(key)) { return true; }
  out = obj.Get(key);
  return true;
}

bool Args::from(const Napi::Object &obj, const char *key, Napi::Array &out) {
  if (failed_) { return false; }
  if (obj.IsEmpty() || !obj.Has(key)) { return true; }
  Napi::Value value = obj.Get(key);
  if (value.IsUndefined() || value.IsNull()) { return true; }
  if (!value.IsArray()) { return rejectValue(value, key, "an array"); }
  out = value.As<Napi::Array>();
  return true;
}

bool Args::fromCallback(const Napi::Object &obj, const char *key, Napi::FunctionReference *&out) {
  if (failed_) { return false; }
  if (obj.IsEmpty() || !obj.Has(key)) { return true; }
  Napi::Value value = obj.Get(key);
  if (value.IsUndefined() || value.IsNull()) { return true; }
  if (!value.IsFunction()) { return rejectValue(value, key, "a function"); }
  out = new Napi::FunctionReference(Napi::Persistent(value.As<Napi::Function>()));
  out->SuppressDestruct();
  return true;
}

} // namespace node_gdal
