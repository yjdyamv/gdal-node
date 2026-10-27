
#include "gdal_spatial_reference.hpp"
#include "utils/args.hpp"
#include "gdal_common.hpp"
#include "utils/string_list.hpp"
#include "async.hpp"

namespace node_gdal {

namespace {
// The wrapper owns the spatial reference it holds, the factory clones the one
// of a dataset or a layer or is handed a fresh one
void free_spatial_reference(void *ptr) {
  // Decrements the reference count by one, and destroys it if zero
  static_cast<OGRSpatialReference *>(ptr)->Release();
}
} // namespace

Napi::FunctionReference &SpatialReference::constructor() {
  return node_gdal::env_context().constructor<SpatialReference>();
}

void SpatialReference::Initialize(Napi::Object target) {
  Napi::Env env = target.Env();
  SELF_CLASS(SpatialReference);

  // NOTE: the descriptor macros carry their own trailing comma
  Napi::Function lcons = GDALDefineClass<SELF>(env, "SpatialReference",
    {
        METHOD(toString)
        METHOD_AS("toWKT", exportToWKT)
        METHOD_AS("toPrettyWKT", exportToPrettyWKT)
        METHOD_AS("toProj4", exportToProj4)
        METHOD_AS("toXML", exportToXML)
        METHOD(clone)
        METHOD(cloneGeogCS)
        METHOD(setWellKnownGeogCS)
        METHOD(morphToESRI)
        METHOD(morphFromESRI)
        METHOD(EPSGTreatsAsLatLong)
        METHOD(EPSGTreatsAsNorthingEasting)
        METHOD(getLinearUnits)
        METHOD(getAngularUnits)
        METHOD(isGeographic)
        METHOD(isGeocentric)
        METHOD(isProjected)
        METHOD(isLocal)
        METHOD_AS("isVectical", isVertical)
        METHOD(isVertical)
        METHOD(isCompound)
        METHOD(isSameGeogCS)
        METHOD(isSameVertCS)
        METHOD(isSame)
        METHOD(getAuthorityName)
        METHOD(getAuthorityCode)
        METHOD(getAttrValue)
        METHOD(autoIdentifyEPSG)
        METHOD(validate)
    });

  GDAL_SetAsyncableMethod(env, lcons, "fromUserInput", fromUserInput);
  GDAL_SetMethod(env, lcons, "fromWKT", fromWKT);
  GDAL_SetMethod(env, lcons, "fromProj4", fromProj4);
  GDAL_SetMethod(env, lcons, "fromEPSG", fromEPSG);
  GDAL_SetMethod(env, lcons, "fromEPSGA", fromEPSGA);
  GDAL_SetMethod(env, lcons, "fromESRI", fromESRI);
  GDAL_SetMethod(env, lcons, "fromWMSAUTO", fromWMSAUTO);
  GDAL_SetMethod(env, lcons, "fromXML", fromXML);
  GDAL_SetMethod(env, lcons, "fromURN", fromURN);
  GDAL_SetAsyncableMethod(env, lcons, "fromCRSURL", fromCRSURL);
  GDAL_SetAsyncableMethod(env, lcons, "fromURL", fromURL);
  GDAL_SetMethod(env, lcons, "fromMICoordSys", fromMICoordSys);

  target.Set("SpatialReference", lcons);

  constructor() = Napi::Persistent(lcons);
}

SpatialReference::~SpatialReference() {
  dispose();
}

void SpatialReference::dispose() {
  if (this_) {
    LOG("Disposing SpatialReference [%p]", this_.get());
    object_store().dispose(uid);
    // The handle releases the spatial reference, if this wrapper owns it
    this_.reset();
    LOG("Disposed SpatialReference");
  }
}

/**
 * This class respresents a OpenGIS Spatial Reference System, and contains
 * methods for converting between this object organization and well known text
 * (WKT) format.
 *
 * @constructor
 * @class SpatialReference
 * @param {string} [wkt]
 */
SpatialReference::SpatialReference(const Napi::CallbackInfo &info)
  : GDALObject<SpatialReference>(info), uid(0) {
  // Internal construction: the factory hands the OGR object over directly
  Handle<OGRSpatialReference> pending = node_gdal::TakePendingHandle<OGRSpatialReference>();
  if (pending) {
    this_ = std::move(pending);
    LOG("Created SpatialReference [%p]", this_);
    return;
  }

  // Constructed from JS: SpatialReference([wkt])
  // (the NODE_ARG_* macros cannot be used here: they return a value on error,
  // which a constructor cannot do)
  std::string wkt("");
  if (info.Length() > 0 && !info[0].IsNull() && !info[0].IsUndefined()) {
    if (!info[0].IsString()) {
      Napi::TypeError::New(info.Env(), "wkt must be a string").ThrowAsJavaScriptException();
      return;
    }
    wkt = info[0].As<Napi::String>().Utf8Value();
  }

  // sets reference count to one
  OGRSpatialReference *srs = new OGRSpatialReference(wkt.empty() ? 0 : wkt.c_str());
  if (!wkt.empty()) {
    OGRChar *wkt_c = (OGRChar *)wkt.c_str();
    int err = srs->importFromWkt(&wkt_c);
    if (err) {
      delete srs;
      // A constructor cannot let a C++ exception escape: node-addon-api's
      // constructor wrapper catches `Napi::Error` only, anything else unwinds
      // through N-API and aborts the process.
      node_gdal::throwError(info.Env(), GDALError(getOGRErrMsg(err)));
      return;
    }
  }

  this_ = Handle<OGRSpatialReference>::owned(srs, &free_spatial_reference);
  uid = object_store().add(srs, *this, 0);
  LOG("Created SpatialReference [%p]", this_);
}

// The spatial reference of a layer, of a geometry or of a multidim object
// cannot be handed out: it belongs to its parent. The factory hands out a
// read-only copy (`clone()` makes a writable one)
Napi::Value SpatialReference::New(const OGRSpatialReference *srs) {
  // This const_cast is not a problem since the very first operation
  // below is copying the SpatialReference
  return SpatialReference::New(const_cast<OGRSpatialReference *>(srs), false);
}

Napi::Value SpatialReference::New(OGRSpatialReference *raw, bool owned) {
  Napi::Env env = node_gdal::napi_env();

  if (!raw) { return env.Null(); }
  if (object_store().has(raw)) { return object_store().get(raw); }

  OGRSpatialReference *cloned_srs = raw;
  if (!owned) cloned_srs = raw->Clone();

  node_gdal::detail::PendingObjectGuard pending(cloned_srs, &free_spatial_reference);
  std::vector<napi_value> args;
  Napi::Object obj = constructor().Value().New(args);
  SpatialReference *wrapped = node_gdal::UnwrapWrapped<SpatialReference>(obj);
  wrapped->uid = object_store().add(raw, *wrapped, 0);
  // A copy is not the spatial reference of the parent it was copied from
  if (!owned) node_gdal::set_read_only(obj);

  return obj;
}

NAPI_METHOD(SpatialReference::toString) {
  return Napi::String::New(node_gdal::napi_env(), "SpatialReference");
}

/**
 * Set a GeogCS based on well known name.
 *
 * @throws {Error}
 * @method setWellKnownGeogCS
 * @instance
 * @memberof SpatialReference
 * @param {string} name
 */
namespace {
// SpatialReference::setWellKnownGeogCS()
struct setWellKnownGeogCSBody : node_gdal::SyncMethodBody {
  static Napi::Value run(const Napi::CallbackInfo &info) {
    Args args(info);
    std::string param;
    if (!args.get(0, "input", param)) { return args.failed(); }
    auto obj = node_gdal::unwrapThis<SpatialReference>(info, "SpatialReference");
    if (!obj) { return obj.failure; }
    if (node_gdal::is_read_only(info.This())) {
      node_gdal::throw_read_only(
        info.Env(), "A SpatialReference retrieved from a layer, a geometry or a multidim object");
      return info.Env().Undefined();
    }
    int err = obj->get()->SetWellKnownGeogCS(param.c_str());
    if (err) { throw GDALError(getOGRErrMsg(err)); }
    return info.Env().Undefined();
  }
};
} // namespace

GDAL_SYNC_METHOD(SpatialReference::setWellKnownGeogCS, setWellKnownGeogCSBody)

/**
 * Convert in place to ESRI WKT format.
 *
 * @throws {Error}
 * @method morphToESRI
 * @instance
 * @memberof SpatialReference
 */
namespace {
// SpatialReference::morphToESRI()
struct morphToESRIBody : node_gdal::SyncMethodBody {
  static Napi::Value run(const Napi::CallbackInfo &info) {
    auto obj = node_gdal::unwrapThis<SpatialReference>(info, "SpatialReference");
    if (!obj) { return obj.failure; }
    if (node_gdal::is_read_only(info.This())) {
      node_gdal::throw_read_only(
        info.Env(), "A SpatialReference retrieved from a layer, a geometry or a multidim object");
      return info.Env().Undefined();
    }
    int err = obj->get()->morphToESRI();
    if (err) { throw GDALError(getOGRErrMsg(err)); }
    return info.Env().Undefined();
  }
};
} // namespace

GDAL_SYNC_METHOD(SpatialReference::morphToESRI, morphToESRIBody)

/**
 * Convert in place from ESRI WKT format.
 *
 * @throws {Error}
 * @method morphFromESRI
 * @instance
 * @memberof SpatialReference
 */
namespace {
// SpatialReference::morphFromESRI()
struct morphFromESRIBody : node_gdal::SyncMethodBody {
  static Napi::Value run(const Napi::CallbackInfo &info) {
    auto obj = node_gdal::unwrapThis<SpatialReference>(info, "SpatialReference");
    if (!obj) { return obj.failure; }
    if (node_gdal::is_read_only(info.This())) {
      node_gdal::throw_read_only(
        info.Env(), "A SpatialReference retrieved from a layer, a geometry or a multidim object");
      return info.Env().Undefined();
    }
    int err = obj->get()->morphFromESRI();
    if (err) { throw GDALError(getOGRErrMsg(err)); }
    return info.Env().Undefined();
  }
};
} // namespace

GDAL_SYNC_METHOD(SpatialReference::morphFromESRI, morphFromESRIBody)

/**
 * This method returns `true` if EPSG feels this geographic coordinate system
 * should be treated as having lat/long coordinate ordering.
 *
 * Currently this returns `true` for all geographic coordinate systems with an
 * EPSG code set, and AXIS values set defining it as lat, long. Note that
 * coordinate systems with an EPSG code and no axis settings will be assumed
 * to not be lat/long.
 *
 * `false` will be returned for all coordinate systems that are not geographic,
 * or that do not have an EPSG code set.
 *
 * @method EPSGTreatsAsLatLong
 * @instance
 * @memberof SpatialReference
 * @return {boolean}
 */
namespace {
// SpatialReference::EPSGTreatsAsLatLong()
struct EPSGTreatsAsLatLongBody : node_gdal::SyncMethodBody {
  static Napi::Value run(const Napi::CallbackInfo &info) {
    auto obj = node_gdal::unwrapThis<SpatialReference>(info, "SpatialReference");
    if (!obj) { return obj.failure; }
    return Boolean::New(info.Env(), obj->get()->EPSGTreatsAsLatLong());
  }
};
} // namespace

GDAL_SYNC_METHOD(SpatialReference::EPSGTreatsAsLatLong, EPSGTreatsAsLatLongBody)

/**
 * This method returns `true` if EPSG feels this projected coordinate system
 * should be treated as having northing/easting coordinate ordering.
 *
 * @method EPSGTreatsAsNorthingEasting
 * @instance
 * @memberof SpatialReference
 * @return {boolean}
 */
namespace {
// SpatialReference::EPSGTreatsAsNorthingEasting()
struct EPSGTreatsAsNorthingEastingBody : node_gdal::SyncMethodBody {
  static Napi::Value run(const Napi::CallbackInfo &info) {
    auto obj = node_gdal::unwrapThis<SpatialReference>(info, "SpatialReference");
    if (!obj) { return obj.failure; }
    return Boolean::New(info.Env(), obj->get()->EPSGTreatsAsNorthingEasting());
  }
};
} // namespace

GDAL_SYNC_METHOD(SpatialReference::EPSGTreatsAsNorthingEasting, EPSGTreatsAsNorthingEastingBody)

/**
 * Check if geocentric coordinate system.
 *
 * @method isGeocentric
 * @instance
 * @memberof SpatialReference
 * @return {boolean}
 */
namespace {
// SpatialReference::isGeocentric()
struct isGeocentricBody : node_gdal::SyncMethodBody {
  static Napi::Value run(const Napi::CallbackInfo &info) {
    auto obj = node_gdal::unwrapThis<SpatialReference>(info, "SpatialReference");
    if (!obj) { return obj.failure; }
    return Boolean::New(info.Env(), obj->get()->IsGeocentric());
  }
};
} // namespace

GDAL_SYNC_METHOD(SpatialReference::isGeocentric, isGeocentricBody)

/**
 * Check if geographic coordinate system.
 *
 * @method isGeographic
 * @instance
 * @memberof SpatialReference
 * @return {boolean}
 */
namespace {
// SpatialReference::isGeographic()
struct isGeographicBody : node_gdal::SyncMethodBody {
  static Napi::Value run(const Napi::CallbackInfo &info) {
    auto obj = node_gdal::unwrapThis<SpatialReference>(info, "SpatialReference");
    if (!obj) { return obj.failure; }
    return Boolean::New(info.Env(), obj->get()->IsGeographic());
  }
};
} // namespace

GDAL_SYNC_METHOD(SpatialReference::isGeographic, isGeographicBody)

/**
 * Check if projected coordinate system.
 *
 * @method isProjected
 * @instance
 * @memberof SpatialReference
 * @return {boolean}
 */
namespace {
// SpatialReference::isProjected()
struct isProjectedBody : node_gdal::SyncMethodBody {
  static Napi::Value run(const Napi::CallbackInfo &info) {
    auto obj = node_gdal::unwrapThis<SpatialReference>(info, "SpatialReference");
    if (!obj) { return obj.failure; }
    return Boolean::New(info.Env(), obj->get()->IsProjected());
  }
};
} // namespace

GDAL_SYNC_METHOD(SpatialReference::isProjected, isProjectedBody)

/**
 * Check if local coordinate system.
 *
 * @method isLocal
 * @instance
 * @memberof SpatialReference
 * @return {boolean}
 */
namespace {
// SpatialReference::isLocal()
struct isLocalBody : node_gdal::SyncMethodBody {
  static Napi::Value run(const Napi::CallbackInfo &info) {
    auto obj = node_gdal::unwrapThis<SpatialReference>(info, "SpatialReference");
    if (!obj) { return obj.failure; }
    return Boolean::New(info.Env(), obj->get()->IsLocal());
  }
};
} // namespace

GDAL_SYNC_METHOD(SpatialReference::isLocal, isLocalBody)

/**
 * Check if vertical coordinate system.
 *
 * @method isVertical
 * @instance
 * @memberof SpatialReference
 * @return {boolean}
 */
namespace {
// SpatialReference::isVertical()
struct isVerticalBody : node_gdal::SyncMethodBody {
  static Napi::Value run(const Napi::CallbackInfo &info) {
    auto obj = node_gdal::unwrapThis<SpatialReference>(info, "SpatialReference");
    if (!obj) { return obj.failure; }
    return Boolean::New(info.Env(), obj->get()->IsVertical());
  }
};
} // namespace

GDAL_SYNC_METHOD(SpatialReference::isVertical, isVerticalBody)
/**
 * Check if vertical coordinate system.
 *
 * Same as `isVertical`; the mis-spelled alias is registered in
 * `SpatialReference::Initialize` next to it and has always been part of the
 * published API, so it keeps its own documentation.
 *
 * @method isVectical
 * @instance
 * @memberof SpatialReference
 * @return {boolean}
 */

/**
 * Check if compound coordinate system.
 *
 * @method isCompound
 * @instance
 * @memberof SpatialReference
 * @return {boolean}
 */
namespace {
// SpatialReference::isCompound()
struct isCompoundBody : node_gdal::SyncMethodBody {
  static Napi::Value run(const Napi::CallbackInfo &info) {
    auto obj = node_gdal::unwrapThis<SpatialReference>(info, "SpatialReference");
    if (!obj) { return obj.failure; }
    return Boolean::New(info.Env(), obj->get()->IsCompound());
  }
};
} // namespace

GDAL_SYNC_METHOD(SpatialReference::isCompound, isCompoundBody)

/**
 * Do the GeogCS'es match?
 *
 * @method isSameGeogCS
 * @instance
 * @memberof SpatialReference
 * @param {SpatialReference} srs
 * @return {boolean}
 */
namespace {
// SpatialReference::isSameGeogCS()
struct isSameGeogCSBody : node_gdal::SyncMethodBody {
  static Napi::Value run(const Napi::CallbackInfo &info) {
    Args args(info);
    SpatialReference *param;
    if (!args.getWrapped(0, "srs", "SpatialReference", param)) { return args.failed(); }
    auto obj = node_gdal::unwrapThis<SpatialReference>(info, "SpatialReference");
    if (!obj) { return obj.failure; }
    return Boolean::New(info.Env(), obj->get()->IsSameGeogCS(param->get()));
  }
};
} // namespace

GDAL_SYNC_METHOD(SpatialReference::isSameGeogCS, isSameGeogCSBody)

/**
 * Do the VertCS'es match?
 *
 * @method isSameVertCS
 * @instance
 * @memberof SpatialReference
 * @param {SpatialReference} srs
 * @return {boolean}
 */
namespace {
// SpatialReference::isSameVertCS()
struct isSameVertCSBody : node_gdal::SyncMethodBody {
  static Napi::Value run(const Napi::CallbackInfo &info) {
    Args args(info);
    SpatialReference *param;
    if (!args.getWrapped(0, "srs", "SpatialReference", param)) { return args.failed(); }
    auto obj = node_gdal::unwrapThis<SpatialReference>(info, "SpatialReference");
    if (!obj) { return obj.failure; }
    return Boolean::New(info.Env(), obj->get()->IsSameVertCS(param->get()));
  }
};
} // namespace

GDAL_SYNC_METHOD(SpatialReference::isSameVertCS, isSameVertCSBody)

/**
 * Do these two spatial references describe the same system?
 *
 * @method isSame
 * @instance
 * @memberof SpatialReference
 * @param {SpatialReference} srs
 * @return {boolean}
 */
namespace {
// SpatialReference::isSame()
struct isSameBody : node_gdal::SyncMethodBody {
  static Napi::Value run(const Napi::CallbackInfo &info) {
    Args args(info);
    SpatialReference *param;
    if (!args.getWrapped(0, "srs", "SpatialReference", param)) { return args.failed(); }
    auto obj = node_gdal::unwrapThis<SpatialReference>(info, "SpatialReference");
    if (!obj) { return obj.failure; }
    return Boolean::New(info.Env(), obj->get()->IsSame(param->get()));
  }
};
} // namespace

GDAL_SYNC_METHOD(SpatialReference::isSame, isSameBody)

/**
 * Set EPSG authority info if possible.
 *
 * @throws {Error}
 * @method autoIdentifyEPSG
 * @instance
 * @memberof SpatialReference
 */
namespace {
// SpatialReference::autoIdentifyEPSG()
struct autoIdentifyEPSGBody : node_gdal::SyncMethodBody {
  static Napi::Value run(const Napi::CallbackInfo &info) {
    auto obj = node_gdal::unwrapThis<SpatialReference>(info, "SpatialReference");
    if (!obj) { return obj.failure; }
    if (node_gdal::is_read_only(info.This())) {
      node_gdal::throw_read_only(
        info.Env(), "A SpatialReference retrieved from a layer, a geometry or a multidim object");
      return info.Env().Undefined();
    }
    int err = obj->get()->AutoIdentifyEPSG();
    if (err) { throw GDALError(getOGRErrMsg(err)); }
    return info.Env().Undefined();
  }
};
} // namespace

GDAL_SYNC_METHOD(SpatialReference::autoIdentifyEPSG, autoIdentifyEPSGBody)

/**
 * Clones the spatial reference.
 *
 * @method clone
 * @instance
 * @memberof SpatialReference
 * @return {SpatialReference}
 */
NAPI_METHOD(SpatialReference::clone) {
  SpatialReference *srs = node_gdal::UnwrapWrapped<SpatialReference>(info.This().As<Napi::Object>());
  return SpatialReference::New(srs->this_->Clone(), true);
}

/**
 * Make a duplicate of the GEOGCS node of this OGRSpatialReference object.
 *
 * @method cloneGeogCS
 * @instance
 * @memberof SpatialReference
 * @return {SpatialReference}
 */
NAPI_METHOD(SpatialReference::cloneGeogCS) {
  SpatialReference *srs = node_gdal::UnwrapWrapped<SpatialReference>(info.This().As<Napi::Object>());
  return SpatialReference::New(srs->this_->CloneGeogCS(), true);
}

/**
 * Get the authority name for a node. The most common authority is "EPSG".
 *
 * @method getAuthorityName
 * @instance
 * @memberof SpatialReference
 * @param {string|null} [target_key] The partial or complete path to the node to get an authority from. ie. `"PROJCS"`, `"GEOGCS"`, "`GEOGCS|UNIT"` or `null` to search for an authority node on the root element.
 * @return {string}
 */
NAPI_METHOD(SpatialReference::getAuthorityName) {
  Args args(info);

  std::string key = "";
  if (!args.optional(0, "target key", key)) return args.failed();

  SpatialReference *srs = node_gdal::UnwrapWrapped<SpatialReference>(info.This().As<Napi::Object>());

  return SafeString::New(srs->this_->GetAuthorityName(key.length() ? key.c_str() : nullptr));
}

/**
 * Get the authority code for a node.
 *
 * @method getAuthorityCode
 * @instance
 * @memberof SpatialReference
 * @param {string|null} [target_key] The partial or complete path to the node to get an authority from. ie. `"PROJCS"`, `"GEOGCS"`, "`GEOGCS|UNIT"` or `null` to search for an authority node on the root element.
 * @return {string}
 */
NAPI_METHOD(SpatialReference::getAuthorityCode) {
  Args args(info);

  std::string key = "";
  if (!args.optional(0, "target key", key)) return args.failed();

  SpatialReference *srs = node_gdal::UnwrapWrapped<SpatialReference>(info.This().As<Napi::Object>());

  return SafeString::New(srs->this_->GetAuthorityCode(key.length() ? key.c_str() : nullptr));
}

/**
 * Convert this SRS into WKT format.
 *
 * @throws {Error}
 * @method toWKT
 * @instance
 * @memberof SpatialReference
 * @return {string}
 */
NAPI_METHOD(SpatialReference::exportToWKT) {

  SpatialReference *srs = node_gdal::UnwrapWrapped<SpatialReference>(info.This().As<Napi::Object>());
  char *str;
  Napi::Value result;

  int err = srs->this_->exportToWkt(&str);
  if (err) {
    throw GDALError(getOGRErrMsg(err));
    return node_gdal::napi_env().Undefined();
  }
  result = SafeString::New(str);
  CPLFree(str);

  return result;
}

/**
 * Convert this SRS into a a nicely formatted WKT string for display to a
 * person.
 *
 * @throws {Error}
 * @method toPrettyWKT
 * @instance
 * @memberof SpatialReference
 * @param {boolean} [simplify=false]
 * @return {string}
 */
NAPI_METHOD(SpatialReference::exportToPrettyWKT) {
  Args args(info);

  int simplify = 0;
  if (!args.optional(0, "simplify", simplify)) return args.failed();

  SpatialReference *srs = node_gdal::UnwrapWrapped<SpatialReference>(info.This().As<Napi::Object>());
  char *str;
  Napi::Value result;

  int err = srs->this_->exportToPrettyWkt(&str, simplify);
  if (err) {
    throw GDALError(getOGRErrMsg(err));
    return node_gdal::napi_env().Undefined();
  }
  result = SafeString::New(str);
  CPLFree(str);

  return result;
}

/**
 * Export coordinate system in PROJ.4 format.
 *
 * @throws {Error}
 * @method toProj4
 * @instance
 * @memberof SpatialReference
 * @return {string}
 */
NAPI_METHOD(SpatialReference::exportToProj4) {

  SpatialReference *srs = node_gdal::UnwrapWrapped<SpatialReference>(info.This().As<Napi::Object>());
  char *str;
  Napi::Value result;

  int err = srs->this_->exportToProj4(&str);
  if (err) {
    throw GDALError(getOGRErrMsg(err));
    return node_gdal::napi_env().Undefined();
  }

  if (str) {
    result = Napi::String::New(node_gdal::napi_env(), CPLString(str).Trim().c_str());
  } else {
    result = node_gdal::napi_env().Null();
  }
  CPLFree(str);

  return result;
}

/**
 * Export coordinate system in XML format.
 *
 * @throws {Error}
 * @method toXML
 * @instance
 * @memberof SpatialReference
 * @return {string}
 */
NAPI_METHOD(SpatialReference::exportToXML) {

  SpatialReference *srs = node_gdal::UnwrapWrapped<SpatialReference>(info.This().As<Napi::Object>());
  char *str;
  Napi::Value result;

  int err = srs->this_->exportToXML(&str);
  if (err) {
    throw GDALError(getOGRErrMsg(err));
    return node_gdal::napi_env().Undefined();
  }
  result = SafeString::New(str);
  CPLFree(str);

  return result;
}

/**
 * Fetch indicated attribute of named node.
 *
 * @method getAttrValue
 * @instance
 * @memberof SpatialReference
 * @param {string} node_name
 * @param {number} [attr_index=0]
 * @return {string}
 */
NAPI_METHOD(SpatialReference::getAttrValue) {
  Args args(info);

  SpatialReference *srs = node_gdal::UnwrapWrapped<SpatialReference>(info.This().As<Napi::Object>());
  std::string node_name("");
  int child = 0;
  if (!args.get(0, "node name", node_name)) return args.failed();
  if (!args.optional(1, "child", child)) return args.failed();
  return SafeString::New(srs->this_->GetAttrValue(node_name.c_str(), child));
}

/**
 * Creates a spatial reference from a WKT string.
 *
 * @static
 * @throws {Error}
 * @method fromWKT
 * @instance
 * @memberof SpatialReference
 * @param {string} wkt
 * @return {SpatialReference}
 */
NAPI_METHOD(SpatialReference::fromWKT) {
  Args args(info);

  std::string wkt("");
  if (!args.get(0, "wkt", wkt)) return args.failed();
  OGRChar *str = (OGRChar *)wkt.c_str();

  OGRSpatialReference *srs = new OGRSpatialReference();
  int err = srs->importFromWkt(&str);
  if (err) {
    delete srs;
    throw GDALError(getOGRErrMsg(err));
    return node_gdal::napi_env().Undefined();
  }

  return SpatialReference::New(srs, true);
}

/**
 * Creates a spatial reference from a Proj.4 string.
 *
 * @static
 * @throws {Error}
 * @method fromProj4
 * @instance
 * @memberof SpatialReference
 * @param {string} input
 * @return {SpatialReference}
 */
NAPI_METHOD(SpatialReference::fromProj4) {
  Args args(info);

  std::string input("");
  if (!args.get(0, "input", input)) return args.failed();

  OGRSpatialReference *srs = new OGRSpatialReference();
  int err = srs->importFromProj4(input.c_str());
  if (err) {
    delete srs;
    throw GDALError(getOGRErrMsg(err));
    return node_gdal::napi_env().Undefined();
  }

  return SpatialReference::New(srs, true);
}

/**
 * Creates a spatial reference from a WMSAUTO string.
 *
 * Note that the WMS 1.3 specification does not include the units code, while
 * apparently earlier specs do. GDAL tries to guess around this.
 *
 * @example
 *
 * var wms = 'AUTO:42001,99,8888';
 * var ref = gdal.SpatialReference.fromWMSAUTO(wms);
 *
 * @static
 * @throws {Error}
 * @method fromWMSAUTO
 * @instance
 * @memberof SpatialReference
 * @param {string} input
 * @return {SpatialReference}
 */
NAPI_METHOD(SpatialReference::fromWMSAUTO) {
  Args args(info);

  std::string input("");
  if (!args.get(0, "input", input)) return args.failed();

  OGRSpatialReference *srs = new OGRSpatialReference();
  int err = srs->importFromWMSAUTO(input.c_str());
  if (err) {
    delete srs;
    throw GDALError(getOGRErrMsg(err));
    return node_gdal::napi_env().Undefined();
  }

  return SpatialReference::New(srs, true);
}

/**
 * Import coordinate system from XML format (GML only currently).
 *
 * @static
 * @throws {Error}
 * @method fromXML
 * @instance
 * @memberof SpatialReference
 * @param {string} input
 * @return {SpatialReference}
 */
NAPI_METHOD(SpatialReference::fromXML) {
  Args args(info);

  std::string input("");
  if (!args.get(0, "xml", input)) return args.failed();

  OGRSpatialReference *srs = new OGRSpatialReference();
  int err = srs->importFromXML(input.c_str());
  if (err) {
    delete srs;
    throw GDALError(getOGRErrMsg(err));
    return node_gdal::napi_env().Undefined();
  }

  return SpatialReference::New(srs, true);
}

/**
 * Initialize from OGC URN.
 *
 * The OGC URN should be prefixed with "urn:ogc:def:crs:" per recommendation
 * paper 06-023r1. Currently EPSG and OGC authority values are supported,
 * including OGC auto codes, but not including CRS1 or CRS88 (NAVD88).
 *
 * @static
 * @throws {Error}
 * @method fromURN
 * @instance
 * @memberof SpatialReference
 * @param {string} input
 * @return {SpatialReference}
 */
NAPI_METHOD(SpatialReference::fromURN) {
  Args args(info);

  std::string input("");
  if (!args.get(0, "input", input)) return args.failed();

  OGRSpatialReference *srs = new OGRSpatialReference();
  int err = srs->importFromURN(input.c_str());
  if (err) {
    delete srs;
    throw GDALError(getOGRErrMsg(err));
    return node_gdal::napi_env().Undefined();
  }

  return SpatialReference::New(srs, true);
}

/**
 * Initialize from OGC URL.
 *
 * The OGC URL should be prefixed with "http://opengis.net/def/crs" per best
 * practice paper 11-135. Currently EPSG and OGC authority values are supported,
 * including OGC auto codes, but not including CRS1 or CRS88 (NAVD88).
 *
 * @static
 * @throws {Error}
 * @method fromCRSURL
 * @instance
 * @memberof SpatialReference
 * @param {string} input
 * @return {SpatialReference}
 */

/**
 * Initialize from OGC URL.
 * @async
 *
 * The OGC URL should be prefixed with "http://opengis.net/def/crs" per best
 * practice paper 11-135. Currently EPSG and OGC authority values are supported,
 * including OGC auto codes, but not including CRS1 or CRS88 (NAVD88).
 *
 * @static
 * @throws {Error}
 * @method fromCRSURLAsync
 * @instance
 * @memberof SpatialReference
 * @param {string} input
 * @param {callback<SpatialReference>} [callback=undefined]
 * @return {Promise<SpatialReference>}
 */
namespace {

struct fromCRSURLBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    Args args(info);

