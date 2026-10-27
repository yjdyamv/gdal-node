#ifndef __NODE_OGR_FEATURE_DEFN_H__
#define __NODE_OGR_FEATURE_DEFN_H__

// node

// nan
#include "gdal_common.hpp"

// ogr
#include <ogrsf_frmts.h>

namespace node_gdal {

class FeatureDefn : public GDALObject<FeatureDefn> {
    public:
  static Napi::FunctionReference &constructor();
  static void Initialize(Napi::Object target);
  static Napi::Value New(const OGRFeatureDefn *def);
  static Napi::Value New(OGRFeatureDefn *def, bool owned);



  FeatureDefn(const Napi::CallbackInfo &info);

  // Must be accessible: ObjectWrap's finalizer deletes the instance itself
  ~FeatureDefn();
  inline OGRFeatureDefn *get() {
    return this_.get();
  }
  inline bool isAlive() {
    return this_;
  }

    private:
  Handle<OGRFeatureDefn> this_;
};

} // namespace node_gdal
#endif
