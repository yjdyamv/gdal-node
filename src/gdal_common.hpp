#ifndef __GDAL_COMMON_H__
#define __GDAL_COMMON_H__

#include <cpl_error.h>
#include <gdal_version.h>
#include <ogr_feature.h>
#include <functional>
#include <thread>
#include <stdio.h>
#include <source_location>
#include <string>
#include <memory>
#include <vector>
#include <array>
#include <iterator>

// napi
#include "napi-wrapper.h"

#include "utils/env_context.hpp"
#include "utils/gdal_error.hpp"
#include "utils/handle.hpp"
#include "utils/ptr_manager.hpp"

#if GDAL_VERSION_MAJOR < 2 || (GDAL_VERSION_MAJOR == 2 && GDAL_VERSION_MINOR < 2)
#error gdal-async now requires GDAL >= 2.2, downgrade to gdal-async@3.6.x for earlier versions
#endif

namespace node_gdal {
extern FILE *log_file;
extern bool eventLoopWarn;

// The context of the environment of the calling thread. Every path which touches
// the state of an environment runs in a Node-API callback (or in the destructor
// of a wrapper) of an environment which went through Init(), and an environment
// is only ever served by one thread, so the context of that thread is the
// context of the environment. The thread of an asynchronous operation serves no
// environment and has no context.
inline EnvContext &env_context(std::source_location loc = std::source_location::current()) {
  EnvContext *ctx = EnvContext::active();
  if (ctx == nullptr) {
    fprintf(stderr, "[gdal] no Node-API environment is being served by this thread (%s:%d)\n", loc.file_name(), loc.line());
    abort();
  }
  return *ctx;
}

// The context of an environment which is at hand, see above
inline EnvContext &env_context(::napi_env env) {
  EnvContext *ctx = EnvContext::find(env);
  if (ctx == nullptr) { abort(); }
  return *ctx;
}

// Napi::Env has no default constructor, so the environment is stored raw and
// wrapped on demand
inline Napi::Env napi_env() {
  return Napi::Env(env_context().env());
}

// The object store of the environment of the calling thread
inline ObjectStore &object_store() {
  return env_context().store();
}
} // namespace node_gdal

#ifdef ENABLE_LOGGING
#define LOG(fmt, ...)                                                                                                  \
  if (node_gdal::log_file) {                                                                                           \
    fprintf(node_gdal::log_file, fmt "\n", __VA_ARGS__);                                                               \
    fflush(node_gdal::log_file);                                                                                       \
  }
#else
#define LOG(fmt, ...)
#endif

//
// Method signatures
//
// The N-API world uses `Napi::Value (const Napi::CallbackInfo &)`. Methods stay
// static so that both the declaration sites and the descriptor builders below
// can reference them as plain function pointers.
//
#define NAPI_METHOD(name) Napi::Value name(const Napi::CallbackInfo &info)
#define NAPI_GETTER(name) Napi::Value name(const Napi::CallbackInfo &info)
#define NAPI_SETTER(name) void name(const Napi::CallbackInfo &info, const Napi::Value &value)

// the marker of a read-only property, defined in gdal_common.cpp
NAPI_SETTER(READ_ONLY_SETTER);

// Building a string from a null pointer used to segfault, hence this helper
class SafeString {
    public:
  static Napi::Value New(Napi::Env env, const char *data) {
    if (!data) return env.Null();
    return Napi::String::New(env, data);
  }
  // Convenience for the (many) call sites that run on the main thread and can
  // use the ambient environment - same approach as v8_undefined()/v8_null()
  static Napi::Value New(const char *data) {
    return New(node_gdal::napi_env(), data);
  }
};

inline const char *getOGRErrMsg(int err) {
  if (err == 6) {
    // get more descriptive error
    return CPLGetLastErrorMsg();
  }
  switch (err) {
    case 0: return "No error";
    case 1: return "Not enough data";
    case 2: return "Not enough memory";
    case 3: return "Unsupported geometry type";
    case 4: return "Unsupported operation";
    case 5: return "Corrupt Data";
    case 6: return "Failure";
    case 7: return "Unsupported SRS";
    default: return "Invalid Error";
  }
};

