#include "layer_features.hpp"
#include "../utils/args.hpp"
#include "../gdal_common.hpp"
#include "../gdal_feature.hpp"
#include "../gdal_layer.hpp"

namespace node_gdal {

Napi::FunctionReference &LayerFeatures::constructor() {
  return node_gdal::env_context().constructor<LayerFeatures>();
}

LayerFeatures::LayerFeatures(const Napi::CallbackInfo &info) : GDALObject<LayerFeatures>(info) {
  if (info.Length() < 1 || !info[0].IsObject()) {
    Napi::Error::New(info.Env(), "Cannot create LayerFeatures directly").ThrowAsJavaScriptException();
    return;
  }
  node_gdal::set_private(info.This(), "parent_", info[0]);
}

LayerFeatures::~LayerFeatures() {
}

/**
 * An encapsulation of a {@link Layer}
 * features.
 *
 * @class LayerFeatures
 */

Napi::Value LayerFeatures::New(Napi::Value layer_obj) {

  std::vector<napi_value> args = {layer_obj};
  Napi::Object obj = LayerFeatures::constructor().Value().New(args);

  return obj;
}

/**
 * Fetch a feature by its identifier.
 *
 * **Important:** The `id` argument is not an index. In most cases it will be
 * zero-based, but in some cases it will not. If iterating, it's best to use the
 * `next()` method.
 *
 * @method get
 * @instance
 * @memberof LayerFeatures
 * @param {number} id The feature ID of the feature to read.
 * @throws {Error}
 * @return {Feature}
 */

/**
 * Fetch a feature by its identifier.
 *
 * **Important:** The `id` argument is not an index. In most cases it will be
 * zero-based, but in some cases it will not. If iterating, it's best to use the
 * `next()` method.
 * @async
 *
 * @method getAsync
 * @instance
 * @memberof LayerFeatures
 * @param {number} id The feature ID of the feature to read.
 * @param {callback<Feature>} [callback=undefined]
 * @throws {Error}
 * @return {Promise<Feature>}
 */
namespace {

struct getBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    Args args(info);

    Napi::Object parent =
      node_gdal::get_private(info.This(), "parent_").As<Napi::Object>();
    Layer *layer = node_gdal::UnwrapWrapped<Layer>(parent);
    if (!layer->isAlive()) {
      Napi::Error::New(node_gdal::napi_env(), "Layer object already destroyed").ThrowAsJavaScriptException();
      return node_gdal::napi_env().Undefined();
    }

    int feature_id;
    if (!args.get(0, "feature id", feature_id)) return args.failed();
    OGRLayer *gdal_layer = layer->get();
    GDALAsyncableJob<OGRFeature *> job(layer->parent_uid);
    job.persist(layer->Value());
    job.main = [gdal_layer, feature_id](const GDALExecutionProgress &) {
      CPLErrorReset();
      OGRFeature *feature = gdal_layer->GetFeature(feature_id);
      if (feature == nullptr) throw GDALError::fromGDAL();
      return feature;
    };
    job.rval = [](OGRFeature *feature, const GetFromPersistentFunc &) { return Feature::New(feature); };
    return job.run(info, node_gdal::is_async<Async>, 1);
  }
};

} // namespace

GDAL_ASYNC_METHOD(LayerFeatures::get, getBody)

/**
 * Resets the feature pointer used by `next()` and
 * returns the first feature in the layer.
 *
 * @method first
 * @instance
 * @memberof LayerFeatures
 * @return {Feature}
 */

/**
 * Resets the feature pointer used by `next()` and
 * returns the first feature in the layer.
 * @async
 *
 * @method firstAsync
 * @instance
 * @memberof LayerFeatures
 * @param {callback<Feature>} [callback=undefined]
 * @return {Promise<Feature>}
 */
namespace {

struct firstBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {

    Napi::Object parent =
      node_gdal::get_private(info.This(), "parent_").As<Napi::Object>();
    Layer *layer = node_gdal::UnwrapWrapped<Layer>(parent);
    if (!layer->isAlive()) {
      Napi::Error::New(node_gdal::napi_env(), "Layer object already destroyed").ThrowAsJavaScriptException();
      return node_gdal::napi_env().Undefined();
    }