    std::string input("");
    if (!args.get(0, "url", input)) return args.failed();

    GDALAsyncableJob<OGRSpatialReference *> job(0);

    job.main = [input](const GDALExecutionProgress &progress) {
      OGRSpatialReference *srs = new OGRSpatialReference();
      int err = srs->importFromCRSURL(input.c_str());
      if (err) {
        delete srs;
        throw GDALError(getOGRErrMsg(err));
      }
      return srs;
    };
    job.rval = [](OGRSpatialReference *srs, const GetFromPersistentFunc &) { return SpatialReference::New(srs, true); };
    return job.run(info, node_gdal::is_async<Async>, 1);
  }
};

} // namespace

GDAL_ASYNC_METHOD(SpatialReference::fromCRSURL, fromCRSURLBody)

/**
 * Initialize spatial reference from a URL.
 *
 * This method will download the spatial reference from the given URL.
 *
 * @static
 * @throws {Error}
 * @method fromURL
 * @instance
 * @memberof SpatialReference
 * @param {string} url
 * @return {SpatialReference}
 */

/**
 * Initialize spatial reference from a URL.
 * {{async}}
 *
 * This method will download the spatial reference from the given URL.
 *
 * @static
 * @throws {Error}
 * @method fromURLAsync
 * @instance
 * @memberof SpatialReference
 * @param {string} url
 * @param {callback<SpatialReference>} [callback=undefined]
 * @return {Promise<SpatialReference>}
 */

