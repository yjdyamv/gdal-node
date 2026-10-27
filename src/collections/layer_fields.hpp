#ifndef __NODE_GDAL_LYR_FIELD_DEFN_COLLECTION_H__
#define __NODE_GDAL_LYR_FIELD_DEFN_COLLECTION_H__

// node

// nan
#include "../gdal_common.hpp"

// gdal
#include <gdal_priv.h>

// Layer.fields : LayerFields

// Identical to FeatureDefn.fields object from the outside
// but on the inside it uses the parent layer
// to create/modify fields instead of illegally
// adding them directly to the layer definition

namespace node_gdal {

class LayerFields : public GDALObject<LayerFields> {
    public:
  static Napi::FunctionReference &constructor();

  static void Initialize(Napi::Object target);
  static Napi::Value New(Napi::Value layer_obj);


  // - implement in the future -
  // static NAPI_METHOD(alter);


  LayerFields(const Napi::CallbackInfo &info);

  // Must be accessible: ObjectWrap's finalizer deletes the instance itself
  ~LayerFields();
    private:
};

} // namespace node_gdal
#endif
