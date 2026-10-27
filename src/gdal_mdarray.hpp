#ifndef __NODE_GDAL_MDARRAY_H__
#define __NODE_GDAL_MDARRAY_H__

// node

// nan
#include "gdal_common.hpp"

// gdal
#include <gdal_priv.h>

// ogr
#include <ogrsf_frmts.h>

#include "async.hpp"

#if GDAL_VERSION_MAJOR > 3 || (GDAL_VERSION_MAJOR == 3 && GDAL_VERSION_MINOR >= 1)

namespace node_gdal {

class MDArray : public GDALObject<MDArray> {
    public:
  static Napi::FunctionReference &constructor();
  static void Initialize(Napi::Object target);
  static Napi::Value New(std::shared_ptr<GDALMDArray> group, GDALDataset *parent_ds);
  GDAL_ASYNCABLE_DECLARE(read);
  static NAPI_GETTER(lengthGetter);

  MDArray(const Napi::CallbackInfo &info);

  // Must be accessible: ObjectWrap's finalizer deletes the instance itself
  ~MDArray();
  inline std::shared_ptr<GDALMDArray> get() {
    return this_;
  }

  inline GDALDataset *getParent() {
    return parent_ds;
  }

  void dispose();
  long uid;
  long parent_uid;
  size_t dimensions;

  inline bool isAlive() {
    return this_ && object_store().isAlive(uid);
  }

    private:
  std::shared_ptr<GDALMDArray> this_;
  GDALDataset *parent_ds;
};

} // namespace node_gdal
#endif
#endif