//
// Property descriptor builders
//
// node-addon-api's `InstanceMethod` only accepts *member* function pointers,
// but every method in this code base is a static function. `ClassPropertyDescriptor`
// has a public constructor from a raw `napi_property_descriptor`, so we can
// build descriptors around plain function pointers and still get instance
// methods (no `napi_static` flag).
//
namespace node_gdal {

// `napi_define_class` takes plain C function pointers, so a C++ exception
// unwinding out of one aborts the process instead of raising a JS error.
// node-addon-api wraps its own callbacks for exactly this reason
// (`details::WrapCallback`); these trampolines have to do the same, because
// every failed `napi_*` call inside the node-addon-api helpers throws.
// Must be called from a catch block.
inline napi_value RethrowAsJavaScriptException(Napi::Env env) noexcept {
  try {
    throw;
  } catch (const Napi::Error &e) {
    e.ThrowAsJavaScriptException();
  } catch (const GDALError &e) {
    // The class and the code of the failure are what the caller has to look at,
    // so this goes through the same path a body struct would take and not
    // through the generic std::exception arm below
    throwError(env, e);
  } catch (const std::exception &e) {
    Napi::Error::New(env, e.what()).ThrowAsJavaScriptException();
  } catch (const char *err) {
    Napi::Error::New(env, err).ThrowAsJavaScriptException();
  } catch (...) {
    Napi::Error::New(env, "Unknown error").ThrowAsJavaScriptException();
  }
  return nullptr;
}

template <Napi::Value (*FN)(const Napi::CallbackInfo &)> napi_value MethodTrampoline(::napi_env env, napi_callback_info info) {
  Napi::CallbackInfo ci(env, info);
  try {
    return FN(ci);
  } catch (...) {
    return RethrowAsJavaScriptException(ci.Env());
  }
}

template <void (*FN)(const Napi::CallbackInfo &, const Napi::Value &)>
napi_value SetterTrampoline(::napi_env env, napi_callback_info info) {
  Napi::CallbackInfo ci(env, info);
  Napi::Value value = ci.Length() > 0 ? ci[0] : ci.Env().Undefined();
  try {
    FN(ci, value);
  } catch (...) {
    return RethrowAsJavaScriptException(ci.Env());
  }
  return nullptr;
}

inline napi_value UndefinedTrampoline(::napi_env env, napi_callback_info) {
  return Napi::Env(env).Undefined();
}

// The free functions of the module and the static methods of the classes are
// not installed through the trampolines above: they go in as ordinary
// `Napi::Function`s, and node-addon-api's wrapper for those catches
// `Napi::Error` only, so a `GDALError` raised by one of them would unwind
// through N-API and abort the process. This gives them the same boundary the
// methods have.
template <Napi::Value (*FN)(const Napi::CallbackInfo &)>
inline Napi::Value FreeFunctionTrampoline(const Napi::CallbackInfo &info) {
  try {
    return FN(info);
  } catch (...) {
    RethrowAsJavaScriptException(info.Env());
    return info.Env().Undefined();
  }
}

//
// The declarative member registry
//
// A class describes its members as two constexpr arrays of plain function
// pointers instead of a list of macros. Nothing about a member has to be a
// property of the class: any function with the shape of NAPI_METHOD will do,
// which is what lets a definition live in the anonymous namespace of the .cpp
// file, next to the body struct of its asynchronous variant.
//
//   static constexpr MethodEntry methods[] = {
//     method("count", count),
//     asyncMethod("count", count, countAsync),
//   };
//   static constexpr AccessorEntry accessors[] = {
//     attr("interpretation", interpretationGetter, READ_ONLY_SETTER),
//     hiddenAttr("band", bandGetter, READ_ONLY_SETTER),
//   };
//   Napi::Function lcons = GDALDefineClass<ColorTable>(env, "ColorTable", methods, accessors);
//
// The `Async` variants are registered from the same entry, so that a method
// cannot have an asynchronous half which is registered as something else.
//
enum MemberFlags : unsigned { MEMBER_NONE = 0, MEMBER_HIDDEN = 1U << 0 };

struct MethodEntry {
  const char *name;
  Napi::Value (*fn)(const Napi::CallbackInfo &);
  // when set, the method is registered a second time as `name + "Async"`
  Napi::Value (*fn_async)(const Napi::CallbackInfo &);
  unsigned flags;
};

struct AccessorEntry {
  const char *name;
  Napi::Value (*getter)(const Napi::CallbackInfo &);
  void (*setter)(const Napi::CallbackInfo &, const Napi::Value &);
  // when set, the accessor is registered a second time as the hidden and
  // read-only `name + "Async"`
  Napi::Value (*getter_async)(const Napi::CallbackInfo &);
  void (*setter_async)(const Napi::CallbackInfo &, const Napi::Value &);
  unsigned flags;
};

// `data` of the property descriptor, which is what napi_get_cb_info returns
inline void *EntryOf(::napi_env env, napi_callback_info info) {
  size_t argc = 0;
  void *data = nullptr;
  if (napi_get_cb_info(env, info, &argc, nullptr, nullptr, &data) != napi_ok) return nullptr;
  return data;
}

inline napi_value MethodEntryTrampoline(::napi_env env, napi_callback_info info) {
  Napi::CallbackInfo ci(env, info);
  const auto *entry = static_cast<const MethodEntry *>(EntryOf(env, info));
  if (entry == nullptr) return nullptr;
  try {
    return entry->fn(ci);
  } catch (...) {
    return RethrowAsJavaScriptException(ci.Env());
  }
}

inline napi_value AsyncMethodEntryTrampoline(::napi_env env, napi_callback_info info) {
  Napi::CallbackInfo ci(env, info);
  const auto *entry = static_cast<const MethodEntry *>(EntryOf(env, info));
  if (entry == nullptr || entry->fn_async == nullptr) return nullptr;
  try {
    return entry->fn_async(ci);
  } catch (...) {
    return RethrowAsJavaScriptException(ci.Env());
  }
}

inline napi_value GetterEntryTrampoline(::napi_env env, napi_callback_info info) {
  Napi::CallbackInfo ci(env, info);
  const auto *entry = static_cast<const AccessorEntry *>(EntryOf(env, info));
  if (entry == nullptr) return nullptr;
  try {
    return entry->getter(ci);
  } catch (...) {
    return RethrowAsJavaScriptException(ci.Env());
  }
}

inline napi_value AsyncGetterEntryTrampoline(::napi_env env, napi_callback_info info) {
  Napi::CallbackInfo ci(env, info);
  const auto *entry = static_cast<const AccessorEntry *>(EntryOf(env, info));
  if (entry == nullptr || entry->getter_async == nullptr) return nullptr;
  try {
    return entry->getter_async(ci);
  } catch (...) {
    return RethrowAsJavaScriptException(ci.Env());
  }
}

// `READ_ONLY_SETTER` is the marker of a property which has no setter; the
// message names the property, which only the registry entry knows
//
inline napi_value ReadOnlySetterTrampoline(::napi_env env, napi_callback_info info) {
  Napi::CallbackInfo ci(env, info);
  const auto *entry = static_cast<const AccessorEntry *>(EntryOf(env, info));
  std::string err = std::string(entry != nullptr ? entry->name : "property") + " is a read-only property";
  Napi::Error::New(ci.Env(), err.c_str()).ThrowAsJavaScriptException();
  return nullptr;
}

inline napi_value SetterEntryTrampoline(::napi_env env, napi_callback_info info) {
  Napi::CallbackInfo ci(env, info);
  const auto *entry = static_cast<const AccessorEntry *>(EntryOf(env, info));
  if (entry == nullptr || entry->setter == nullptr) return nullptr;
  Napi::Value value = ci.Length() > 0 ? ci[0] : ci.Env().Undefined();
  try {
    entry->setter(ci, value);
  } catch (...) {
    return RethrowAsJavaScriptException(ci.Env());
  }
  return nullptr;
}

inline napi_value AsyncSetterEntryTrampoline(::napi_env env, napi_callback_info info) {
  Napi::CallbackInfo ci(env, info);
  const auto *entry = static_cast<const AccessorEntry *>(EntryOf(env, info));
  if (entry == nullptr || entry->setter_async == nullptr) return nullptr;
  // the asynchronous half of a read-only property is read-only as well and
  // `READ_ONLY_SETTER` does not know its own name
  if (entry->setter_async == &READ_ONLY_SETTER) {
    std::string err = std::string(entry->name) + "Async is a read-only property";
    Napi::Error::New(ci.Env(), err.c_str()).ThrowAsJavaScriptException();
    return nullptr;
  }
  Napi::Value value = ci.Length() > 0 ? ci[0] : ci.Env().Undefined();
  try {
    entry->setter_async(ci, value);
  } catch (...) {
    return RethrowAsJavaScriptException(ci.Env());
  }
  return nullptr;
}

inline void InstallMethod(Napi::Env env, Napi::Object prototype, const char *name, napi_callback trampoline,
  const void *entry, bool enumerable) {
  napi_value fn = nullptr;
  if (napi_create_function(env, name, NAPI_AUTO_LENGTH, trampoline, const_cast<void *>(entry), &fn) != napi_ok) return;
  prototype.DefineProperty(Napi::PropertyDescriptor::Value(name, Napi::Value(env, fn),
    static_cast<napi_property_attributes>(napi_writable | napi_configurable | (enumerable ? napi_enumerable : 0))));
}


} // namespace node_gdal

