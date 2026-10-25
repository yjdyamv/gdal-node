#include "env_context.hpp"

namespace node_gdal {

thread_local EnvContext *EnvContext::active_env_context = nullptr;

namespace {

// The contexts of this thread, one per environment.
//
// The list is a function-local static because the constructor of a thread_local
// at namespace scope is not run in an add-on which node dlopen()-ed, and the
// context has to exist before the first object is wrapped. It belongs to the
// thread and not to the environment: the objects which V8 finalizes while the
// isolate is torn down run after the cleanup hooks of the environment, and their
// destructors still have to reach the object store. The context therefore
// outlives the environment it describes, and everything which has to happen
// while the environment is still usable happens in its cleanup hook.
std::vector<std::unique_ptr<EnvContext>> &contexts() {
  static thread_local std::vector<std::unique_ptr<EnvContext>> list;
  return list;
}

} // namespace

EnvContext::EnvContext(::napi_env env) : env_(env), thread_id_(std::this_thread::get_id()) {
}

void EnvContext::release() {
  // The constructor references are strong references to values of this
  // environment, so they have to be dropped while it is still usable - that is
  // why this runs from a cleanup hook and not when the context is destroyed
  constructors_.clear();
  symbols_.clear();
}

void EnvContext::releaseHook(void *data) {
  static_cast<EnvContext *>(data)->release();
}

EnvContext *EnvContext::find(::napi_env env) {
  if (env == nullptr) { return nullptr; }

  EnvContext *ctx = active_env_context;
  if (ctx != nullptr && ctx->env() == env) { return ctx; }

  for (auto &existing : contexts()) {
    if (existing->env() == env) {
      active_env_context = existing.get();
      return existing.get();
    }
  }

  // Registered when the environment is first used, so it runs after the hook of
  // the module itself - the cleanup hooks run in the reverse order of their
  // registration, and that one closes the objects which use the symbols and the
  // constructors this releases
  ::napi_add_env_cleanup_hook(env, releaseHook, contexts().emplace_back(new EnvContext(env)).get());
  active_env_context = contexts().back().get();
  return active_env_context;
}

} // namespace node_gdal
