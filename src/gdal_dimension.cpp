#include "gdal_dimension.hpp"
#include "gdal_common.hpp"
#include "gdal_driver.hpp"
#include "gdal_group.hpp"
#include "collections/group_groups.hpp"
#include "collections/group_arrays.hpp"
#include "geometry/gdal_geometry.hpp"
#include "gdal_layer.hpp"
#include "gdal_majorobject.hpp"
#include "gdal_rasterband.hpp"
#include "gdal_spatial_reference.hpp"

namespace node_gdal {

#if GDAL_VERSION_MAJOR > 3 || (GDAL_VERSION_MAJOR == 3 && GDAL_VERSION_MINOR >= 1)

Napi::FunctionReference &Dimension::constructor() {
  return node_gdal::env_context().constructor<Dimension>();
}

void Dimension::Initialize(Napi::Object target) {
  Napi::Env env = target.Env();
  SELF_CLASS(Dimension);

  // NOTE: the descriptor macros carry their own trailing comma
  Napi::Function lcons = GDALDefineClass<SELF>(env, "Dimension",
    {
        METHOD(toString)
        ATTR_DONT_ENUM(lcons, "_uid", uidGetter, READ_ONLY_SETTER)
        ATTR(lcons, "size", sizeGetter, READ_ONLY_SETTER)
        ATTR(lcons, "description", descriptionGetter, READ_ONLY_SETTER)
        ATTR(lcons, "type", typeGetter, READ_ONLY_SETTER)
        ATTR(lcons, "direction", directionGetter, READ_ONLY_SETTER)
    });

  target.Set("Dimension", lcons);

  constructor() = Napi::Persistent(lcons);
}

Dimension::Dimension(const Napi::CallbackInfo &info) : GDALObject<Dimension>(info), uid(0), this_(nullptr), parent_ds(0) {
  std::shared_ptr<GDALDimension> pending = node_gdal::TakePendingShared<GDALDimension>();
  if (pending) {
    this_ = pending;
    LOG("Created dimension [%p]", this_.get());
    return;
  }
  Napi::Error::New(info.Env(), "Cannot create Dimension directly").ThrowAsJavaScriptException();
}

Dimension::~Dimension() {
  dispose();
}

void Dimension::dispose() {
  if (this_) {

    LOG("Disposing dimension [%p]", this_.get());

    object_store().dispose(uid);

    LOG("Disposed dimension [%p]", this_.get());
  }
};

/**
 * A representation of a group with access methods.
 *
 * @class Dimension
 */

Napi::Value Dimension::New(std::shared_ptr<GDALDimension> raw, GDALDataset *parent_ds) {

  if (!raw) { return node_gdal::napi_env().Null(); }
  if (object_store().has(raw)) { return object_store().get(raw); }

  node_gdal::detail::PendingObjectGuard pending(node_gdal::ExportShared(raw));
  std::vector<napi_value> args;
  Napi::Object obj = Dimension::constructor().Value().New(args);
  Dimension *wrapped = node_gdal::UnwrapWrapped<Dimension>(obj);

  Dataset *unwrapped_ds = node_gdal::UnwrapWrapped<Dataset>(object_store().get(parent_ds));
  long parent_uid = unwrapped_ds->uid;

  wrapped->uid = object_store().add(raw, *wrapped, parent_uid);
  wrapped->parent_ds = parent_ds;
  wrapped->parent_uid = parent_uid;


  return obj;
}

NAPI_METHOD(Dimension::toString) {
  return Napi::String::New(node_gdal::napi_env(), "Dimension");
}

/**
 * @readonly
 * @kind member
 * @name size
 * @instance
 * @memberof Dimension
 * @type {number}
 */
namespace {
// Dimension::sizeGetter()
struct sizeGetterBody : node_gdal::SyncMethodBody {
  static Napi::Value run(const Napi::CallbackInfo &info) {
    auto obj = node_gdal::unwrapThis<Dimension>(info, "Dimension");
    if (!obj) { return obj.failure; }
    GDAL_LOCK_PARENT(obj);
    return Number::New(info.Env(), obj->get()->GetSize());
  }
};
} // namespace

GDAL_SYNC_GETTER(Dimension::sizeGetter, sizeGetterBody)

/**
 * @readonly
 * @kind member
 * @name description
 * @instance
 * @memberof Dimension
 * @type {string}
 */
namespace {
// Dimension::descriptionGetter()
struct descriptionGetterBody : node_gdal::SyncMethodBody {
  static Napi::Value run(const Napi::CallbackInfo &info) {
    auto obj = node_gdal::unwrapThis<Dimension>(info, "Dimension");
    if (!obj) { return obj.failure; }
    GDAL_LOCK_PARENT(obj);
    auto r = obj->get()->GetFullName();
    return SafeString::New(info.Env(), r.c_str());
  }
};
} // namespace

GDAL_SYNC_GETTER(Dimension::descriptionGetter, descriptionGetterBody)

/**
 * @readonly
 * @kind member
 * @name direction
 * @instance
 * @memberof Dimension
 * @type {string}
 */
namespace {
// Dimension::directionGetter()
struct directionGetterBody : node_gdal::SyncMethodBody {
  static Napi::Value run(const Napi::CallbackInfo &info) {
    auto obj = node_gdal::unwrapThis<Dimension>(info, "Dimension");
    if (!obj) { return obj.failure; }
    GDAL_LOCK_PARENT(obj);
    auto r = obj->get()->GetDirection();
    return SafeString::New(info.Env(), r.c_str());
  }
};
} // namespace

GDAL_SYNC_GETTER(Dimension::directionGetter, directionGetterBody)

/**
 * @readonly
 * @kind member
 * @name type
 * @instance
 * @memberof Dimension
 * @type {string}
 */
namespace {
// Dimension::typeGetter()
struct typeGetterBody : node_gdal::SyncMethodBody {
  static Napi::Value run(const Napi::CallbackInfo &info) {
    auto obj = node_gdal::unwrapThis<Dimension>(info, "Dimension");
    if (!obj) { return obj.failure; }
    GDAL_LOCK_PARENT(obj);
    auto r = obj->get()->GetType();
    return SafeString::New(info.Env(), r.c_str());
  }
};
} // namespace

GDAL_SYNC_GETTER(Dimension::typeGetter, typeGetterBody)

NAPI_GETTER(Dimension::uidGetter) {
  Dimension *group = node_gdal::UnwrapWrapped<Dimension>(info.This().As<Napi::Object>());
  return Napi::Number::New(node_gdal::napi_env(), (int)group->uid);
}

#endif

} // namespace node_gdal