template <typename T, Napi::Value (*FN)(const Napi::CallbackInfo &)>
inline Napi::ClassPropertyDescriptor<T> GDALInstanceMethodT(const char *name) {
  napi_property_descriptor d = {};
  d.utf8name = name;
  d.method = &node_gdal::MethodTrampoline<FN>;
  d.attributes = static_cast<napi_property_attributes>(napi_writable | napi_enumerable | napi_configurable);
  return Napi::ClassPropertyDescriptor<T>(d);
}

// Same, but non-enumerable (used for the hidden async accessors)
template <typename T, Napi::Value (*FN)(const Napi::CallbackInfo &)>
inline Napi::ClassPropertyDescriptor<T> GDALInstanceMethodHiddenT(const char *name) {
  napi_property_descriptor d = {};
  d.utf8name = name;
  d.method = &node_gdal::MethodTrampoline<FN>;
  d.attributes = static_cast<napi_property_attributes>(napi_writable | napi_configurable);
  return Napi::ClassPropertyDescriptor<T>(d);
}

// Non-enumerable accessor, with a setter: the original ATTR_DONT_ENUM passed
// one, and the read-only ones rely on it to produce their message rather than
// the generic "has only a getter" thrown by V8
template <
  typename T,
  Napi::Value (*GET)(const Napi::CallbackInfo &),
  void (*SET)(const Napi::CallbackInfo &, const Napi::Value &)>
inline Napi::ClassPropertyDescriptor<T> GDALInstanceAccessorHiddenT(const char *name) {
  napi_property_descriptor d = {};
  d.utf8name = name;
  d.getter = &node_gdal::MethodTrampoline<GET>;
  d.setter = &node_gdal::SetterTrampoline<SET>;
  d.data = (void *)name;
  d.attributes = static_cast<napi_property_attributes>(napi_writable | napi_configurable);
  return Napi::ClassPropertyDescriptor<T>(d);
}

//
// Drop-in replacement for ObjectWrap<T>::DefineClass.
//
// The methods cannot go through napi_define_class: V8 gives a `method`
// descriptor a signature tied to the class it is defined on, and calling one on
// a subclass instance is rejected with `Illegal invocation`. N-API has no
// counterpart of v8::FunctionTemplate::Inherit (node-addon-api has none either),
// so the class hierarchy only exists as a JS prototype chain and V8 cannot tie
// the two together - every inherited method would break, and there are many
// (Geometry is the base of a dozen classes).
//
// Feeding the functions in as `value` descriptors is not an option either:
// napi_define_class only accepts a primitive or a Template there and aborts the
// process otherwise.
//
// So the methods are installed on the prototype after the class has been
// defined, as ordinary functions - which is exactly what the old binding's
// SetPrototypeMethod produced. They carry no receiver check, and the prototype
// chain is enough to make them inheritable. Accessors are unaffected and keep
// going through napi_define_class.
//

//
// The declarative form of the above: the members of the class are two constexpr
// arrays of function pointers, which is why a definition does not have to be a
// member of the class (see node_gdal::MethodEntry)
//
// the marker of a read-only property, defined in gdal_common.cpp
NAPI_SETTER(READ_ONLY_SETTER);

