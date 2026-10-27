#ifndef __NODE_OGR_FEATURE_H__
#define __NODE_OGR_FEATURE_H__

// node

// nan
#include "gdal_common.hpp"

// ogr
#include <ogrsf_frmts.h>

#include <ogr_featurestyle.h>

namespace node_gdal {

class Feature : public GDALObject<Feature> {
    public:
  static Napi::FunctionReference &constructor();
  static void Initialize(Napi::Object target);
  static Napi::Value New(OGRFeature *feature);
  static Napi::Value New(OGRFeature *feature, bool owned);
  void updateMemory();
  //	static NAPI_METHOD(setGeometryDirectly);
  //  static NAPI_METHOD(stealGeometry);
  static NAPI_METHOD(getFieldDefn);



  Feature(const Napi::CallbackInfo &info);

  // Must be accessible: ObjectWrap's finalizer deletes the instance itself
  ~Feature();
  inline OGRFeature *get() {
    return this_.get();
  }
  inline bool isAlive() {
    return this_;
  }
  void dispose();

    private:
  Handle<OGRFeature> this_;
  // int size_;
};

} // namespace node_gdal
#endif
