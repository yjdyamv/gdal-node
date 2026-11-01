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
#include "utils/string_list.hpp"

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

// The multidimensional objects are created by their parent, so the arguments
// of every creation method begin with a name and a data type, and the data type
// is given as the name of a {@link GDT} constant - the same string which
// `dataType` reports. `GDALExtendedDataType` has no default constructor and is
// not copy-assignable, so the type is parsed where it is used and only its two
// ingredients are carried around until then.
struct DataTypeName {
  bool is_string;
  GDALDataType base;

  inline GDALExtendedDataType create() const {
    return is_string ? GDALExtendedDataType::CreateString() : GDALExtendedDataType::Create(base);
  }
};

inline bool parseDataTypeName(const std::string &name, DataTypeName &out) {
  if (name == "String") {
    out.is_string = true;
    out.base = GDT_Unknown;
    return true;
  }
  GDALDataType type = GDALGetDataTypeByName(name.c_str());
  if (type == GDT_Unknown) { return false; }
  out.is_string = false;
  out.base = type;
  return true;
}

} // namespace

/**
 * Create a subgroup.
 *
 * @method createGroup
 * @instance
 * @memberof Group
 * @throws {Error}
 * @param {string} name
 * @param {StringOptions} [options] Driver-specific creation options
 * @return {Group}
 */

/**
 * Create a subgroup.
 * @async
 *
 * @method createGroupAsync
 * @instance
 * @memberof Group
 * @throws {Error}
 * @param {string} name
 * @param {StringOptions} [options] Driver-specific creation options
 * @param {callback<Group>} [callback=undefined]
 * @return {Promise<Group>}
 */
namespace {

struct createGroupBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    Args args(info);
    Group *self = node_gdal::unwrap_or_throw<Group>(info.This(), "Group");
    std::shared_ptr<GDALGroup> raw = node_gdal::raw_or_throw<std::shared_ptr<GDALGroup>>(self, "std::shared_ptr<GDALGroup>");
    GDALDataset *gdal_ds = self->getParent();

    std::string name;
    StringList *options = new StringList;
    if (!args.get(0, "name", name)) return args.failed();
    if (info.Length() > 1 && options->parse(info[1])) {
      node_gdal::ThrowOverPending(info.Env(), "Failed parsing options");
      return info.Env().Undefined();
    }

    GDALAsyncableJob<std::shared_ptr<GDALGroup>> job(self->parent_uid);
    job.persist(self->Value());
    job.main = [raw, name, options](const GDALExecutionProgress &) {
      std::unique_ptr<StringList> options_ptr(options);
      CPLErrorReset();
      std::shared_ptr<GDALGroup> result = raw->CreateGroup(name, options->get());
      if (result == nullptr) { throw GDALError::fromGDAL(); }
      return result;
    };
    job.rval = [gdal_ds](std::shared_ptr<GDALGroup> r, const GetFromPersistentFunc &) { return Group::New(r, gdal_ds); };
    return job.run(info, node_gdal::is_async<Async>, 2);
  }
};

} // namespace

GDAL_ASYNC_METHOD(Group::createGroup, createGroupBody)

/**
 * Create a dimension.
 *
 * The dimensions of a group are what the arrays of the group are indexed by:
 * an array is created from a list of dimensions.
 *
 * @method createDimension
 * @instance
 * @memberof Group
 * @throws {Error}
 * @param {string} name
 * @param {string} type The axis type, one of `HORIZONTAL_X`, `HORIZONTAL_Y`,
 * `VERTICAL`, `TEMPORAL` or `PARAMETRIC`. An empty string means unknown.
 * @param {string} direction `EAST`, `WEST`, `NORTH`, `SOUTH`, `UP`, `DOWN` or
 * an empty string for none
 * @param {number} size
 * @param {StringOptions} [options] Driver-specific creation options
 * @return {Dimension}
 */