template <typename T, size_t N, typename Accessors>
inline Napi::Function GDALDefineClass(Napi::Env env, const char *name, const node_gdal::MethodEntry (&methods)[N],
  const Accessors &accessors) {
  std::vector<Napi::ClassPropertyDescriptor<T>> properties;
  // the `Async` accessors generate their name, and napi_property_descriptor
  // keeps the pointer: the storage has to outlive the loop
  std::vector<std::string> async_names;
  properties.reserve(std::size(accessors) * 2);
  async_names.reserve(std::size(accessors));

  constexpr napi_property_attributes base_attributes = static_cast<napi_property_attributes>(napi_writable | napi_configurable);
  constexpr napi_property_attributes enumerable_attributes =
    static_cast<napi_property_attributes>(napi_writable | napi_configurable | napi_enumerable);

  for (const auto &entry : accessors) {
    const bool hidden = (entry.flags & node_gdal::MEMBER_HIDDEN) != 0;
    napi_property_descriptor d = {};
    d.utf8name = entry.name;
    d.getter = &node_gdal::GetterEntryTrampoline;
    d.setter = entry.setter == nullptr ? nullptr
             : entry.setter == &READ_ONLY_SETTER ? &node_gdal::ReadOnlySetterTrampoline
                                                 : &node_gdal::SetterEntryTrampoline;
    d.data = const_cast<node_gdal::AccessorEntry *>(&entry);
    d.attributes = hidden ? base_attributes : enumerable_attributes;
    properties.push_back(Napi::ClassPropertyDescriptor<T>(d));

    if (entry.getter_async != nullptr) {
      async_names.push_back(std::string(entry.name) + "Async");
      napi_property_descriptor a = {};
      a.utf8name = async_names.back().c_str();
      a.getter = &node_gdal::AsyncGetterEntryTrampoline;
      a.setter = &node_gdal::AsyncSetterEntryTrampoline;
      a.data = const_cast<node_gdal::AccessorEntry *>(&entry);
      a.attributes = base_attributes;
      properties.push_back(Napi::ClassPropertyDescriptor<T>(a));
    }
  }

  Napi::Function lcons = Napi::ObjectWrap<T>::DefineClass(env, name, properties);
  Napi::Object prototype = lcons.Get("prototype").As<Napi::Object>();

  for (const auto &entry : methods) {
    node_gdal::InstallMethod(env, prototype, entry.name, &node_gdal::MethodEntryTrampoline, &entry, true);
    if (entry.fn_async != nullptr) {
      const std::string async_name = std::string(entry.name) + "Async";
      node_gdal::InstallMethod(env, prototype, async_name.c_str(), &node_gdal::AsyncMethodEntryTrampoline, &entry, true);
    }
  }

  return lcons;
}

// a class without accessors
//
template <typename T, size_t N>
inline Napi::Function GDALDefineClass(Napi::Env env, const char *name, const node_gdal::MethodEntry (&methods)[N]) {
  static constexpr std::array<node_gdal::AccessorEntry, 0> no_accessors = {};
  return GDALDefineClass<T>(env, name, methods, no_accessors);
}




//
// Scalar -> Napi::Value conversions used by the generated method bodies
//
namespace node_gdal {
inline Napi::Value ToNapi(Napi::Env env, int v) {
  return Napi::Number::New(env, v);
}
inline Napi::Value ToNapi(Napi::Env env, unsigned int v) {
  return Napi::Number::New(env, v);
}
inline Napi::Value ToNapi(Napi::Env env, long v) {
  return Napi::Number::New(env, (double)v);
}
inline Napi::Value ToNapi(Napi::Env env, unsigned long v) {
  return Napi::Number::New(env, (double)v);
}
inline Napi::Value ToNapi(Napi::Env env, long long v) {
  return Napi::Number::New(env, (double)v);
}
inline Napi::Value ToNapi(Napi::Env env, unsigned long long v) {
  return Napi::Number::New(env, (double)v);
}
inline Napi::Value ToNapi(Napi::Env env, double v) {
  return Napi::Number::New(env, v);
}
inline Napi::Value ToNapi(Napi::Env env, float v) {
  return Napi::Number::New(env, v);
}
inline Napi::Value ToNapi(Napi::Env env, bool v) {
  return Napi::Boolean::New(env, v);
}
inline Napi::Value ToNapi(Napi::Env env, const std::string &v) {
  return Napi::String::New(env, v);
}
inline Napi::Value ToNapi(Napi::Env env, const char *v) {
  return SafeString::New(env, v);
}
inline Napi::Value ToNapi(Napi::Env, Napi::Value v) {
  return v;
}
inline Napi::Value ToNapi(Napi::Env, Napi::Object v) {
  return v;
}
inline Napi::Value ToNapi(Napi::Env, Napi::Array v) {
  return v;
}
inline Napi::Value ToNapi(Napi::Env, Napi::String v) {
  return v;
}
inline Napi::Value ToNapi(Napi::Env, Napi::Number v) {
  return v;
}

// V8's ThrowException replaced an already pending exception, so a later, more
// specific message used to win over an earlier generic one. N-API refuses to
// touch a pending exception (node-addon-api's
// NODE_API_SWALLOW_UNTHROWABLE_EXCEPTIONS path), which would leave the caller
// that re-throws after StringList::parse with the parser's message instead of
// its own.
inline void ThrowOverPending(Napi::Env env, const char *message) {
  napi_value pending = nullptr;
  napi_get_and_clear_last_exception(env, &pending);
  Napi::Error::New(env, message).ThrowAsJavaScriptException();
}

//
// The result_type of a method body names the JS type the result has to be
// converted to. The old binding returned a v8 local from each of them, so the
// names are kept - but they cannot be resolved through ToNapi, which
// dispatches on the C++ type: OGRBoolean is an int, and every method returning
// one would hand JS a number where a boolean is expected.
//
struct Boolean {
  static Napi::Value New(Napi::Env env, bool v) {
    return Napi::Boolean::New(env, v);
  }
};

struct Number {
  static Napi::Value New(Napi::Env env, double v) {
    return Napi::Number::New(env, v);
  }
};

struct Integer {
  static Napi::Value New(Napi::Env env, int32_t v) {
    return Napi::Number::New(env, v);
  }
};

} // namespace node_gdal