namespace {

struct fromURLBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    Args args(info);

    std::string input("");
    if (!args.get(0, "url", input)) return args.failed();

    GDALAsyncableJob<OGRSpatialReference *> job(0);

    job.main = [input](const GDALExecutionProgress &progress) {
      OGRSpatialReference *srs = new OGRSpatialReference();
      OGRErr err = srs->importFromUrl(input.c_str());
      if (err) {
        delete srs;
        throw GDALError(getOGRErrMsg(err));
      }
      return srs;
    };
    job.rval = [](OGRSpatialReference *srs, const GetFromPersistentFunc &) { return SpatialReference::New(srs, true); };
    return job.run(info, node_gdal::is_async<Async>, 1);
  }
};

} // namespace

GDAL_ASYNC_METHOD(SpatialReference::fromURL, fromURLBody)

/**
 * Initialize from a Mapinfo style CoordSys definition.
 *
 * @static
 * @throws {Error}
 * @method fromMICoordSys
 * @instance
 * @memberof SpatialReference
 * @param {string} input
 * @return {SpatialReference}
 */
NAPI_METHOD(SpatialReference::fromMICoordSys) {
  Args args(info);

  std::string input("");
  if (!args.get(0, "input", input)) return args.failed();

  OGRSpatialReference *srs = new OGRSpatialReference();
  int err = srs->importFromMICoordSys(input.c_str());
  if (err) {
    delete srs;
    throw GDALError(getOGRErrMsg(err));
    return node_gdal::napi_env().Undefined();
  }

  return SpatialReference::New(srs, true);
}