    OGRLayer *gdal_layer = layer->get();
    GDALAsyncableJob<OGRFeature *> job(layer->parent_uid);
    job.persist(layer->Value());
    job.main = [gdal_layer](const GDALExecutionProgress &) {
      gdal_layer->ResetReading();
      OGRFeature *feature = gdal_layer->GetNextFeature();
      return feature;
    };
    job.rval = [](OGRFeature *feature, const GetFromPersistentFunc &) { return Feature::New(feature); };
    return job.run(info, node_gdal::is_async<Async>, 0);
  }
};

} // namespace

GDAL_ASYNC_METHOD(LayerFeatures::first, firstBody)

/**
 * Returns the next feature in the layer. Returns null if no more features.
 *
 * @example
 *
 * while (feature = layer.features.next()) { ... }
 *
 * @method next
 * @instance
 * @memberof LayerFeatures
 * @return {Feature}
 */

/**
 * Returns the next feature in the layer. Returns null if no more features.
 * @async
 *
 * @example
 *
 * while (feature = await layer.features.nextAsync()) { ... }
 *
 * @method nextAsync
 * @instance
 * @memberof LayerFeatures
 * @param {callback<Feature>} [callback=undefined]
 * @return {Promise<Feature>}
 */
namespace {

struct nextBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {

    Napi::Object parent =
      node_gdal::get_private(info.This(), "parent_").As<Napi::Object>();
    Layer *layer = node_gdal::UnwrapWrapped<Layer>(parent);
    if (!layer->isAlive()) {
      Napi::Error::New(node_gdal::napi_env(), "Layer object already destroyed").ThrowAsJavaScriptException();
      return node_gdal::napi_env().Undefined();
    }

    OGRLayer *gdal_layer = layer->get();
    GDALAsyncableJob<OGRFeature *> job(layer->parent_uid);
    job.persist(layer->Value());
    job.main = [gdal_layer](const GDALExecutionProgress &) {
      OGRFeature *feature = gdal_layer->GetNextFeature();
      return feature;
    };
    job.rval = [](OGRFeature *feature, const GetFromPersistentFunc &) { return Feature::New(feature); };
    return job.run(info, node_gdal::is_async<Async>, 0);
  }
};

} // namespace

GDAL_ASYNC_METHOD(LayerFeatures::next, nextBody)

/**
 * Adds a feature to the layer. The feature should be created using the current
 * layer as the definition.
 *
 * @example
 *
 * var feature = new gdal.Feature(layer);
 * feature.setGeometry(new gdal.Point(0, 1));
 * feature.fields.set('name', 'somestring');
 * layer.features.add(feature);
 *
 * @method add
 * @instance
 * @memberof LayerFeatures
 * @throws {Error}
 * @param {Feature} feature
 */

/**
 * Adds a feature to the layer. The feature should be created using the current
 * layer as the definition.
 * @async
 *
 * @example
 *
 * var feature = new gdal.Feature(layer);
 * feature.setGeometry(new gdal.Point(0, 1));
 * feature.fields.set('name', 'somestring');
 * await layer.features.addAsync(feature);
 *
 * @method addAsync
 * @instance
 * @memberof LayerFeatures
 * @throws {Error}
 * @param {Feature} feature
 * @param {callback<void>} [callback=undefined]
 * @return {Promise<void>}
 */

namespace {

struct addBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    Args args(info);

    Napi::Object parent =
      node_gdal::get_private(info.This(), "parent_").As<Napi::Object>();
    Layer *layer = node_gdal::UnwrapWrapped<Layer>(parent);
    if (!layer->isAlive()) {
      Napi::Error::New(node_gdal::napi_env(), "Layer object already destroyed").ThrowAsJavaScriptException();
      return node_gdal::napi_env().Undefined();
    }

    Feature *f;
    if (!args.getWrapped(0, "feature", "Feature", f)) return args.failed();

    OGRLayer *gdal_layer = layer->get();
    OGRFeature *gdal_f = f->get();
    GDALAsyncableJob<int> job(layer->parent_uid);
    job.persist(layer->Value());
    job.main = [gdal_layer, gdal_f](const GDALExecutionProgress &) {
      int err = gdal_layer->CreateFeature(gdal_f);
      if (err != CE_None) throw GDALError(getOGRErrMsg(err));
      return err;
    };
    job.rval = [](int, const GetFromPersistentFunc &) { return node_gdal::napi_env().Undefined(); };
    return job.run(info, node_gdal::is_async<Async>, 1);
  }
};

} // namespace

