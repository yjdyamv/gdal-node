#ifndef __NODE_GDAL_FIELD_DEFN_COLLECTION_H__
#define __NODE_GDAL_FIELD_DEFN_COLLECTION_H__

// node

// nan
#include "../gdal_common.hpp"

// gdal
#include <gdal_priv.h>

// FeatureDefn.fields : FeatureDefnFields

namespace node_gdal {

class FeatureDefnFields : public GDALObject<FeatureDefnFields> {
    public:
  static Napi::FunctionReference &constructor();

  static void Initialize(Napi::Object target);
  static Napi::Value New(Napi::Value layer_obj);


  // - implement in the future -
  // static NAPI_METHOD(alter);


  FeatureDefnFields(const Napi::CallbackInfo &info);

  // Must be accessible: ObjectWrap's finalizer deletes the instance itself
  ~FeatureDefnFields();
    private:
};

} // namespace node_gdal
#endif