/**
 * Initialize from an arbitrary spatial reference string.
 *
 * This method will examine the provided input, and try to deduce the format,
 * and then use it to initialize the spatial reference system.
 *
 * @static
 * @throws {Error}
 * @method fromUserInput
 * @instance
 * @memberof SpatialReference
 * @param {string} input
 * @return {SpatialReference}
 */

/**
 * Initialize from an arbitrary spatial reference string.
 *
 * This method will examine the provided input, and try to deduce the format,
 * and then use it to initialize the spatial reference system.
 * @async
 *
 * @static
 * @throws {Error}
 * @method fromUserInputAsync
 * @instance
 * @memberof SpatialReference
 * @param {string} input
 * @param {callback<SpatialReference>} [callback=undefined]
 * @return {Promise<SpatialReference>}
 */
namespace {

struct fromUserInputBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    Args args(info);

    std::string input("");
    if (!args.get(0, "url", input)) return args.failed();

    GDALAsyncableJob<OGRSpatialReference *> job(0);

    job.main = [input](const GDALExecutionProgress &progress) {
      OGRSpatialReference *srs = new OGRSpatialReference();
      int err = srs->SetFromUserInput(input.c_str());
      if (err) {
        delete srs;
        throw GDALError(getOGRErrMsg(err));
      }
      return srs;
    };
    job.rval = [](OGRSpatialReference *srs, const GetFromPersistentFunc &) { return SpatialReference::New(srs, true); };
    return job.run(info, node_gdal::is_async<Async>, 1);
  }
};

} // namespace

