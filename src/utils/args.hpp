#ifndef NODE_GDAL_ARGS_HPP
#define NODE_GDAL_ARGS_HPP

#include <cstddef>
#include <string>

#include <napi.h>

namespace node_gdal {

// Defined in gdal_common.hpp, which cannot be included here
template <typename T> T *UnwrapWrapped(const Napi::Object &obj);
template <typename T> bool IsInstanceOf(const Napi::Value &value);

// The arguments of a method, checked and converted in one place.
//
// Every getter either produces a value, or throws the JavaScript exception
// which says what was wrong and returns false, so a method reads its arguments
// as a chain of checks and cannot go on with one that was rejected:
//
//   Args args(info);
//   int width, height;
//   std::string resampling;
//   if (!(args.get(0, "width", width) && args.get(1, "height", height) &&
//         args.get(2, "resampling", resampling))) {
//     return args.failed();
//   }
//
// The optional getters leave the variable alone when the argument is missing,
// so the default of the caller stays.
class Args {
 public:
  explicit Args(const Napi::CallbackInfo &info) : info_(&info), env_(info.Env()), failed_(false) {}

  // For the helper functions which parse something that is not the
  // CallbackInfo of a method
  explicit Args(Napi::Env env) : info_(nullptr), env_(env), failed_(false) {}

  Napi::Env env() const {
    return env_;
  }

  size_t length() const {
    return info_->Length();
  }

  const Napi::CallbackInfo &info() const {
    return *info_;
  }

  // What a method returns once one of its arguments was rejected: the
  // exception describing it is already pending
  Napi::Value failed() const {
    return env_.Undefined();
  }

  // True when the argument is neither missing, nor null, nor undefined
  bool present(size_t num) const {
    return num < info_->Length() && !(*info_)[num].IsUndefined() && !(*info_)[num].IsNull();
  }

  // ----- required positional arguments -----

  bool get(size_t num, const char *name, int &out);
  bool get(size_t num, const char *name, short &out);
  bool get(size_t num, const char *name, unsigned int &out);
  bool get(size_t num, const char *name, int64_t &out);
  bool get(size_t num, const char *name, double &out);
  bool get(size_t num, const char *name, bool &out);
  bool get(size_t num, const char *name, std::string &out);
  bool get(size_t num, const char *name, Napi::Value &out);
  bool get(size_t num, const char *name, Napi::Object &out);
  bool get(size_t num, const char *name, Napi::Array &out);
  bool getBuffer(size_t num, const char *name, Napi::Value &out);
  bool getCallback(size_t num, const char *name, Napi::FunctionReference *&out);

  // An argument which has to be an instance of `type_name` and still alive
  template <typename T> bool getWrapped(size_t num, const char *name, const char *type_name, T *&out) {
    if (!require(num, name)) { return false; }
    if (!IsInstanceOf<T>((*info_)[num])) { return rejectArgument(num, name, type_name); }
    T *wrapped = UnwrapWrapped<T>((*info_)[num].As<Napi::Object>());
    if (wrapped == nullptr || !wrapped->isAlive()) { return rejectArgument(num, name, type_name); }
    out = wrapped;
    return true;
  }

  // An argument which has to be one of the values of a GDAL enum
  template <typename E> bool getEnum(size_t num, const char *name, const char *type_name, E &out) {
    if (!require(num, name)) { return false; }
    if (!(*info_)[num].IsNumber()) { return rejectArgument(num, name, type_name); }
    out = E((*info_)[num].As<Napi::Number>().Uint32Value());
    return true;
  }

  template <typename E> bool optionalEnum(size_t num, const char *name, const char *type_name, E &out) {
    if (!present(num)) { return !failed_; }
    if (!(*info_)[num].IsNumber()) { return rejectArgument(num, name, type_name); }
    out = E((*info_)[num].As<Napi::Number>().Uint32Value());
    return true;
  }

  // A number, or a string which names the same value (a data type, a color
  // interpretation, ...); `is_string` says which of the two was given
  bool getStringOrInt(
    size_t num, const char *name, std::string &out_string, int &out_int, bool &is_string);

  // ----- optional positional arguments -----

  bool optional(size_t num, const char *name, int &out);
  bool optional(size_t num, const char *name, short &out);
  bool optional(size_t num, const char *name, unsigned int &out);
  bool optional(size_t num, const char *name, int64_t &out);
  bool optional(size_t num, const char *name, double &out);
  bool optional(size_t num, const char *name, bool &out);
  bool optional(size_t num, const char *name, std::string &out);
  bool optional(size_t num, const char *name, Napi::Object &out);
  bool optional(size_t num, const char *name, Napi::Array &out);
  bool optionalCallback(size_t num, const char *name, Napi::FunctionReference *&out);

  template <typename T>
  bool optionalWrapped(size_t num, const char *name, const char *type_name, T *&out) {
    if (!present(num)) { return true; }
    return getWrapped(num, name, type_name, out);
  }

  // ----- the properties of an options object -----

  // A missing property is left alone, a present one has to be valid
  bool from(const Napi::Object &obj, const char *key, int &out);
  bool from(const Napi::Object &obj, const char *key, short &out);
  bool from(const Napi::Object &obj, const char *key, double &out);
  bool from(const Napi::Object &obj, const char *key, std::string &out);
  bool from(const Napi::Object &obj, const char *key, Napi::Value &out);
  bool from(const Napi::Object &obj, const char *key, Napi::Array &out);
  bool fromCallback(const Napi::Object &obj, const char *key, Napi::FunctionReference *&out);

  template <typename T>
  bool fromWrapped(const Napi::Object &obj, const char *key, const char *type_name, T *&out) {
    if (obj.IsEmpty() || !obj.Has(key)) { return true; }
    Napi::Value value = obj.Get(key);
    if (value.IsUndefined() || value.IsNull()) { return true; }
    if (!IsInstanceOf<T>(value)) { return rejectProperty(key, type_name); }
    T *wrapped = UnwrapWrapped<T>(value.As<Napi::Object>());
    if (wrapped == nullptr || !wrapped->isAlive()) { return rejectProperty(key, type_name); }
    out = wrapped;
    return true;
  }

 private:
  // Throws "name must be given" unless the argument is there at all
  bool require(size_t num, const char *name);

  // The three shapes of "this is not what the method expects"
  bool rejectArgument(size_t num, const char *name, const char *expected);
  bool rejectProperty(const char *key, const char *expected);
  bool rejectType(const Napi::Value &value, const char *what, const char *expected);
  bool rejectValue(const Napi::Value &value, const char *what, const char *expected);

  const Napi::CallbackInfo *info_;
  Napi::Env env_;
  bool failed_;
};

} // namespace node_gdal

#endif
