#ifndef __NODE_GDAL_RASTERATTRIBUTETABLE_H__
#define __NODE_GDAL_RASTERATTRIBUTETABLE_H__

// node

// nan
#include "gdal_common.hpp"

// gdal
#include <gdal_priv.h>
#include <gdal_rat.h>

#include "async.hpp"

namespace node_gdal {

class RasterAttributeTable : public GDALObject<RasterAttributeTable> {
    public:
  static Napi::FunctionReference &constructor();
  static void Initialize(Napi::Object target);
  // A table which is owned by a band is handed out read-only: writing to it
  // would change the table of the band behind its back
  static Napi::Value New(GDALRasterAttributeTable *rat, Napi::Value parent);
  // A standalone table, created either by the user or by `clone()`
  static Napi::Value New(GDALRasterAttributeTable *rat);

  RasterAttributeTable(const Napi::CallbackInfo &info);

  // Must be accessible: ObjectWrap's finalizer deletes the instance itself
  ~RasterAttributeTable();
  inline GDALRasterAttributeTable *get() {
    return this_;
  }

  void dispose();
  long uid;
  long parent_uid;

  inline bool isAlive() {
    return this_ && object_store().isAlive(uid) && (parent_uid == 0 || object_store().isAlive(parent_uid));
  }

    private:
  Handle<GDALRasterAttributeTable> this_;
};

} // namespace node_gdal
#endif