template <
  typename T,
  Napi::Value (*GET)(const Napi::CallbackInfo &),
  void (*SET)(const Napi::CallbackInfo &, const Napi::Value &)>
inline Napi::ClassPropertyDescriptor<T> GDALInstanceAccessorT(const char *name) {
  napi_property_descriptor d = {};
  d.utf8name = name;
  d.getter = &node_gdal::MethodTrampoline<GET>;
  d.setter = &node_gdal::SetterTrampoline<SET>;
  d.data = (void *)name;
  d.attributes = static_cast<napi_property_attributes>(napi_writable | napi_enumerable | napi_configurable);
  return Napi::ClassPropertyDescriptor<T>(d);
}

// ----- throwing / rejecting helpers -------

// These are referenced from the macros below as node_gdal::..., so they must
// live in the node_gdal namespace (node_gdal::ToNapi and the trampolines
// already do)
namespace node_gdal {

inline void ThrowError(Napi::Env env, const char *msg) {
  Napi::Error::New(env, msg).ThrowAsJavaScriptException();
}

inline Napi::Value RejectPromise(const Napi::CallbackInfo &info, const char *msg) {
  auto deferred = Napi::Promise::Deferred::New(info.Env());
  deferred.Reject(toJS(info.Env(), GDALError(msg != nullptr ? msg : "Operation failed")).Value());
  return deferred.Promise();
}

} // namespace node_gdal

//
// Object handle helpers
//
namespace node_gdal {

template <typename T> inline T *UnwrapWrapped(const Napi::Object &obj) {
  return Napi::ObjectWrap<T>::Unwrap(obj);
}

template <typename T> inline bool IsInstanceOf(const Napi::Value &obj) {
  return obj.IsObject() && obj.As<Napi::Object>().InstanceOf(T::constructor().Value());
}

//
// The wrapped object behind `value`, which must be a `type`: this is what the
// bodies of the methods start with. It throws instead of returning a value on
// failure, which the asynchronous path turns into a rejection.
//
// `name` is the name of the class as it appears in the message, because a
// template cannot stringify its own type argument the way the old macro did.
//
template <typename T> inline T *unwrap_or_throw(Napi::Value value, const char *name) {
  if (!IsInstanceOf<T>(value)) throw GDALError(std::string("Object must be a ") + name + " object", "TypeError");
  T *obj = UnwrapWrapped<T>(value.As<Napi::Object>());
  if (!obj->isAlive()) throw GDALError(std::string(name) + " object has already been destroyed");
  return obj;
}

//
// The GDAL object behind the handle which `obj->get()` returns, which throws
// when the object of the parent has been destroyed in the meantime
//
template <typename T, typename O> inline T raw_or_throw(const O &obj, const char *name) {
  T raw = obj->get();
  if (!obj) throw GDALError(std::string(name) + " object has already been destroyed");
  return raw;
}

} // namespace node_gdal

// ----- object base class -------

//
// Every JS-visible class derives from this instead of Napi::ObjectWrap<T>
// directly, for three reasons:
//
//  * the message thrown by a constructor called without `new` is asserted by
//    the test suite (test/api_classes.test.ts), and node-addon-api's own
//    wording ("Class constructors cannot be invoked without 'new'") does not
//    match what the old binding used to throw;
//  * it gives the classes a single place for the shared constructor plumbing;
//  * the memory which the wrapped object holds is memory V8 knows nothing
//    about, so the wrapper needs a place where it can report it.
//
template <typename T> class GDALObject : public Napi::ObjectWrap<T> {
    public:
  explicit GDALObject(const Napi::CallbackInfo &info) : Napi::ObjectWrap<T>(info) {
  }

  static Napi::Value OnCalledAsFunction(const Napi::CallbackInfo &info) {
    Napi::Error::New(info.Env(), "Cannot call constructor as function, you need to use 'new' keyword")
      .ThrowAsJavaScriptException();
    return info.Env().Undefined();
  }

  //
  // GDAL allocates memory on our behalf which V8 cannot see - a dataset keeps
  // the blocks of its bands, a feature keeps its geometries - so a process which
  // holds a few hundred wrapped objects looks idle to the garbage collector,
  // which then has no reason to run and collect the wrappers which nobody uses.
  // Every wrapper reports what it holds when that changes, and the base class
  // gives back whatever is left once the wrapper is gone.
  //
  void setExternalMemory(int64_t size) {
    if (size == size_) return;
    Napi::MemoryManagement::AdjustExternalMemory(this->Env(), size - size_);
    size_ = size;
  }

  int64_t externalMemory() const {
    return size_;
  }

  virtual ~GDALObject() {
    setExternalMemory(0);
  }

    private:
  int64_t size_ = 0;
};

// ----- private property keys -------