GDAL_ASYNC_METHOD(SpatialReference::fromUserInput, fromUserInputBody)

/**
 * Initialize from EPSG GCS or PCS code.
 *
 * @example
 *
 * var ref = gdal.SpatialReference.fromEPSGA(4326);
 *
 * @static
 * @throws {Error}
 * @method fromEPSG
 * @instance
 * @memberof SpatialReference
 * @param {number} input
 * @return {SpatialReference}
 */
NAPI_METHOD(SpatialReference::fromEPSG) {
  Args args(info);

  int epsg;
  if (!args.get(0, "epsg", epsg)) return args.failed();

  OGRSpatialReference *srs = new OGRSpatialReference();
  int err = srs->importFromEPSG(epsg);
  if (err) {
    delete srs;
    throw GDALError(getOGRErrMsg(err));
    return node_gdal::napi_env().Undefined();
  }

  return SpatialReference::New(srs, true);
}

/**
 * Initialize from EPSG GCS or PCS code.
 *
 * This method is similar to `fromEPSG()` except that EPSG preferred axis
 * ordering *will* be applied for geographic and projected coordinate systems.
 * EPSG normally defines geographic coordinate systems to use lat/long, and also
 * there are also a few projected coordinate systems that use northing/easting
 * order contrary to typical GIS use).
 *
 * @example
 *
 * var ref = gdal.SpatialReference.fromEPSGA(26910);
 *
 * @static
 * @throws {Error}
 * @method fromEPSGA
 * @instance
 * @memberof SpatialReference
 * @param {number} input
 * @return {SpatialReference}
 */