/**
 * Create a dimension.
 * @async
 *
 * @method createDimensionAsync
 * @instance
 * @memberof Group
 * @throws {Error}
 * @param {string} name
 * @param {string} type The axis type, one of `HORIZONTAL_X`, `HORIZONTAL_Y`,
 * `VERTICAL`, `TEMPORAL` or `PARAMETRIC`. An empty string means unknown.
 * @param {string} direction `EAST`, `WEST`, `NORTH`, `SOUTH`, `UP`, `DOWN` or
 * an empty string for none
 * @param {number} size
 * @param {StringOptions} [options] Driver-specific creation options
 * @param {callback<Dimension>} [callback=undefined]
 * @return {Promise<Dimension>}
 */
namespace {

struct createDimensionBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    Args args(info);
    Group *self = node_gdal::unwrap_or_throw<Group>(info.This(), "Group");
    std::shared_ptr<GDALGroup> raw = node_gdal::raw_or_throw<std::shared_ptr<GDALGroup>>(self, "std::shared_ptr<GDALGroup>");
    GDALDataset *gdal_ds = self->getParent();

    std::string name, type, direction;
    double size = 0;
    StringList *options = new StringList;
    if (!args.get(0, "name", name)) return args.failed();
    if (!args.get(1, "type", type)) return args.failed();
    if (!args.get(2, "direction", direction)) return args.failed();
    if (!args.get(3, "size", size)) return args.failed();
    if (info.Length() > 4 && options->parse(info[4])) {
      node_gdal::ThrowOverPending(info.Env(), "Failed parsing options");
      return info.Env().Undefined();
    }
    if (size < 0) {
      delete options;
      Napi::RangeError::New(info.Env(), "size must not be negative").ThrowAsJavaScriptException();
      return info.Env().Undefined();
    }

    GDALAsyncableJob<std::shared_ptr<GDALDimension>> job(self->parent_uid);
    job.persist(self->Value());
    job.main = [raw, name, type, direction, size, options](const GDALExecutionProgress &) {
      std::unique_ptr<StringList> options_ptr(options);
      CPLErrorReset();
      std::shared_ptr<GDALDimension> result =
        raw->CreateDimension(name, type, direction, (GUInt64)size, options->get());
      if (result == nullptr) { throw GDALError::fromGDAL(); }
      return result;
    };
    job.rval = [gdal_ds](std::shared_ptr<GDALDimension> r, const GetFromPersistentFunc &) {
      return Dimension::New(r, gdal_ds);
    };
    return job.run(info, node_gdal::is_async<Async>, 5);
  }
};

} // namespace

GDAL_ASYNC_METHOD(Group::createDimension, createDimensionBody)

/**
 * Create an array.
 *
 * @method createMDArray
 * @instance
 * @memberof Group
 * @throws {Error}
 * @param {string} name
 * @param {Dimension[]} dimensions The dimensions the array is indexed by
 * @param {string} data_type See {@link GDT|GDT constants}, or `"String"`
 * @param {StringOptions} [options] Driver-specific creation options
 * @return {MDArray}
 */

/**
 * Create an array.
 * @async
 *
 * @method createMDArrayAsync
 * @instance
 * @memberof Group
 * @throws {Error}
 * @param {string} name
 * @param {Dimension[]} dimensions The dimensions the array is indexed by
 * @param {string} data_type See {@link GDT|GDT constants}, or `"String"`
 * @param {StringOptions} [options] Driver-specific creation options
 * @param {callback<MDArray>} [callback=undefined]
 * @return {Promise<MDArray>}
 */
namespace {

struct createMDArrayBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    Args args(info);
    Group *self = node_gdal::unwrap_or_throw<Group>(info.This(), "Group");
    std::shared_ptr<GDALGroup> raw = node_gdal::raw_or_throw<std::shared_ptr<GDALGroup>>(self, "std::shared_ptr<GDALGroup>");
    GDALDataset *gdal_ds = self->getParent();

    std::string name, type_name;
    Napi::Array dimensions;
    StringList *options = new StringList;
    if (!args.get(0, "name", name)) return args.failed();
    if (!args.get(1, "dimensions", dimensions)) return args.failed();
    if (!args.get(2, "data type", type_name)) return args.failed();
    if (info.Length() > 3 && options->parse(info[3])) {
      node_gdal::ThrowOverPending(info.Env(), "Failed parsing options");
      return info.Env().Undefined();
    }