GDAL_ASYNC_METHOD(LayerFeatures::add, addBody)

/**
 * Returns the number of features in the layer.
 *
 * @method count
 * @instance
 * @memberof LayerFeatures
 * @param {boolean} [force=true]
 * @return {number} number of features in the layer.
 */

/**
 * Returns the number of features in the layer.
 * @async
 *
 * @method countAsync
 * @instance
 * @memberof LayerFeatures
 * @param {boolean} [force=true]
 * @param {callback<number>} [callback=undefined]
 * @return {Promise<number>} number of features in the layer.
 */

namespace {

struct countBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    Args args(info);

    Napi::Object parent =
      node_gdal::get_private(info.This(), "parent_").As<Napi::Object>();
    Layer *layer = node_gdal::UnwrapWrapped<Layer>(parent);
    if (!layer->isAlive()) {
      Napi::Error::New(node_gdal::napi_env(), "Layer object already destroyed").ThrowAsJavaScriptException();
      return node_gdal::napi_env().Undefined();
    }

    Napi::Object ds;
    if (object_store().has(layer->getParent())) {
      ds = object_store().get(layer->getParent());
    } else {
      Napi::Error::New(node_gdal::napi_env(), "Dataset object already destroyed").ThrowAsJavaScriptException();
      return node_gdal::napi_env().Undefined();
    }

    int force = 1;
    if (!args.optional(0, "force", force)) return args.failed();

    OGRLayer *gdal_layer = layer->get();
    GDALAsyncableJob<GIntBig> job(layer->parent_uid);
    job.persist(layer->Value());
    job.main = [gdal_layer, force](const GDALExecutionProgress &) {
      GIntBig count = gdal_layer->GetFeatureCount(force);
      return count;
    };
    job.rval = [](GIntBig count, const GetFromPersistentFunc &) { return Napi::Number::New(node_gdal::napi_env(), count); };
    return job.run(info, node_gdal::is_async<Async>, 1);
  }
};

} // namespace

GDAL_ASYNC_METHOD(LayerFeatures::count, countBody)

/**
 * Sets a feature in the layer.
 *
 * @method set
 * @instance
 * @memberof LayerFeatures
 * @throws {Error}
 * @param {Feature} feature
 */

/**
 * Sets a feature in the layer.
 *
 * @method set
 * @instance
 * @memberof LayerFeatures
 * @throws {Error}
 * @param {number} id
 * @param {Feature} feature
 */

/**
 * Sets a feature in the layer.
 * @async
 *
 * @method setAsync
 * @instance
 * @memberof LayerFeatures
 * @throws {Error}
 * @param {number} id
 * @param {Feature} feature
 * @param {callback<Feature>} [callback=undefined]
 * @return {Promise<Feature>}
 */
namespace {

struct setBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    Args args(info);

    Napi::Object parent =
      node_gdal::get_private(info.This(), "parent_").As<Napi::Object>();
    Layer *layer = node_gdal::UnwrapWrapped<Layer>(parent);
    if (!layer->isAlive()) {
      Napi::Error::New(node_gdal::napi_env(), "Layer object already destroyed").ThrowAsJavaScriptException();
      return node_gdal::napi_env().Undefined();
    }

    Napi::Object ds;
    if (object_store().has(layer->getParent())) { ds = object_store().get(layer->getParent()); }
    if (!layer->isAlive()) {
      Napi::Error::New(node_gdal::napi_env(), "Dataset object already destroyed").ThrowAsJavaScriptException();
      return node_gdal::napi_env().Undefined();
    }

    int err;
    Feature *f;

    Napi::Object feature;
    if (info[0].IsObject()) {
      if (!args.getWrapped(0, "feature", "Feature", f)) return args.failed();
      feature = info[0].As<Napi::Object>();
    } else if (info[0].IsNumber()) {
      int i = 0;
      if (!args.get(0, "feature id", i)) return args.failed();
      if (!args.getWrapped(1, "feature", "Feature", f)) return args.failed();
      feature = info[1].As<Napi::Object>();
      err = f->get()->SetFID(i);
      if (err) {
        Napi::Error::New(node_gdal::napi_env(), "Error setting feature id").ThrowAsJavaScriptException();
        return node_gdal::napi_env().Undefined();
      }
    } else {
      Napi::Error::New(node_gdal::napi_env(), "Invalid arguments").ThrowAsJavaScriptException();
      return node_gdal::napi_env().Undefined();
    }

