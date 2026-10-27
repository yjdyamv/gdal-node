#ifndef __NODE_OGR_FIELD_DEFN_H__
#define __NODE_OGR_FIELD_DEFN_H__

// node

// nan
#include "gdal_common.hpp"

// ogr
#include <ogrsf_frmts.h>

namespace node_gdal {

class FieldDefn : public GDALObject<FieldDefn> {
    public:
  static Napi::FunctionReference &constructor();
  static void Initialize(Napi::Object target);
  static Napi::Value New(const OGRFieldDefn *def);
  static Napi::Value New(OGRFieldDefn *def, bool owned);



  FieldDefn(const Napi::CallbackInfo &info);

  // Must be accessible: ObjectWrap's finalizer deletes the instance itself
  ~FieldDefn();
  inline OGRFieldDefn *get() {
    return this_.get();
  }
  inline bool isAlive() {
    return this_;
  }

    private:
  Handle<OGRFieldDefn> this_;
};

} // namespace node_gdal
#endif