    std::vector<std::shared_ptr<GDALDimension>> gdal_dimensions;
    for (uint32_t i = 0; i < dimensions.Length(); i++) {
      Napi::Value dim = dimensions.Get(i);
      if (!node_gdal::IsInstanceOf<Dimension>(dim)) {
        delete options;
        Napi::TypeError::New(info.Env(), "dimensions must contain only Dimension objects").ThrowAsJavaScriptException();
        return info.Env().Undefined();
      }
      Dimension *dim_obj = node_gdal::UnwrapWrapped<Dimension>(dim.As<Napi::Object>());
      gdal_dimensions.push_back(dim_obj->get());
    }

    DataTypeName data_type;
    if (!parseDataTypeName(type_name, data_type)) {
      delete options;
      Napi::TypeError::New(info.Env(), "Invalid data type: " + type_name).ThrowAsJavaScriptException();
      return info.Env().Undefined();
    }

    GDALAsyncableJob<std::shared_ptr<GDALMDArray>> job(self->parent_uid);
    job.persist(self->Value());
    job.main = [raw, name, gdal_dimensions, data_type, options](const GDALExecutionProgress &) {
      std::unique_ptr<StringList> options_ptr(options);
      CPLErrorReset();
      std::shared_ptr<GDALMDArray> result = raw->CreateMDArray(name, gdal_dimensions, data_type.create(), options->get());
      if (result == nullptr) { throw GDALError::fromGDAL(); }
      return result;
    };
    job.rval = [gdal_ds](std::shared_ptr<GDALMDArray> r, const GetFromPersistentFunc &) {
      return MDArray::New(r, gdal_ds);
    };
    return job.run(info, node_gdal::is_async<Async>, 4);
  }
};

} // namespace

GDAL_ASYNC_METHOD(Group::createMDArray, createMDArrayBody)

/**
 * Create an attribute.
 *
 * Attributes are the metadata of a group or of an array. Their value is written
 * with {@link Attribute#value}.
 *
 * @method createAttribute
 * @instance
 * @memberof Group
 * @throws {Error}
 * @param {string} name
 * @param {number[]} [dimensions] The sizes of the dimensions of the attribute,
 * an empty array for a scalar
 * @param {string} [data_type=String] See {@link GDT|GDT constants}, or `"String"`
 * @param {StringOptions} [options] Driver-specific creation options
 * @return {Attribute}
 */

/**
 * Create an attribute.
 * @async
 *
 * @method createAttributeAsync
 * @instance
 * @memberof Group
 * @throws {Error}
 * @param {string} name
 * @param {number[]} [dimensions] The sizes of the dimensions of the attribute,
 * an empty array for a scalar
 * @param {string} [data_type=String] See {@link GDT|GDT constants}, or `"String"`
 * @param {StringOptions} [options] Driver-specific creation options
 * @param {callback<Attribute>} [callback=undefined]
 * @return {Promise<Attribute>}
 */
namespace {

struct createAttributeBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    Args args(info);
    Group *self = node_gdal::unwrap_or_throw<Group>(info.This(), "Group");
    std::shared_ptr<GDALGroup> raw = node_gdal::raw_or_throw<std::shared_ptr<GDALGroup>>(self, "std::shared_ptr<GDALGroup>");
    GDALDataset *gdal_ds = self->getParent();

    std::string name, type_name = "String";
    Napi::Array dimensions;
    StringList *options = new StringList;
    if (!args.get(0, "name", name)) return args.failed();
    if (!args.optional(1, "dimensions", dimensions)) return args.failed();
    if (!args.optional(2, "data type", type_name)) return args.failed();
    if (info.Length() > 3 && options->parse(info[3])) {
      node_gdal::ThrowOverPending(info.Env(), "Failed parsing options");
      return info.Env().Undefined();
    }

    std::shared_ptr<GUInt64[]> gdal_dimensions;
    size_t count = 0;
    try {
      gdal_dimensions = NumberArrayToSharedPtr<int64_t, GUInt64>(dimensions, 0);
      if (gdal_dimensions != nullptr) count = dimensions.Length();
    } catch (const char *e) {
      delete options;
      Napi::Error::New(info.Env(), e).ThrowAsJavaScriptException();
      return info.Env().Undefined();
    }