NAPI_METHOD(SpatialReference::fromEPSGA) {
  Args args(info);

  int epsg;
  if (!args.get(0, "epsg", epsg)) return args.failed();

  OGRSpatialReference *srs = new OGRSpatialReference();
  int err = srs->importFromEPSGA(epsg);
  if (err) {
    delete srs;
    throw GDALError(getOGRErrMsg(err));
    return node_gdal::napi_env().Undefined();
  }

  return SpatialReference::New(srs, true);
}

/**
 * Import coordinate system from ESRI .prj format(s).
 *
 * This function will read the text loaded from an ESRI .prj file, and translate
 * it into an OGRSpatialReference definition. This should support many (but by
 * no means all) old style (Arc/Info 7.x) .prj files, as well as the newer
 * pseudo-OGC WKT .prj files. Note that new style .prj files are in OGC WKT
 * format, but require some manipulation to correct datum names, and units on
 * some projection parameters. This is addressed within importFromESRI() by an
 * automatical call to morphFromESRI().
 *
 * Currently only GEOGRAPHIC, UTM, STATEPLANE, GREATBRITIAN_GRID, ALBERS,
 * EQUIDISTANT_CONIC, TRANSVERSE (mercator), POLAR, MERCATOR and POLYCONIC
 * projections are supported from old style files.
 *
 * @static
 * @throws {Error}
 * @method fromESRI
 * @instance
 * @memberof SpatialReference
 * @param {object|string[]} input
 * @return {SpatialReference}
 */