//
// The old binding had SetPrivate/GetPrivate, keyed by a *name* and interned by
// V8. N-API only has napi_create_symbol, which yields a fresh symbol on every
// call, so the keys have to be registered to keep behaving like the originals.
// The registry is per-thread: every V8 isolate (the main thread and each
// worker_thread) has its own environment and a symbol created in one isolate
// cannot be used in another. The references are released by an environment
// cleanup hook, which runs while the environment is still usable; the thread
// local map is empty by the time its own destructor runs, after the environment
// is gone.
//
namespace node_gdal {

//
// The interned private property symbols. The old binding had SetPrivate/GetPrivate
// keyed by a *name* and interned by V8; N-API only has napi_create_symbol, which
// yields a fresh symbol on every call, so the keys are registered per environment
// to keep behaving like the originals. The registry lives in the EnvContext of the
// environment, which releases it while the environment is still usable.
//
inline Napi::Symbol PrivateKey(Napi::Env env, const char *name) {
  EnvContext *ctx = EnvContext::find(env);
  if (ctx == nullptr) {
    // Reachable only when the addon did not go through Init(); the symbol is
    // still usable, it just will not be interned
    return Napi::Symbol::New(env, name);
  }

  auto &keys = ctx->symbols();
  auto i = keys.find(name);
  if (i != keys.end()) return i->second.Value();

  Napi::Symbol s = Napi::Symbol::New(env, name);
  keys.emplace(name, Napi::Persistent(s));
  return s;
}

//
// The private property of `obj` which is registered as `name`: this is how the
// wrapped objects keep their internal state out of sight. `obj` is taken as a
// `Napi::Value`, because the call sites pass `info.This()` and plain locals
// alike.
//
inline Napi::Value get_private(Napi::Value obj, const char *name) {
  return obj.As<Napi::Object>().Get(PrivateKey(obj.Env(), name));
}

inline void set_private(Napi::Value obj, const char *name, Napi::Value value) {
  obj.As<Napi::Object>().Set(PrivateKey(obj.Env(), name), value);
}

//
// Every wrapped class keeps a persistent reference to its constructor, because
// every object of that class needs it to create a new wrapper. The reference has
// to outlive the objects, but not the environment: a worker_thread destroys its
// environment when it terminates, and the reference would keep the constructor
// alive after that. The references are therefore kept in the EnvContext of the
// environment, which releases them while the environment is still usable, see
// EnvContext::constructor().
//
} // namespace node_gdal

namespace node_gdal {

//
// The getters which hand out the object of a parent hand out a copy of it, so
// that the object of the parent cannot be modified behind its back. A write to
// the copy would succeed and change nothing, so the copy is marked read-only and
// refuses to be modified, which is the only way the user can find out that the
// write did not do what it looked like it did.
//
inline void set_read_only(Napi::Value obj) {
  set_private(obj, "read_only_", Napi::Boolean::New(obj.Env(), true));
}

inline bool is_read_only(Napi::Value obj) {
  Napi::Value flag = get_private(obj, "read_only_");
  return flag.IsBoolean() && flag.As<Napi::Boolean>().Value();
}

// `what` says where the object came from, e.g.
// "A FieldDefn retrieved from a collection"
inline void throw_read_only(Napi::Env env, const char *what) {
  Napi::Error::New(env, std::string(what) + " is read-only, create a new one to modify it")
    .ThrowAsJavaScriptException();
}

} // namespace node_gdal

// ----- shared_ptr payloads -------

//
// Several classes (Group, MDArray, Attribute, Dimension) hold a
// std::shared_ptr to their GDAL object rather than a raw pointer. A shared_ptr
// cannot travel through Napi::External<void> by value, so the factory hands
// over an owning copy and the constructor takes it. They travel in the same
// thread-local slot as the raw pointers.
//
namespace node_gdal {

//
// A factory creates the wrapper of an existing GDAL object by calling the JS
// constructor, and the constructor needs the pointer. The usual carrier is a
// Napi::External, but an External is a JS value whose C++ allocation is freed by
// a weak callback when the wrapper is garbage collected - and a wrapper which is
// still alive when its environment is destroyed (a terminating worker thread, a
// process that exits with open datasets) is never collected, so every such
// External showed up as a leak. The pointer is handed over in a slot of the
// EnvContext instead: the constructor runs synchronously inside New() and takes
// it back, and the guard restores the slot even when the constructor throws.
//
namespace detail {

struct PendingObjectGuard {
  EnvContext &ctx;
  PendingObject previous;
  explicit PendingObjectGuard(const void *ptr, void (*free)(void *) = nullptr)
    : ctx(env_context()), previous(ctx.pending_object()) {
    ctx.pending_object() = {ptr, free};
  }
  ~PendingObjectGuard() {
    ctx.pending_object() = previous;
  }
};

} // namespace detail

// Takes the pointer that the factory stored before calling the constructor.
// Returns nullptr when the object is created directly from JS, which the
// constructors report as an error.
template <typename T> inline T *TakePendingObject() {
  PendingObject &slot = env_context().pending_object();
  const void *ptr = slot.ptr;
  slot = PendingObject();
  return const_cast<T *>(static_cast<const T *>(ptr));
}

// Same, as a handle which owns the object the way the factory said. The
// constructors of the classes which own their object take this one, so the
// question of who frees the object is answered by the factory and not by a flag
// which the constructor has to remember to set.
template <typename T> inline Handle<T> TakePendingHandle() {
  PendingObject &slot = env_context().pending_object();
  PendingObject pending = slot;
  slot = PendingObject();
  return Handle<T>(const_cast<T *>(static_cast<const T *>(pending.ptr)), pending.free);
}

// Allocates the owning copy of a shared_ptr that a factory hands over; taken
// back and freed by TakePendingShared().
template <typename T> [[nodiscard]] inline void *ExportShared(const std::shared_ptr<T> &p) {
  return new std::shared_ptr<T>(p);
}

// Same, for the classes that hold a std::shared_ptr to their GDAL object
// (Group, MDArray, Attribute, Dimension): the factory allocates an owning copy
// of the shared_ptr, which this takes and frees. The shared_ptr spells out the
// ownership by itself, so those classes do not need a Handle.
template <typename T> inline std::shared_ptr<T> TakePendingShared() {
  PendingObject &slot = env_context().pending_object();
  auto *ptr = static_cast<std::shared_ptr<T> *>(const_cast<void *>(slot.ptr));
  slot = PendingObject();
  if (ptr == nullptr) { return std::shared_ptr<T>(); }
  std::shared_ptr<T> r = *ptr;
  delete ptr;
  return r;
}

} // namespace node_gdal

