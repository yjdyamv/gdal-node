#ifndef __NODE_GDAL_DATASET_H__
#define __NODE_GDAL_DATASET_H__

// gdal
#include <gdal_priv.h>

// ogr
#include <ogrsf_frmts.h>

#include "async.hpp"
#include "gdal_common.hpp"

namespace node_gdal {

// > GDAL 2.0 : a wrapper for GDALDataset
// < GDAL 2.0 : a wrapper for either a GDALDataset or OGRDataSource that behaves
// like a 2.0 Dataset

class Dataset : public GDALObject<Dataset> {
    public:
  static Napi::FunctionReference &constructor();
  static void Initialize(Napi::Object target);
  static Napi::Value New(GDALDataset *ds, GDALDataset *parent = nullptr, bool close = true, bool shared_lock = true);
  GDAL_ASYNCABLE_DECLARE(flush);
  GDAL_ASYNCABLE_DECLARE(getMetadata);
  GDAL_ASYNCABLE_DECLARE(setMetadata);
  GDAL_ASYNCABLE_DECLARE(executeSQL);
  GDAL_ASYNCABLE_DECLARE(buildOverviews);

  GDAL_ASYNCABLE_GETTER_DECLARE(rasterSizeGetter);
  GDAL_ASYNCABLE_GETTER_DECLARE(srsGetter);
  GDAL_ASYNCABLE_GETTER_DECLARE(geoTransformGetter);


  Dataset(const Napi::CallbackInfo &info);
  // Must be accessible: ObjectWrap's finalizer deletes the instance itself
  ~Dataset();

  inline GDALDataset *get() {
    return this_dataset;
  }

  void dispose(bool manual);
  void updateMemory();
  long uid;
  long parent_uid;

  inline bool isAlive() {
    return this_dataset && object_store().isAlive(uid);
  }

    private:
  GDALDataset *this_dataset;
  GDALDataset *parent_ds;
};

} // namespace node_gdal
#endif