NAPI_METHOD(SpatialReference::fromESRI) {

  StringList list;

  if (info.Length() < 1) {
    Napi::Error::New(node_gdal::napi_env(), "input string list must be provided").ThrowAsJavaScriptException();
    return node_gdal::napi_env().Undefined();
  }

  if (list.parse(info[0])) {
    return node_gdal::napi_env().Undefined(); // error parsing string list
  }

  OGRSpatialReference *srs = new OGRSpatialReference();
  int err = srs->importFromESRI(list.get());
  if (err) {
    delete srs;
    throw GDALError(getOGRErrMsg(err));
    return node_gdal::napi_env().Undefined();
  }

  return SpatialReference::New(srs, true);
}

/**
 * @typedef {object} units
 * @property {string} units
 * @property {number} value
 */

/**
 * Fetch linear geographic coordinate system units.
 *
 * @method getLinearUnits
 * @instance
 * @memberof SpatialReference
 * @return {units} An object containing `value` and `unit` properties.
 */
NAPI_METHOD(SpatialReference::getLinearUnits) {

  SpatialReference *srs = node_gdal::UnwrapWrapped<SpatialReference>(info.This().As<Napi::Object>());

  OGRChar *unit_name;
  double units = srs->this_->GetLinearUnits(&unit_name);

  Napi::Object result = Napi::Object::New(node_gdal::napi_env());
  result.Set( Napi::String::New(node_gdal::napi_env(), "value"), Napi::Number::New(node_gdal::napi_env(), units));
  result.Set( Napi::String::New(node_gdal::napi_env(), "units"), SafeString::New(unit_name));

  return result;
}