// ----- inheritance -------

//
// node-addon-api has no equivalent of v8::FunctionTemplate::Inherit, and the
// stable N-API has no prototype setter (node_api_set_prototype is behind
// NAPI_EXPERIMENTAL), so the chain is established from JS.
//
// Both the constructor and its `prototype` object have to be relinked:
// `x instanceof Base` walks `Derived.prototype`, while `Derived.staticMember`
// needs the constructor chain.
//
namespace node_gdal {

inline void Inherit(Napi::Function derived, Napi::Function base) {
  Napi::Env env = derived.Env();
  Napi::Function setProtoOf =
    env.Global().Get("Object").As<Napi::Object>().Get("setPrototypeOf").As<Napi::Function>();
  setProtoOf.Call({derived, base});
  setProtoOf.Call({derived.Get("prototype"), base.Get("prototype")});
}

} // namespace node_gdal

// ----- object property conversion -------

#define NODE_INT64_FROM_OBJ_OPT(obj, key, var)                                                                         \
  {                                                                                                                    \
    if (obj.HasOwnProperty(Napi::String::New(info.Env(), key))) {                                                       \
      Napi::Value val = obj.Get(Napi::String::New(info.Env(), key));                                                    \
      if (!val.IsNumber()) {                                                                                            \
        Napi::TypeError::New(info.Env(), "Property \"" key "\" must be a number").ThrowAsJavaScriptException();        \
        return info.Env().Undefined();                                                                                  \
      }                                                                                                                \
      var = val.As<Napi::Number>().Int64Value();                                                                        \
    }                                                                                                                  \
  }

// ----- argument conversion -------

namespace node_gdal {

//
// The index of the field which `value` names, either by name or by position;
// like the rest of the argument checking, a wrong argument is a `TypeError` or
// a `RangeError` and not a `GDALError`
//
template <typename OGRFT> inline int parse_field_index(Napi::Value value, const OGRFT *defn) {
  if (value.IsString()) {
    std::string name = value.As<Napi::String>().Utf8Value();
    int index = defn->GetFieldIndex(name.c_str());
    if (index == -1) throw GDALError("Specified field name does not exist");
    return index;
  }

  if (value.IsNumber()) {
    int index = value.As<Napi::Number>().Int32Value();
    if (index < 0 || index >= defn->GetFieldCount()) throw GDALError("Invalid field index", "RangeError");
    return index;
  }

  throw GDALError("Field index must be integer or string", "TypeError");
}

} // namespace node_gdal

// ----- optional argument conversion -------

// ----- special case: AbortSignal in an [options] argument -----
// The variable (declared by the caller) is left empty when there is no abort_signal
#define NODE_ABORT_SIGNAL_FROM_OBJ_OPT(obj, var)                                                                       \
  if (!(obj).IsEmpty() && (obj).Has("abort_signal")) var = (obj).Get("abort_signal");

// ----- special case: progress callback in [options] argument -------
//
// A function in the place of the options object is the callback of a method
// whose options are optional, so it is not an error: the progress callback is
// then simply missing

// The progress callback is released by hand when the AbortSignal turns out to
// be invalid: the operation returns at once with the pending TypeError and
// neither the worker nor the sync execution context will ever own it
#define NODE_PROGRESS_CB_OPT(num, progress_cb, job)                                                                    \
  {                                                                                                                    \
    Napi::Object progress_obj;                                                                                         \
    progress_cb = nullptr;                                                                                             \
    if (info.Length() > num && !info[num].IsNull() && !info[num].IsUndefined() && !info[num].IsFunction()) {           \
      if (!info[num].IsObject()) {                                                                                     \
        Napi::TypeError::New(info.Env(), "options must be an object").ThrowAsJavaScriptException();                    \
        return info.Env().Undefined();                                                                                  \
      }                                                                                                                \
      progress_obj = info[num].As<Napi::Object>();                                                                     \
      Args progress_args(info);                                                                                        \
      if (!progress_args.fromCallback(progress_obj, "progress_cb", progress_cb)) {                                     \
        return progress_args.failed();                                                                                 \
      }                                                                                                                \
      Napi::Value _abort_signal;                                                                                       \
      NODE_ABORT_SIGNAL_FROM_OBJ_OPT(progress_obj, _abort_signal);                                                     \
      if (!_abort_signal.IsEmpty() && !job.setAbortSignal(_abort_signal)) {                                             \
        if (progress_cb) {                                                                                              \
          progress_cb->Reset();                                                                                         \
          delete progress_cb;                                                                                           \
        }                                                                                                               \
        return info.Env().Undefined();                                                                                  \
      }                                                                                                                 \
    }                                                                                                                  \
    if (progress_cb) { job.progress = progress_cb; }                                                                   \
  }