    if (!f->isAlive()) {
      Napi::Error::New(node_gdal::napi_env(), "Feature already destroyed").ThrowAsJavaScriptException();
      return node_gdal::napi_env().Undefined();
    }

    OGRLayer *gdal_layer = layer->get();
    OGRFeature *gdal_feature = f->get();
    GDALAsyncableJob<OGRErr> job(layer->parent_uid);
    job.persist(layer->Value(), f->Value());
    job.main = [gdal_layer, gdal_feature](const GDALExecutionProgress &) {
      OGRErr err = gdal_layer->SetFeature(gdal_feature);
      if (err != CE_None) throw GDALError(getOGRErrMsg(err));
      return err;
    };

    job.rval = [](int, const GetFromPersistentFunc &) { return node_gdal::napi_env().Undefined(); };
    return job.run(info, node_gdal::is_async<Async>, 2);
  }
};

} // namespace

GDAL_ASYNC_METHOD(LayerFeatures::set, setBody)

/**
 * Removes a feature from the layer.
 *
 * @method remove
 * @instance
 * @memberof LayerFeatures
 * @throws {Error}
 * @param {number} id
 */

/**
 * Removes a feature from the layer.
 * @async
 *
 * @method removeAsync
 * @instance
 * @memberof LayerFeatures
 * @throws {Error}
 * @param {number} id
 * @param {callback<void>} [callback=undefined]
 * @return {Promise<void>}
 */

namespace {

struct removeBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    Args args(info);

    Napi::Object parent =
      node_gdal::get_private(info.This(), "parent_").As<Napi::Object>();
    Layer *layer = node_gdal::UnwrapWrapped<Layer>(parent);
    if (!layer->isAlive()) {
      Napi::Error::New(node_gdal::napi_env(), "Layer object already destroyed").ThrowAsJavaScriptException();
      return node_gdal::napi_env().Undefined();
    }

    int i;
    if (!args.get(0, "feature id", i)) return args.failed();

    OGRLayer *gdal_layer = layer->get();
    GDALAsyncableJob<int> job(layer->parent_uid);
    job.persist(layer->Value());
    job.main = [gdal_layer, i](const GDALExecutionProgress &) {
      int err = gdal_layer->DeleteFeature(i);
      if (err) { throw GDALError(getOGRErrMsg(err)); }
      return err;
    };
    job.rval = [](int, const GetFromPersistentFunc &) { return node_gdal::napi_env().Undefined(); };
    return job.run(info, node_gdal::is_async<Async>, 1);

    return node_gdal::napi_env().Undefined();
  }
};

} // namespace

GDAL_ASYNC_METHOD(LayerFeatures::remove, removeBody)

/**
 * Returns the parent layer.
 *
 * @kind member
 * @name layer
 * @instance
 * @memberof LayerFeatures
 * @type {Layer}
 */
namespace {

Napi::Value toString(const Napi::CallbackInfo &info) {
  return Napi::String::New(node_gdal::napi_env(), "LayerFeatures");
}


Napi::Value layerGetter(const Napi::CallbackInfo &info) {
  return node_gdal::get_private(info.This(), "parent_");
}

} // namespace

void LayerFeatures::Initialize(Napi::Object target) {
  Napi::Env env = target.Env();
  static constexpr MethodEntry methods[] = {
    method("toString", toString),
    asyncMethod("count", count, countAsync),
    asyncMethod("add", add, addAsync),
    asyncMethod("get", get, getAsync),
    asyncMethod("set", set, setAsync),
    asyncMethod("first", first, firstAsync),
    asyncMethod("next", next, nextAsync),
    asyncMethod("remove", remove, removeAsync),
  };

  static constexpr AccessorEntry accessors[] = {
    hiddenAttr("layer", layerGetter, READ_ONLY_SETTER),
  };

  Napi::Function lcons = GDALDefineClass<LayerFeatures>(env, "LayerFeatures", methods, accessors);

  target.Set("LayerFeatures", lcons);

  constructor() = Napi::Persistent(lcons);
}

} // namespace node_gdal