    DataTypeName data_type;
    if (!parseDataTypeName(type_name, data_type)) {
      delete options;
      Napi::TypeError::New(info.Env(), "Invalid data type: " + type_name).ThrowAsJavaScriptException();
      return info.Env().Undefined();
    }

    std::vector<GUInt64> sizes(gdal_dimensions.get(), gdal_dimensions.get() + count);

    GDALAsyncableJob<std::shared_ptr<GDALAttribute>> job(self->parent_uid);
    job.persist(self->Value());
    job.main = [raw, name, sizes, data_type, options](const GDALExecutionProgress &) {
      std::unique_ptr<StringList> options_ptr(options);
      CPLErrorReset();
      std::shared_ptr<GDALAttribute> result = raw->CreateAttribute(name, sizes, data_type.create(), options->get());
      if (result == nullptr) { throw GDALError::fromGDAL(); }
      return result;
    };
    job.rval = [gdal_ds](std::shared_ptr<GDALAttribute> r, const GetFromPersistentFunc &) {
      return Attribute::New(r, gdal_ds);
    };
    return job.run(info, node_gdal::is_async<Async>, 4);
  }
};

} // namespace

GDAL_ASYNC_METHOD(Group::createAttribute, createAttributeBody)

/**
 * Delete a subgroup.
 *
 * @method deleteGroup
 * @instance
 * @memberof Group
 * @throws {Error}
 * @param {string} name
 * @return {boolean}
 */

/**
 * Delete a subgroup.
 * @async
 *
 * @method deleteGroupAsync
 * @instance
 * @memberof Group
 * @throws {Error}
 * @param {string} name
 * @param {callback<boolean>} [callback=undefined]
 * @return {Promise<boolean>}
 */
namespace {

struct deleteGroupBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    Args args(info);
    Group *self = node_gdal::unwrap_or_throw<Group>(info.This(), "Group");
    std::shared_ptr<GDALGroup> raw = node_gdal::raw_or_throw<std::shared_ptr<GDALGroup>>(self, "std::shared_ptr<GDALGroup>");

    std::string name;
    if (!args.get(0, "name", name)) return args.failed();

    GDALAsyncableJob<bool> job(self->parent_uid);
    job.persist(self->Value());
    job.main = [raw, name](const GDALExecutionProgress &) {
      CPLErrorReset();
      bool result = raw->DeleteGroup(name, nullptr);
      if (!result) { throw GDALError::fromGDAL(); }
      return result;
    };
    job.rval = [](bool r, const GetFromPersistentFunc &) { return Napi::Boolean::New(node_gdal::napi_env(), r); };
    return job.run(info, node_gdal::is_async<Async>, 1);
  }
};

} // namespace

GDAL_ASYNC_METHOD(Group::deleteGroup, deleteGroupBody)

/**
 * Delete an array.
 *
 * @method deleteMDArray
 * @instance
 * @memberof Group
 * @throws {Error}
 * @param {string} name
 * @return {boolean}
 */

/**
 * Delete an array.
 * @async
 *
 * @method deleteMDArrayAsync
 * @instance
 * @memberof Group
 * @throws {Error}
 * @param {string} name
 * @param {callback<boolean>} [callback=undefined]
 * @return {Promise<boolean>}
 */
namespace {

struct deleteMDArrayBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    Args args(info);
    Group *self = node_gdal::unwrap_or_throw<Group>(info.This(), "Group");
    std::shared_ptr<GDALGroup> raw = node_gdal::raw_or_throw<std::shared_ptr<GDALGroup>>(self, "std::shared_ptr<GDALGroup>");

    std::string name;
    if (!args.get(0, "name", name)) return args.failed();