#define MEASURE_EXECUTION_TIME(msg, op)                                                                                \
  {                                                                                                                    \
    auto start = std::chrono::high_resolution_clock::now();                                                            \
    if (msg != nullptr) fprintf(stderr, "%s", msg);                                                                    \
    op;                                                                                                                \
    auto elapsed = std::chrono::high_resolution_clock::now() - start;                                                  \
    if (msg != nullptr)                                                                                                \
      fprintf(                                                                                                         \
        stderr,                                                                                                        \
        "%ld µs\n",                                                                                                    \
        static_cast<long>(std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count()));                    \
  }

// gdal-async supports a single instance per V8 isolate (enforced in Init),
// so the environment is kept reachable for the few places (async result
// lambdas) where the old binding did not require threading it through.
// The environment is declared near the top of this file (the trampolines above
// need the ::napi_env type unshadowed), it is defined in node_gdal.cpp
inline Napi::Value v8_undefined() { return node_gdal::napi_env().Undefined(); }
inline Napi::Value v8_null() { return node_gdal::napi_env().Null(); }

// ----- array helpers -------

template <typename INPUT, typename RETURN>
std::shared_ptr<RETURN[]> NumberArrayToSharedPtr(Napi::Env env, Napi::Array array, size_t count = 0) {
  if (array.IsEmpty()) return nullptr;
  if (count != 0 && array.Length() != count) throw node_gdal::GDALError("Array size must match the number of dimensions");
  std::shared_ptr<RETURN[]> ptr(new RETURN[array.Length()]);
  for (unsigned i = 0; i < array.Length(); i++) {
    Napi::Value val = array.Get(i);
    if (!val.IsNumber()) throw node_gdal::GDALError("Array must contain only numbers");
    ptr.get()[i] = static_cast<RETURN>(val.As<Napi::Number>().DoubleValue());
  }
  return ptr;
}

// the original call sites do not thread the environment through
template <typename INPUT, typename RETURN>
std::shared_ptr<RETURN[]> NumberArrayToSharedPtr(Napi::Array array, size_t count = 0) {
  return NumberArrayToSharedPtr<INPUT, RETURN>(node_gdal::napi_env(), array, count);
}


// Module-level accessor. `PropertyDescriptor::Accessor(name, getter, setter)`
// is not usable: node-addon-api 8.5's own trampoline calls the setter with a
// single argument, so the raw descriptor is built here.
#define GDAL_DEFINE_ACCESSOR(target, name, get, set)                                                                   \
  {                                                                                                                    \
    napi_property_descriptor d = {};                                                                                   \
    d.utf8name = name;                                                                                                 \
    d.getter = &node_gdal::MethodTrampoline<get>;                                                                      \
    d.setter = &node_gdal::SetterTrampoline<set>;                                                                      \
    d.attributes = static_cast<napi_property_attributes>(napi_writable | napi_enumerable | napi_configurable);          \
    (target).DefineProperty(Napi::PropertyDescriptor(d));                                                              \
  }


// ----- constants -------

#define NODE_DEFINE_CONSTANT(target, constant) target.Set(#constant, Napi::Number::New(target.Env(), constant))

NAPI_SETTER(READ_ONLY_SETTER);

// module-level asyncable free functions
#define GDAL_SetAsyncableMethod(env, target, name, method)                                                             \
  target.Set(name, Napi::Function::New(env, &node_gdal::FreeFunctionTrampoline<method>));                              \
  target.Set(name "Async", Napi::Function::New(env, &node_gdal::FreeFunctionTrampoline<method##Async>));

// module-level plain free function
#define GDAL_SetMethod(env, target, name, method)                                                                      \
  target.Set(name, Napi::Function::New(env, &node_gdal::FreeFunctionTrampoline<method>));
//
// The entries of the registry above. A member is a value, not an expansion:
//
//   static constexpr MethodEntry methods[] = {
//     method("toString", toString),
//     asyncMethod("open", open, openAsync),
//   };
//
using node_gdal::AccessorEntry;
using node_gdal::MethodEntry;

inline constexpr MethodEntry method(const char *name, Napi::Value (*fn)(const Napi::CallbackInfo &)) {
  return MethodEntry{ name, fn, nullptr, node_gdal::MEMBER_NONE };
}

inline constexpr MethodEntry asyncMethod(
  const char *name, Napi::Value (*fn)(const Napi::CallbackInfo &), Napi::Value (*fn_async)(const Napi::CallbackInfo &)) {
  return MethodEntry{ name, fn, fn_async, node_gdal::MEMBER_NONE };
}

inline constexpr AccessorEntry attr(const char *name, Napi::Value (*getter)(const Napi::CallbackInfo &),
  void (*setter)(const Napi::CallbackInfo &, const Napi::Value &)) {
  return AccessorEntry{ name, getter, setter, nullptr, nullptr, node_gdal::MEMBER_NONE };
}

inline constexpr AccessorEntry hiddenAttr(const char *name, Napi::Value (*getter)(const Napi::CallbackInfo &),
  void (*setter)(const Napi::CallbackInfo &, const Napi::Value &)) {
  return AccessorEntry{ name, getter, setter, nullptr, nullptr, node_gdal::MEMBER_HIDDEN };
}

inline constexpr AccessorEntry asyncAttr(const char *name, Napi::Value (*getter)(const Napi::CallbackInfo &),
  void (*setter)(const Napi::CallbackInfo &, const Napi::Value &), Napi::Value (*getter_async)(const Napi::CallbackInfo &)) {
  // the asynchronous form of an accessor is always read-only: there is no
  // `xAsync = value`
  return AccessorEntry{ name, getter, setter, getter_async, &READ_ONLY_SETTER, node_gdal::MEMBER_NONE };
}

#endif
