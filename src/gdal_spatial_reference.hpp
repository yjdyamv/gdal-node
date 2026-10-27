#ifndef __NODE_OGR_SPATIALREFERENCE_H__
#define __NODE_OGR_SPATIALREFERENCE_H__

// node

// nan
#include "gdal_common.hpp"

// ogr
#include <ogrsf_frmts.h>

#include "async.hpp"

namespace node_gdal {

#if GDAL_VERSION_MAJOR == 2 && GDAL_VERSION_MINOR < 3
typedef char OGRChar;
#else
typedef const char OGRChar;
#endif

class SpatialReference : public GDALObject<SpatialReference> {
    public:
  static Napi::FunctionReference &constructor();
  static void Initialize(Napi::Object target);
  static Napi::Value New(const OGRSpatialReference *srs);
  static Napi::Value New(OGRSpatialReference *srs, bool owned);
  static NAPI_METHOD(setWellKnownGeogCS);
  static NAPI_METHOD(morphToESRI);
  static NAPI_METHOD(morphFromESRI);
  static NAPI_METHOD(EPSGTreatsAsLatLong);
  static NAPI_METHOD(EPSGTreatsAsNorthingEasting);
  static NAPI_METHOD(isGeocentric);
  static NAPI_METHOD(isGeographic);
  static NAPI_METHOD(isProjected);
  static NAPI_METHOD(isLocal);
  static NAPI_METHOD(isVertical);
  static NAPI_METHOD(isCompound);
  static NAPI_METHOD(isSameGeogCS);
  static NAPI_METHOD(isSameVertCS);
  static NAPI_METHOD(isSame);
  static NAPI_METHOD(autoIdentifyEPSG);

  GDAL_ASYNCABLE_DECLARE(fromUserInput);
  static NAPI_METHOD(fromWKT);
  static NAPI_METHOD(fromProj4);
  static NAPI_METHOD(fromEPSG);
  static NAPI_METHOD(fromEPSGA);
  static NAPI_METHOD(fromESRI);
  static NAPI_METHOD(fromWMSAUTO);
  static NAPI_METHOD(fromXML);
  static NAPI_METHOD(fromURN);
  GDAL_ASYNCABLE_DECLARE(fromCRSURL);
  GDAL_ASYNCABLE_DECLARE(fromURL);
  static NAPI_METHOD(fromMICoordSys);

  SpatialReference(const Napi::CallbackInfo &info);

  // Must be accessible: ObjectWrap's finalizer deletes the instance itself
  ~SpatialReference();
  inline OGRSpatialReference *get() {
    return this_;
  }
  inline bool isAlive() {
    return this_;
  }
  void dispose();
  long uid;

    private:
  Handle<OGRSpatialReference> this_;
};

} // namespace node_gdal
#endif
