#ifndef NODE_GDAL_ENV_CONTEXT_HPP
#define NODE_GDAL_ENV_CONTEXT_HPP

#include "../napi-wrapper.h"

#include "ptr_manager.hpp"

#include <map>
#include <string>
#include <thread>
#include <typeindex>
#include <typeinfo>
#include <vector>

namespace node_gdal {

/**
 * The state which belongs to one Node-API environment.
 *
 * Every V8 isolate - the main thread and each worker_thread - has its own
 * environment and the addon keeps state for it: the object store, the interned
 * private property symbols, the constructor of every wrapped class, the pointer
 * a wrapper factory hands to the constructor and the two flags which say that
 * the environment is going away. All of it used to live in `thread_local`
 * variables, each with its own environment cleanup hook, which made the
 * teardown order something to be reasoned about by hand and made it impossible
 * to tell two loadings of the addon apart.
 *
 * There is one context per environment and it is found by the environment it
 * belongs to, so two loadings of the addon in one isolate share it. It outlives
 * the environment, though: V8 finalizes the objects which are still alive when
 * the isolate is torn down after the cleanup hooks of the environment have run,
 * and their destructors still have to reach the object store. The cleanup hook
 * therefore releases the symbols and the constructors, which are only usable
 * while the environment is alive, and leaves the rest to the thread's exit.
 *
 * @class EnvContext
 */
class EnvContext {
    public:
  explicit EnvContext(::napi_env env);
  ~EnvContext() = default;

  EnvContext(const EnvContext &) = delete;
  EnvContext &operator=(const EnvContext &) = delete;

  ::napi_env env() const {
    return env_;
  }

  // The thread which serves this environment: the only one on which the JS
  // values of the environment may be touched
  std::thread::id thread_id() const {
    return thread_id_;
  }

  // The interned private property symbols, see node_gdal::PrivateKey()
  std::map<std::string, Napi::Reference<Napi::Symbol>> &symbols() {
    return symbols_;
  }

  // The constructor of a wrapped class, one per class per environment. The
  // reference is empty until the class has been initialized
  template <typename T> Napi::FunctionReference &constructor() {
    return constructors_[std::type_index(typeid(T))];
  }

  // The object store of this environment
  ObjectStore &store() {
    return store_;
  }

  // The pointer which a wrapper factory of the calling thread handed to the
  // constructor of its class, see node_gdal::detail::PendingObjectGuard
  const void *&pending_object() {
    return pending_object_;
  }

  // Set while the environment is being torn down, when the JS values of the
  // environment may no longer be touched
  bool &shutting_down() {
    return shutting_down_;
  }

  // Drops what is only usable while the environment is alive
  void release();

  // The context of the environment the calling thread serves, null when the
  // thread serves none (the thread of an asynchronous operation, for one)
  static EnvContext *active() {
    return active_env_context;
  }

  // The context of `env`, created on first use. Never null when `env` is not.
  static EnvContext *find(::napi_env env);

    private:
  // The environment cleanup hook: runs while the environment is still usable,
  // but after the hook of the module itself, which closes the objects
  static void releaseHook(void *data);

  ::napi_env env_;
  std::thread::id thread_id_;
  ObjectStore store_;
  std::map<std::string, Napi::Reference<Napi::Symbol>> symbols_;
  std::map<std::type_index, Napi::FunctionReference> constructors_;
  const void *pending_object_ = nullptr;
  bool shutting_down_ = false;

  static thread_local EnvContext *active_env_context;
};

} // namespace node_gdal

#endif