/**
 * Fetch angular geographic coordinate system units.
 *
 * @method getAngularUnits
 * @instance
 * @memberof SpatialReference
 * @return {units} An object containing `value` and `unit` properties.
 */
NAPI_METHOD(SpatialReference::getAngularUnits) {

  SpatialReference *srs = node_gdal::UnwrapWrapped<SpatialReference>(info.This().As<Napi::Object>());

  OGRChar *unit_name;
  double units = srs->this_->GetAngularUnits(&unit_name);

  Napi::Object result = Napi::Object::New(node_gdal::napi_env());
  result.Set( Napi::String::New(node_gdal::napi_env(), "value"), Napi::Number::New(node_gdal::napi_env(), units));
  result.Set( Napi::String::New(node_gdal::napi_env(), "units"), SafeString::New(unit_name));

  return result;
}

/**
 * Validate SRS tokens.
 *
 * This method attempts to verify that the spatial reference system is well
 * formed, and consists of known tokens. The validation is not comprehensive.
 *
 * @method validate
 * @instance
 * @memberof SpatialReference
 * @return {string|null} `"corrupt"`, '"unsupported"', `null` (if fine)
 */
NAPI_METHOD(SpatialReference::validate) {

  SpatialReference *srs = node_gdal::UnwrapWrapped<SpatialReference>(info.This().As<Napi::Object>());

  OGRErr err = srs->this_->Validate();

  if (err == OGRERR_NONE) {
    return node_gdal::napi_env().Null();
    return node_gdal::napi_env().Undefined();
  }
  if (err == OGRERR_CORRUPT_DATA) {
    return Napi::String::New(node_gdal::napi_env(), "corrupt");
    return node_gdal::napi_env().Undefined();
  }
  if (err == OGRERR_UNSUPPORTED_SRS) {
    return Napi::String::New(node_gdal::napi_env(), "unsupported");
    return node_gdal::napi_env().Undefined();
  }

  throw GDALError(getOGRErrMsg(err));
  return node_gdal::napi_env().Undefined();
}

} // namespace node_gdal
