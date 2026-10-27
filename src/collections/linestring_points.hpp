#ifndef __NODE_GDAL_LINESTRING_POINTS_H__
#define __NODE_GDAL_LINESTRING_POINTS_H__

// node

// nan
#include "../gdal_common.hpp"

// gdal
#include <gdal_priv.h>

// LineString.children

namespace node_gdal {

class LineStringPoints : public GDALObject<LineStringPoints> {
    public:
  static Napi::FunctionReference &constructor();

  static void Initialize(Napi::Object target);
  static Napi::Value New(Napi::Value geom);


  LineStringPoints(const Napi::CallbackInfo &info);

  // Must be accessible: ObjectWrap's finalizer deletes the instance itself
  ~LineStringPoints();
    private:
};

} // namespace node_gdal
#endif