    GDALAsyncableJob<bool> job(self->parent_uid);
    job.persist(self->Value());
    job.main = [raw, name](const GDALExecutionProgress &) {
      CPLErrorReset();
      bool result = raw->DeleteMDArray(name, nullptr);
      if (!result) { throw GDALError::fromGDAL(); }
      return result;
    };
    job.rval = [](bool r, const GetFromPersistentFunc &) { return Napi::Boolean::New(node_gdal::napi_env(), r); };
    return job.run(info, node_gdal::is_async<Async>, 1);
  }
};

} // namespace

GDAL_ASYNC_METHOD(Group::deleteMDArray, deleteMDArrayBody)

/**
 * Delete an attribute.
 *
 * @method deleteAttribute
 * @instance
 * @memberof Group
 * @throws {Error}
 * @param {string} name
 * @return {boolean}
 */

/**
 * Delete an attribute.
 * @async
 *
 * @method deleteAttributeAsync
 * @instance
 * @memberof Group
 * @throws {Error}
 * @param {string} name
 * @param {callback<boolean>} [callback=undefined]
 * @return {Promise<boolean>}
 */
namespace {

struct deleteAttributeBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    Args args(info);
    Group *self = node_gdal::unwrap_or_throw<Group>(info.This(), "Group");
    std::shared_ptr<GDALGroup> raw = node_gdal::raw_or_throw<std::shared_ptr<GDALGroup>>(self, "std::shared_ptr<GDALGroup>");

    std::string name;
    if (!args.get(0, "name", name)) return args.failed();

    GDALAsyncableJob<bool> job(self->parent_uid);
    job.persist(self->Value());
    job.main = [raw, name](const GDALExecutionProgress &) {
      CPLErrorReset();
      bool result = raw->DeleteAttribute(name, nullptr);
      if (!result) { throw GDALError::fromGDAL(); }
      return result;
    };
    job.rval = [](bool r, const GetFromPersistentFunc &) { return Napi::Boolean::New(node_gdal::napi_env(), r); };
    return job.run(info, node_gdal::is_async<Async>, 1);
  }
};

} // namespace

GDAL_ASYNC_METHOD(Group::deleteAttribute, deleteAttributeBody)

/**
 * Rename the group.
 *
 * @method rename
 * @instance
 * @memberof Group
 * @throws {Error}
 * @param {string} newName
 * @return {void}
 */

/**
 * Rename the group.
 * @async
 *
 * @method renameAsync
 * @instance
 * @memberof Group
 * @throws {Error}
 * @param {string} newName
 * @param {callback<void>} [callback=undefined]
 * @return {Promise<void>}
 */
namespace {

struct renameBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    Args args(info);
    Group *self = node_gdal::unwrap_or_throw<Group>(info.This(), "Group");
    std::shared_ptr<GDALGroup> raw = node_gdal::raw_or_throw<std::shared_ptr<GDALGroup>>(self, "std::shared_ptr<GDALGroup>");

    std::string new_name;
    if (!args.get(0, "new name", new_name)) return args.failed();

    GDALAsyncableJob<bool> job(self->parent_uid);
    job.persist(self->Value());
    job.main = [raw, new_name](const GDALExecutionProgress &) {
      CPLErrorReset();
      bool result = raw->Rename(new_name);
      if (!result) { throw GDALError::fromGDAL(); }
      return result;
    };
    job.rval = [](bool, const GetFromPersistentFunc &) { return v8_undefined(); };
    return job.run(info, node_gdal::is_async<Async>, 1);
  }
};

} // namespace

GDAL_ASYNC_METHOD(Group::rename, renameBody)

void Group::Initialize(Napi::Object target) {
  Napi::Env env = target.Env();
  static constexpr MethodEntry methods[] = {
    method("toString", toString),
    asyncMethod("createGroup", createGroup, createGroupAsync),
    asyncMethod("createDimension", createDimension, createDimensionAsync),
    asyncMethod("createMDArray", createMDArray, createMDArrayAsync),
    asyncMethod("createAttribute", createAttribute, createAttributeAsync),
    asyncMethod("deleteGroup", deleteGroup, deleteGroupAsync),
    asyncMethod("deleteMDArray", deleteMDArray, deleteMDArrayAsync),
    asyncMethod("deleteAttribute", deleteAttribute, deleteAttributeAsync),
    asyncMethod("rename", rename, renameAsync),
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
