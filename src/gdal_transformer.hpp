#ifndef __NODE_GDAL_TRANSFORMER_H__
#define __NODE_GDAL_TRANSFORMER_H__

// node

// nan
#include "gdal_common.hpp"

// gdal
#include <gdal_alg.h>
#include <gdal_priv.h>

// ogr
#include <ogrsf_frmts.h>

#include "async.hpp"

namespace node_gdal {

/**
 * A coordinate transformer: the `GDALTransformerFunc` machinery of
 * `gdal_alg.h`, which is what `gdal.warp()` uses internally.
 *
 * A `Transformer` is always created from a source and a target
 * {@link SpatialReference}, or from a {@link Dataset} with a geotransform and
 * its own SRS.
 */
class Transformer : public GDALObject<Transformer> {
    public:
  static Napi::FunctionReference &constructor();
  static void Initialize(Napi::Object target);

  Transformer(const Napi::CallbackInfo &info);

  // Must be accessible: ObjectWrap's finalizer deletes the instance itself
  ~Transformer();

  inline void *get() {
    return this_;
  }

  void dispose();

  long uid;

  inline bool isAlive() {
    return this_ != nullptr;
  }

    private:
  // GDALDestroyTransformer(), which dispatches on the actual transformer type
  void *this_;
};

} // namespace node_gdal
#endif
