#include "gdal_group.hpp"
#include "gdal_common.hpp"
#include "gdal_driver.hpp"
#include "collections/group_groups.hpp"
#include "collections/group_arrays.hpp"
#include "collections/group_dimensions.hpp"
#include "collections/group_attributes.hpp"
#include "geometry/gdal_geometry.hpp"
#include "gdal_layer.hpp"
#include "gdal_majorobject.hpp"
#include "gdal_rasterband.hpp"
#include "gdal_spatial_reference.hpp"

namespace node_gdal {

#if GDAL_VERSION_MAJOR > 3 || (GDAL_VERSION_MAJOR == 3 && GDAL_VERSION_MINOR >= 1)

Napi::FunctionReference &Group::constructor() {
  return node_gdal::env_context().constructor<Group>();
}

Group::Group(const Napi::CallbackInfo &info) : GDALObject<Group>(info), uid(0), this_(nullptr), parent_ds(0) {
  std::shared_ptr<GDALGroup> pending = node_gdal::TakePendingShared<GDALGroup>();
  if (pending) {
    this_ = pending;
    LOG("Created group [%p]", this_.get());
  } else {
    Napi::Error::New(info.Env(), "Cannot create Group directly").ThrowAsJavaScriptException();
    return;
  }

  // the parent dataset travels as the second argument, for the sub-collections
  Napi::Value parent_ds = info.Length() > 0 ? info[0] : info.Env().Undefined();
  node_gdal::set_private(info.This(), "groups_", GroupGroups::New(info.This(), parent_ds));
  node_gdal::set_private(info.This(), "arrays_", GroupArrays::New(info.This(), parent_ds));
  node_gdal::set_private(info.This(), "dims_", GroupDimensions::New(info.This(), parent_ds));
  node_gdal::set_private(info.This(), "attrs_", GroupAttributes::New(info.This(), parent_ds));
}

Group::~Group() {
  dispose();
}

void Group::dispose() {
  if (this_) {

    LOG("Disposing group [%p]", this_.get());

    object_store().dispose(uid);

    LOG("Disposed group [%p]", this_.get());
  }
};

/**
 * A representation of a group with access methods.
 *
 * @class Group
 */

Napi::Value Group::New(std::shared_ptr<GDALGroup> raw, GDALDataset *parent_ds) {

  if (object_store().has(parent_ds)) {
    Napi::Object ds = object_store().get(parent_ds);
    return Group::New(raw, ds);
  } else {
    LOG("Group's parent dataset disappeared from cache (group = %p, dataset = %p)", raw.get(), parent_ds);
    Napi::Error::New(node_gdal::napi_env(), "Group's parent dataset disappeared from cache").ThrowAsJavaScriptException();
    return node_gdal::napi_env().Undefined();
  }
}

Napi::Value Group::New(std::shared_ptr<GDALGroup> raw, Napi::Object parent_ds) {

  if (!raw) { return node_gdal::napi_env().Null(); }
  if (object_store().has(raw)) { return object_store().get(raw); }

  node_gdal::detail::PendingObjectGuard pending(node_gdal::ExportShared(raw));
  std::vector<napi_value> args = {parent_ds};
  Napi::Object obj = Group::constructor().Value().New(args);
  Group *wrapped = node_gdal::UnwrapWrapped<Group>(obj);

  long parent_group_uid = 0;
  Napi::Object parent;

  Dataset *unwrapped_ds = node_gdal::UnwrapWrapped<Dataset>(parent_ds);
  long parent_uid = unwrapped_ds->uid;

  wrapped->uid = object_store().add(raw, *wrapped, parent_uid);
  wrapped->parent_ds = unwrapped_ds->get();
  wrapped->parent_uid = parent_uid;
  if (parent_group_uid != 0) node_gdal::set_private(obj, "parent_", parent);

  return obj;
}

/**
 * @readonly
 * @kind member
 * @name description
 * @instance
 * @memberof Group
 * @type {string}
 */
namespace {
// Group::descriptionGetter()
struct descriptionGetterBody : node_gdal::SyncMethodBody {
  static Napi::Value run(const Napi::CallbackInfo &info) {
    auto obj = node_gdal::unwrapThis<Group>(info, "Group");
    if (!obj) { return obj.failure; }
    GDAL_LOCK_PARENT(obj);
    auto r = obj->get()->GetFullName();
    return SafeString::New(info.Env(), r.c_str());
  }
};
} // namespace

GDAL_SYNC_GETTER(Group::descriptionGetter, descriptionGetterBody)

/**
 * @readonly
 * @kind member
 * @name groups
 * @instance
 * @memberof Group
 * @type {GroupGroups}
 */
/**
 * @readonly
 * @kind member
 * @name arrays
 * @instance
 * @memberof Group
 * @type {GroupArrays}
 */
/**
 * @readonly
 * @kind member
 * @name dimensions
 * @instance
 * @memberof Group
 * @type {GroupDimensions}
 */
/**
 * @readonly
 * @kind member
 * @name attributes
 * @instance
 * @memberof Group
 * @type {GroupAttributes}
 */
#endif

namespace {

Napi::Value toString(const Napi::CallbackInfo &info) {
  return Napi::String::New(node_gdal::napi_env(), "Group");
}


Napi::Value groupsGetter(const Napi::CallbackInfo &info) {
  node_gdal::requireAlive<Group>(info.This(), "Group");
  return node_gdal::get_private(info.This(), "groups_");
}


Napi::Value arraysGetter(const Napi::CallbackInfo &info) {
  node_gdal::requireAlive<Group>(info.This(), "Group");
  return node_gdal::get_private(info.This(), "arrays_");
}


Napi::Value dimensionsGetter(const Napi::CallbackInfo &info) {
  node_gdal::requireAlive<Group>(info.This(), "Group");
  return node_gdal::get_private(info.This(), "dims_");
}


Napi::Value attributesGetter(const Napi::CallbackInfo &info) {
  node_gdal::requireAlive<Group>(info.This(), "Group");
  return node_gdal::get_private(info.This(), "attrs_");
}


Napi::Value uidGetter(const Napi::CallbackInfo &info) {
  Group *group = node_gdal::UnwrapWrapped<Group>(info.This().As<Napi::Object>());
  return Napi::Number::New(node_gdal::napi_env(), (int)group->uid);
}

} // namespace

void Group::Initialize(Napi::Object target) {
  Napi::Env env = target.Env();
  static constexpr MethodEntry methods[] = {
    method("toString", toString),
  };

  static constexpr AccessorEntry accessors[] = {
    hiddenAttr("_uid", uidGetter, READ_ONLY_SETTER),
    attr("description", descriptionGetter, READ_ONLY_SETTER),
    attr("groups", groupsGetter, READ_ONLY_SETTER),
    attr("arrays", arraysGetter, READ_ONLY_SETTER),
    attr("dimensions", dimensionsGetter, READ_ONLY_SETTER),
    attr("attributes", attributesGetter, READ_ONLY_SETTER),
  };

  Napi::Function lcons = GDALDefineClass<Group>(env, "Group", methods, accessors);

  target.Set("Group", lcons);

  constructor() = Napi::Persistent(lcons);
}

} // namespace node_gdal
