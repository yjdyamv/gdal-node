#include "../gdal_common.hpp"
#include "../utils/args.hpp"

#include "../gdal_coordinate_transformation.hpp"
#include "gdal_geometry.hpp"
#include "gdal_geometrycollection.hpp"
#include "gdal_linearring.hpp"
#include "gdal_linestring.hpp"
#include "gdal_circularstring.hpp"
#include "gdal_compoundcurve.hpp"
#include "gdal_multilinestring.hpp"
#include "gdal_multicurve.hpp"
#include "gdal_multipoint.hpp"
#include "gdal_multipolygon.hpp"
#include "gdal_point.hpp"
#include "gdal_polygon.hpp"
#include "../gdal_spatial_reference.hpp"

#include <ogr_core.h>
#include <memory>
#include <sstream>
#include <stdlib.h>

namespace node_gdal {

Napi::FunctionReference &Geometry::constructor() {
  return node_gdal::env_context().constructor<Geometry>();
}

void Geometry::Initialize(Napi::Object target) {
  Napi::Env env = target.Env();
  SELF_CLASS(Geometry);

  // NOTE: the descriptor macros carry their own trailing comma
  Napi::Function lcons = GDALDefineClass<SELF>(env, "Geometry",
    {
        METHOD(toString)
        METHOD_ASYNCABLE_AS("toKML", exportToKML)
        METHOD_ASYNCABLE_AS("toGML", exportToGML)
        METHOD_ASYNCABLE_AS("toJSON", exportToJSON)
        METHOD_ASYNCABLE_AS("toWKT", exportToWKT)
        METHOD_ASYNCABLE_AS("toWKB", exportToWKB)
        METHOD_ASYNCABLE(isEmpty)
        METHOD_ASYNCABLE(isValid)
        METHOD_ASYNCABLE(isSimple)
        METHOD_ASYNCABLE(isRing)
        METHOD(clone)
        METHOD_ASYNCABLE(empty)
        METHOD_ASYNCABLE(closeRings)
        METHOD_ASYNCABLE(intersects)
        METHOD_ASYNCABLE(equals)
        METHOD_ASYNCABLE(disjoint)
        METHOD_ASYNCABLE(touches)
        METHOD_ASYNCABLE(crosses)
        METHOD_ASYNCABLE(within)
        METHOD_ASYNCABLE(contains)
        METHOD_ASYNCABLE(overlaps)
        METHOD_ASYNCABLE(boundary)
        METHOD_ASYNCABLE(distance)
        METHOD_ASYNCABLE(convexHull)
        METHOD_ASYNCABLE(buffer)
        METHOD_ASYNCABLE(intersection)
        METHOD_ASYNCABLE_AS("union", unionGeometry)
        METHOD_ASYNCABLE(difference)
        METHOD_ASYNCABLE(symDifference)
        METHOD_ASYNCABLE(centroid)
        METHOD_ASYNCABLE(simplify)
        METHOD_ASYNCABLE(simplifyPreserveTopology)
        METHOD(segmentize)
        METHOD_ASYNCABLE(swapXY)
        METHOD_ASYNCABLE(getEnvelope)
        METHOD_ASYNCABLE(getEnvelope3D)
        METHOD_ASYNCABLE(flattenTo2D)
        METHOD_ASYNCABLE(transform)
        METHOD_ASYNCABLE(transformTo)
#if GDAL_VERSION_MAJOR >= 3
        METHOD_ASYNCABLE(makeValid)
#endif
        ATTR(lcons, "srs", srsGetter, srsSetter)
        ATTR(lcons, "wkbSize", wkbSizeGetter, READ_ONLY_SETTER)
        ATTR(lcons, "dimension", dimensionGetter, READ_ONLY_SETTER)
        ATTR(lcons, "coordinateDimension", coordinateDimensionGetter, coordinateDimensionSetter)
        ATTR(lcons, "wkbType", typeGetter, READ_ONLY_SETTER)
        ATTR(lcons, "name", nameGetter, READ_ONLY_SETTER)
    });

  GDAL_SetAsyncableMethod(env, lcons, "fromWKT", Geometry::createFromWkt);
  GDAL_SetAsyncableMethod(env, lcons, "fromWKB", Geometry::createFromWkb);
  GDAL_SetAsyncableMethod(env, lcons, "fromGeoJson", Geometry::createFromGeoJson);
  GDAL_SetAsyncableMethod(env, lcons, "fromGeoJsonBuffer", Geometry::createFromGeoJsonBuffer);
  GDAL_SetMethod(env, lcons, "getName", Geometry::getName);
  GDAL_SetMethod(env, lcons, "getConstructor", static_cast<Napi::Value (*)(const Napi::CallbackInfo &)>(Geometry::getConstructor));

  target.Set("Geometry", lcons);

  constructor() = Napi::Persistent(lcons);
}

/**
 * Abstract base class for all geometry classes.
 *
 * @class Geometry
 */
Geometry::Geometry(const Napi::CallbackInfo &info) : GeometryBase<Geometry, OGRGeometry>(info) {
  // The factory hands the OGR object over directly; the class is otherwise
  // abstract in JS
  if (from_factory_) return;

  Napi::Error::New(
    info.Env(),
    "Geometry doesnt have a constructor, use Geometry.fromWKT(), Geometry.fromWKB() or type-specific constructor. ie. new ogr.Point()")
    .ThrowAsJavaScriptException();
}

Napi::Value Geometry::New(OGRGeometry *geom, bool owned) {

  if (!geom) { return node_gdal::napi_env().Null(); }

  OGRwkbGeometryType type = getGeometryType_fixed(geom);
  type = wkbFlatten(type);

  switch (type) {
    case wkbPoint: return Point::New(static_cast<OGRPoint *>(geom), owned);
    case wkbLineString: return LineString::New(static_cast<OGRLineString *>(geom), owned);
    case wkbLinearRing: return LinearRing::New(static_cast<OGRLinearRing *>(geom), owned);
    case wkbPolygon: return Polygon::New(static_cast<OGRPolygon *>(geom), owned);
    case wkbGeometryCollection:
      return GeometryCollection::New(static_cast<OGRGeometryCollection *>(geom), owned);
    case wkbMultiPoint: return MultiPoint::New(static_cast<OGRMultiPoint *>(geom), owned);
    case wkbMultiLineString: return MultiLineString::New(static_cast<OGRMultiLineString *>(geom), owned);
    case wkbMultiPolygon: return MultiPolygon::New(static_cast<OGRMultiPolygon *>(geom), owned);
    case wkbCompoundCurve: return CompoundCurve::New(static_cast<OGRCompoundCurve *>(geom), owned);
    case wkbCircularString: return CircularString::New(static_cast<OGRCircularString *>(geom), owned);
    case wkbMultiCurve: return MultiCurve::New(static_cast<OGRMultiCurve *>(geom), owned);
    default: Napi::Error::New(node_gdal::napi_env(), "Tried to create unsupported geometry type").ThrowAsJavaScriptException(); return node_gdal::napi_env().Undefined();
  }
}

OGRwkbGeometryType Geometry::getGeometryType_fixed(OGRGeometry *geom) {
  // For some reason OGRLinearRing::getGeometryType uses OGRLineString's
  // method... meaning OGRLinearRing::getGeometryType returns wkbLineString

  // http://trac.osgeo.org/gdal/ticket/1755

  OGRwkbGeometryType type = geom->getGeometryType();

  if (std::string(geom->getGeometryName()) == "LINEARRING") {
    type = (OGRwkbGeometryType)(wkbLinearRing | (type & wkb25DBit));
  }

  return type;
}

NAPI_METHOD(Geometry::toString) {
  Geometry *geom = node_gdal::UnwrapWrapped<Geometry>(info.This().As<Napi::Object>());
  std::ostringstream ss;
  ss << "Geometry (" << geom->get()->getGeometryName() << ")";
  return Napi::String::New(node_gdal::napi_env(), ss.str().c_str());
}

/**
 * Closes any un-closed rings.
 *
 * @method closeRings
 * @instance
 * @memberof Geometry
 */

/**
 * Closes any un-closed rings.
 * @async
 *
 * @method closeRingsAsync
 * @instance
 * @memberof Geometry
 * @param {callback<void>} [callback=undefined]
 * @return {Promise<void>}
 */

namespace {
// Geometry.closeRings()
struct closeRingsBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    auto obj = node_gdal::unwrapThis<Geometry>(info, "Geometry", async);
    if (!obj) { return obj.failure; }
    auto *gdal_obj = obj->get();
    GDALAsyncableJob<int> job(0);
    job.main = [gdal_obj](const GDALExecutionProgress &) {
      gdal_obj->closeRings();
      return 0;
    };
    job.rval = [](int, const GetFromPersistentFunc &) { return v8_undefined(); };
    return job.run(info, node_gdal::is_async<Async>, 0);
  }
};
} // namespace

GDAL_ASYNC_METHOD(Geometry::closeRings, closeRingsBody)

/**
 * Clears the geometry.
 *
 * @method empty
 * @instance
 * @memberof Geometry
 */

/**
 * Clears the geometry.
 * @async
 *
 * @method emptyAsync
 * @instance
 * @memberof Geometry
 * @param {callback<void>} [callback=undefined]
 * @return {Promise<void>}
 */

namespace {
// Geometry.empty()
struct emptyBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    auto obj = node_gdal::unwrapThis<Geometry>(info, "Geometry", async);
    if (!obj) { return obj.failure; }
    auto *gdal_obj = obj->get();
    GDALAsyncableJob<int> job(0);
    job.main = [gdal_obj](const GDALExecutionProgress &) {
      gdal_obj->empty();
      return 0;
    };
    job.rval = [](int, const GetFromPersistentFunc &) { return v8_undefined(); };
    return job.run(info, node_gdal::is_async<Async>, 0);
  }
};
} // namespace

GDAL_ASYNC_METHOD(Geometry::empty, emptyBody)

/**
 * Swaps x, y coordinates.
 *
 * @method swapXY
 * @instance
 * @memberof Geometry
 */

/**
 * Swaps x, y coordinates.
 * @async
 *
 * @method swapXYAsync
 * @instance
 * @memberof Geometry
 * @param {callback<void>} [callback=undefined]
 * @return {Promise<void>}
 */

namespace {
// Geometry.swapXY()
struct swapXYBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    auto obj = node_gdal::unwrapThis<Geometry>(info, "Geometry", async);
    if (!obj) { return obj.failure; }
    auto *gdal_obj = obj->get();
    GDALAsyncableJob<int> job(0);
    job.main = [gdal_obj](const GDALExecutionProgress &) {
      gdal_obj->swapXY();
      return 0;
    };
    job.rval = [](int, const GetFromPersistentFunc &) { return v8_undefined(); };
    return job.run(info, node_gdal::is_async<Async>, 0);
  }
};
} // namespace

GDAL_ASYNC_METHOD(Geometry::swapXY, swapXYBody)

/**
 * Determines if the geometry is empty.
 *
 * @method isEmpty
 * @instance
 * @memberof Geometry
 * @return {boolean}
 */

/**
 * Determines if the geometry is empty.
 * @async
 *
 * @method isEmptyAsync
 * @instance
 * @memberof Geometry
 * @param {callback<boolean>} [callback=undefined]
 * @return {Promise<boolean>}
 */

namespace {
// Geometry.isEmpty()
struct isEmptyBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    auto obj = node_gdal::unwrapThis<Geometry>(info, "Geometry", async);
    if (!obj) { return obj.failure; }
    auto *gdal_obj = obj->get();
    GDALAsyncableJob<OGRBoolean> job(0);
    job.main = [gdal_obj](const GDALExecutionProgress &) { return gdal_obj->IsEmpty(); };
    job.rval = [](OGRBoolean r, const GetFromPersistentFunc &) { return Boolean::New(node_gdal::napi_env(), r); };
    return job.run(info, node_gdal::is_async<Async>, 0);
  }
};
} // namespace

GDAL_ASYNC_METHOD(Geometry::isEmpty, isEmptyBody)

/**
 * Determines if the geometry is valid.
 *
 * @method isValid
 * @instance
 * @memberof Geometry
 * @return {boolean}
 */

/**
 * Determines if the geometry is valid.
 * @async
 *
 * @method isValidAsync
 * @instance
 * @memberof Geometry
 * @param {callback<boolean>} [callback=undefined]
 * @return {Promise<boolean>}
 */

namespace {
// Geometry.isValid()
struct isValidBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    auto obj = node_gdal::unwrapThis<Geometry>(info, "Geometry", async);
    if (!obj) { return obj.failure; }
    auto *gdal_obj = obj->get();
    GDALAsyncableJob<OGRBoolean> job(0);
    job.main = [gdal_obj](const GDALExecutionProgress &) { return gdal_obj->IsValid(); };
    job.rval = [](OGRBoolean r, const GetFromPersistentFunc &) { return Boolean::New(node_gdal::napi_env(), r); };
    return job.run(info, node_gdal::is_async<Async>, 0);
  }
};
} // namespace

GDAL_ASYNC_METHOD(Geometry::isValid, isValidBody)

/**
 * Determines if the geometry is simple.
 *
 * @method isSimple
 * @instance
 * @memberof Geometry
 * @return {boolean}
 */

/**
 * Determines if the geometry is simple.
 * @async
 *
 * @method isSimpleAsync
 * @instance
 * @memberof Geometry
 * @param {callback<boolean>} [callback=undefined]
 * @return {Promise<boolean>}
 */

namespace {
// Geometry.isSimple()
struct isSimpleBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    auto obj = node_gdal::unwrapThis<Geometry>(info, "Geometry", async);
    if (!obj) { return obj.failure; }
    auto *gdal_obj = obj->get();
    GDALAsyncableJob<OGRBoolean> job(0);
    job.main = [gdal_obj](const GDALExecutionProgress &) { return gdal_obj->IsSimple(); };
    job.rval = [](OGRBoolean r, const GetFromPersistentFunc &) { return Boolean::New(node_gdal::napi_env(), r); };
    return job.run(info, node_gdal::is_async<Async>, 0);
  }
};
} // namespace

GDAL_ASYNC_METHOD(Geometry::isSimple, isSimpleBody)

/**
 * Determines if the geometry is a ring.
 *
 * @method isRing
 * @instance
 * @memberof Geometry
 * @return {boolean}
 */

/**
 * Determines if the geometry is a ring.
 * @async
 *
 * @method isRingAsync
 * @instance
 * @memberof Geometry
 * @param {callback<boolean>} [callback=undefined]
 * @return {Promise<boolean>}
 */

namespace {
// Geometry.isRing()
struct isRingBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    auto obj = node_gdal::unwrapThis<Geometry>(info, "Geometry", async);
    if (!obj) { return obj.failure; }
    auto *gdal_obj = obj->get();
    GDALAsyncableJob<OGRBoolean> job(0);
    job.main = [gdal_obj](const GDALExecutionProgress &) { return gdal_obj->IsRing(); };
    job.rval = [](OGRBoolean r, const GetFromPersistentFunc &) { return Boolean::New(node_gdal::napi_env(), r); };
    return job.run(info, node_gdal::is_async<Async>, 0);
  }
};
} // namespace

GDAL_ASYNC_METHOD(Geometry::isRing, isRingBody)

/**
 * Determines if the two geometries intersect.
 *
 * @method intersects
 * @instance
 * @memberof Geometry
 * @param {Geometry} geometry
 * @return {boolean}
 */

/**
 * Determines if the two geometries intersect.
 * @async
 *
 * @method intersectsAsync
 * @instance
 * @memberof Geometry
 * @param {Geometry} geometry
 * @param {callback<boolean>} [callback=undefined]
 * @return {Promise<boolean>}
 */

namespace {
// Geometry.intersects()
struct intersectsBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    Args args(info);
    Geometry *param;
    if (!args.getWrapped(0, "geometry to compare", "Geometry", param)) { return args.failed(); }
    auto obj = node_gdal::unwrapThis<Geometry>(info, "Geometry", async);
    if (!obj) { return obj.failure; }
    auto *gdal_obj = obj->get();
    auto *gdal_param = param->get();
    GDALAsyncableJob<OGRBoolean> job(0);
    job.persist(info[0].As<Napi::Object>());
    job.main = [gdal_obj, gdal_param](const GDALExecutionProgress &) {
      return gdal_obj->Intersects(gdal_param);
    };
    job.rval = [](OGRBoolean r, const GetFromPersistentFunc &) { return Boolean::New(node_gdal::napi_env(), r); };
    return job.run(info, node_gdal::is_async<Async>, 1);
  }
};
} // namespace

GDAL_ASYNC_METHOD(Geometry::intersects, intersectsBody)

/**
 * Determines if the two geometries equal each other.
 *
 * @method equals
 * @instance
 * @memberof Geometry
 * @param {Geometry} geometry
 * @return {boolean}
 */

/**
 * Determines if the two geometries equal each other.
 * @async
 *
 * @method equalsAsync
 * @instance
 * @memberof Geometry
 * @param {Geometry} geometry
 * @param {callback<boolean>} [callback=undefined]
 * @return {Promise<boolean>}
 */

namespace {
// Geometry.equals()
struct equalsBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    Args args(info);
    Geometry *param;
    if (!args.getWrapped(0, "geometry to compare", "Geometry", param)) { return args.failed(); }
    auto obj = node_gdal::unwrapThis<Geometry>(info, "Geometry", async);
    if (!obj) { return obj.failure; }
    auto *gdal_obj = obj->get();
    auto *gdal_param = param->get();
    GDALAsyncableJob<OGRBoolean> job(0);
    job.persist(info[0].As<Napi::Object>());
    job.main = [gdal_obj, gdal_param](const GDALExecutionProgress &) {
      return gdal_obj->Equals(gdal_param);
    };
    job.rval = [](OGRBoolean r, const GetFromPersistentFunc &) { return Boolean::New(node_gdal::napi_env(), r); };
    return job.run(info, node_gdal::is_async<Async>, 1);
  }
};
} // namespace

GDAL_ASYNC_METHOD(Geometry::equals, equalsBody)

/**
 * Determines if the two geometries are disjoint.
 *
 * @method disjoint
 * @instance
 * @memberof Geometry
 * @param {Geometry} geometry
 * @return {boolean}
 */

/**
 * Determines if the two geometries are disjoint.
 * @async
 *
 * @method disjointAsync
 * @instance
 * @memberof Geometry
 * @param {Geometry} geometry
 * @param {callback<boolean>} [callback=undefined]
 * @return {Promise<boolean>}
 */

namespace {
// Geometry.disjoint()
struct disjointBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    Args args(info);
    Geometry *param;
    if (!args.getWrapped(0, "geometry to compare", "Geometry", param)) { return args.failed(); }
    auto obj = node_gdal::unwrapThis<Geometry>(info, "Geometry", async);
    if (!obj) { return obj.failure; }
    auto *gdal_obj = obj->get();
    auto *gdal_param = param->get();
    GDALAsyncableJob<OGRBoolean> job(0);
    job.persist(info[0].As<Napi::Object>());
    job.main = [gdal_obj, gdal_param](const GDALExecutionProgress &) {
      return gdal_obj->Disjoint(gdal_param);
    };
    job.rval = [](OGRBoolean r, const GetFromPersistentFunc &) { return Boolean::New(node_gdal::napi_env(), r); };
    return job.run(info, node_gdal::is_async<Async>, 1);
  }
};
} // namespace

GDAL_ASYNC_METHOD(Geometry::disjoint, disjointBody)

/**
 * Determines if the two geometries touch.
 *
 * @method touches
 * @instance
 * @memberof Geometry
 * @param {Geometry} geometry
 * @return {boolean}
 */

/**
 * Determines if the two geometries touch.
 * @async
 *
 * @method touchesAsync
 * @instance
 * @memberof Geometry
 * @param {Geometry} geometry
 * @param {callback<boolean>} [callback=undefined]
 * @return {Promise<boolean>}
 */

namespace {
// Geometry.touches()
struct touchesBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    Args args(info);
    Geometry *param;
    if (!args.getWrapped(0, "geometry to compare", "Geometry", param)) { return args.failed(); }
    auto obj = node_gdal::unwrapThis<Geometry>(info, "Geometry", async);
    if (!obj) { return obj.failure; }
    auto *gdal_obj = obj->get();
    auto *gdal_param = param->get();
    GDALAsyncableJob<OGRBoolean> job(0);
    job.persist(info[0].As<Napi::Object>());
    job.main = [gdal_obj, gdal_param](const GDALExecutionProgress &) {
      return gdal_obj->Touches(gdal_param);
    };
    job.rval = [](OGRBoolean r, const GetFromPersistentFunc &) { return Boolean::New(node_gdal::napi_env(), r); };
    return job.run(info, node_gdal::is_async<Async>, 1);
  }
};
} // namespace

GDAL_ASYNC_METHOD(Geometry::touches, touchesBody)

/**
 * Determines if the two geometries cross.
 *
 * @method crosses
 * @instance
 * @memberof Geometry
 * @param {Geometry} geometry
 * @return {boolean}
 */

/**
 * Determines if the two geometries cross.
 * @async
 *
 * @method crossesAsync
 * @instance
 * @memberof Geometry
 * @param {Geometry} geometry
 * @param {callback<boolean>} [callback=undefined]
 * @return {Promise<boolean>}
 */

namespace {
// Geometry.crosses()
struct crossesBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    Args args(info);
    Geometry *param;
    if (!args.getWrapped(0, "geometry to compare", "Geometry", param)) { return args.failed(); }
    auto obj = node_gdal::unwrapThis<Geometry>(info, "Geometry", async);
    if (!obj) { return obj.failure; }
    auto *gdal_obj = obj->get();
    auto *gdal_param = param->get();
    GDALAsyncableJob<OGRBoolean> job(0);
    job.persist(info[0].As<Napi::Object>());
    job.main = [gdal_obj, gdal_param](const GDALExecutionProgress &) {
      return gdal_obj->Crosses(gdal_param);
    };
    job.rval = [](OGRBoolean r, const GetFromPersistentFunc &) { return Boolean::New(node_gdal::napi_env(), r); };
    return job.run(info, node_gdal::is_async<Async>, 1);
  }
};
} // namespace

GDAL_ASYNC_METHOD(Geometry::crosses, crossesBody)

/**
 * Determines if the current geometry is within the provided geometry.
 *
 * @method within
 * @instance
 * @memberof Geometry
 * @param {Geometry} geometry
 * @return {boolean}
 */

/**
 * Determines if the current geometry is within the provided geometry.
 * @async
 *
 * @method withinAsync
 * @instance
 * @memberof Geometry
 * @param {Geometry} geometry
 * @param {callback<boolean>} [callback=undefined]
 * @return {Promise<boolean>}
 */

namespace {
// Geometry.within()
struct withinBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    Args args(info);
    Geometry *param;
    if (!args.getWrapped(0, "geometry to compare", "Geometry", param)) { return args.failed(); }
    auto obj = node_gdal::unwrapThis<Geometry>(info, "Geometry", async);
    if (!obj) { return obj.failure; }
    auto *gdal_obj = obj->get();
    auto *gdal_param = param->get();
    GDALAsyncableJob<OGRBoolean> job(0);
    job.persist(info[0].As<Napi::Object>());
    job.main = [gdal_obj, gdal_param](const GDALExecutionProgress &) {
      return gdal_obj->Within(gdal_param);
    };
    job.rval = [](OGRBoolean r, const GetFromPersistentFunc &) { return Boolean::New(node_gdal::napi_env(), r); };
    return job.run(info, node_gdal::is_async<Async>, 1);
  }
};
} // namespace

GDAL_ASYNC_METHOD(Geometry::within, withinBody)

/**
 * Determines if the current geometry contains the provided geometry.
 *
 * @method contains
 * @instance
 * @memberof Geometry
 * @param {Geometry} geometry
 * @return {boolean}
 */

/**
 * Determines if the current geometry contains the provided geometry.
 * @async
 *
 * @method containsAsync
 * @instance
 * @memberof Geometry
 * @param {Geometry} geometry
 * @param {callback<boolean>} [callback=undefined]
 * @return {Promise<boolean>}
 */

namespace {
// Geometry.contains()
struct containsBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    Args args(info);
    Geometry *param;
    if (!args.getWrapped(0, "geometry to compare", "Geometry", param)) { return args.failed(); }
    auto obj = node_gdal::unwrapThis<Geometry>(info, "Geometry", async);
    if (!obj) { return obj.failure; }
    auto *gdal_obj = obj->get();
    auto *gdal_param = param->get();
    GDALAsyncableJob<OGRBoolean> job(0);
    job.persist(info[0].As<Napi::Object>());
    job.main = [gdal_obj, gdal_param](const GDALExecutionProgress &) {
      return gdal_obj->Contains(gdal_param);
    };
    job.rval = [](OGRBoolean r, const GetFromPersistentFunc &) { return Boolean::New(node_gdal::napi_env(), r); };
    return job.run(info, node_gdal::is_async<Async>, 1);
  }
};
} // namespace

GDAL_ASYNC_METHOD(Geometry::contains, containsBody)

/**
 * Determines if the current geometry overlaps the provided geometry.
 *
 * @method overlaps
 * @instance
 * @memberof Geometry
 * @param {Geometry} geometry
 * @return {boolean}
 */

/**
 * Determines if the current geometry overlaps the provided geometry.
 * @async
 *
 * @method overlapsAsync
 * @instance
 * @memberof Geometry
 * @param {Geometry} geometry
 * @param {callback<boolean>} [callback=undefined]
 * @return {Promise<boolean>}
 */

namespace {
// Geometry.overlaps()
struct overlapsBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    Args args(info);
    Geometry *param;
    if (!args.getWrapped(0, "geometry to compare", "Geometry", param)) { return args.failed(); }
    auto obj = node_gdal::unwrapThis<Geometry>(info, "Geometry", async);
    if (!obj) { return obj.failure; }
    auto *gdal_obj = obj->get();
    auto *gdal_param = param->get();
    GDALAsyncableJob<OGRBoolean> job(0);
    job.persist(info[0].As<Napi::Object>());
    job.main = [gdal_obj, gdal_param](const GDALExecutionProgress &) {
      return gdal_obj->Overlaps(gdal_param);
    };
    job.rval = [](OGRBoolean r, const GetFromPersistentFunc &) { return Boolean::New(node_gdal::napi_env(), r); };
    return job.run(info, node_gdal::is_async<Async>, 1);
  }
};
} // namespace

GDAL_ASYNC_METHOD(Geometry::overlaps, overlapsBody)

/**
 * Computes the distance between the two geometries.
 *
 * @method distance
 * @instance
 * @memberof Geometry
 * @param {Geometry} geometry
 * @return {number}
 */

/**
 * Computes the distance between the two geometries.
 * @async
 *
 * @method distanceAsync
 * @instance
 * @memberof Geometry
 * @param {Geometry} geometry
 * @param {callback<number>} [callback=undefined]
 * @return {Promise<number>}
 */

namespace {
// Geometry.distance()
struct distanceBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    Args args(info);
    Geometry *param;
    if (!args.getWrapped(0, "geometry to use for distance calculation", "Geometry", param)) { return args.failed(); }
    auto obj = node_gdal::unwrapThis<Geometry>(info, "Geometry", async);
    if (!obj) { return obj.failure; }
    auto *gdal_obj = obj->get();
    auto *gdal_param = param->get();
    GDALAsyncableJob<double> job(0);
    job.persist(info[0].As<Napi::Object>());
    job.main = [gdal_obj, gdal_param](const GDALExecutionProgress &) {
      return gdal_obj->Distance(gdal_param);
    };
    job.rval = [](double r, const GetFromPersistentFunc &) { return Number::New(node_gdal::napi_env(), r); };
    return job.run(info, node_gdal::is_async<Async>, 1);
  }
};
} // namespace

GDAL_ASYNC_METHOD(Geometry::distance, distanceBody)

/**
 * Modify the geometry such it has no segment longer then the given distance.
 *
 * @method segmentize
 * @instance
 * @memberof Geometry
 * @param {number} segment_length
 * @return {void}
 */
namespace {
// Geometry::segmentize()
struct segmentizeBody : node_gdal::SyncMethodBody {
  static Napi::Value run(const Napi::CallbackInfo &info) {
    Args args(info);
    double param;
    if (!args.get(0, "segment length", param)) { return args.failed(); }
    auto obj = node_gdal::unwrapThis<Geometry>(info, "Geometry");
    if (!obj) { return obj.failure; }
    obj->get()->segmentize(param);
    return info.Env().Undefined();
  }
};
} // namespace

GDAL_SYNC_METHOD(Geometry::segmentize, segmentizeBody)

/**
 * Apply arbitrary coordinate transformation to the geometry.
 *
 * This method will transform the coordinates of a geometry from their current
 * spatial reference system to a new target spatial reference system. Normally
 * this means reprojecting the vectors, but it could include datum shifts,
 * and changes of units.
 *
 * Note that this method does not require that the geometry already have a
 * spatial reference system. It will be assumed that they can be treated as
 * having the source spatial reference system of the {@link CoordinateTransformation}
 * object, and the actual SRS of the geometry will be ignored.
 *
 * @throws {Error}
 * @method transform
 * @instance
 * @memberof Geometry
 * @param {CoordinateTransformation} transformation
 */

/**
 * Apply arbitrary coordinate transformation to the geometry.
 * @async
 *
 * @throws {Error}
 * @method transformAsync
 * @instance
 * @memberof Geometry
 * @param {CoordinateTransformation} transformation
 * @param {callback<void>} [callback=undefined]
 * @return {Promise<void>}
 */

namespace {
// Geometry.transform()
struct transformBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    Args args(info);
    CoordinateTransformation *param;
    if (!args.getWrapped(0, "transform", "CoordinateTransformation", param)) { return args.failed(); }
    auto obj = node_gdal::unwrapThis<Geometry>(info, "Geometry", async);
    if (!obj) { return obj.failure; }
    auto *gdal_obj = obj->get();
    auto *gdal_param = param->get();
    GDALAsyncableJob<int> job(0);
    job.persist(info[0].As<Napi::Object>());
    job.main = [gdal_obj, gdal_param](const GDALExecutionProgress &) {
      int err = gdal_obj->transform(gdal_param);
      if (err) { throw GDALError(getOGRErrMsg(err)); }
      return err;
    };
    job.rval = [](int, const GetFromPersistentFunc &) { return v8_undefined(); };
    return job.run(info, node_gdal::is_async<Async>, 1);
  }
};
} // namespace

GDAL_ASYNC_METHOD(Geometry::transform, transformBody)

/**
 * Transforms the geometry to match the provided {@link SpatialReference}
 *
 * @throws {Error}
 * @method transformTo
 * @instance
 * @memberof Geometry
 * @param {SpatialReference} srs
 */

/**
 * Transforms the geometry to match the provided {@link SpatialReference}
 * @async
 *
 * @throws {Error}
 * @method transformToAsync
 * @instance
 * @memberof Geometry
 * @param {SpatialReference} srs
 * @param {callback<void>} [callback=undefined]
 * @return {Promise<void>}
 */

namespace {
// Geometry.transformTo()
struct transformToBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    Args args(info);
    SpatialReference *param;
    if (!args.getWrapped(0, "spatial reference", "SpatialReference", param)) { return args.failed(); }
    auto obj = node_gdal::unwrapThis<Geometry>(info, "Geometry", async);
    if (!obj) { return obj.failure; }
    auto *gdal_obj = obj->get();
    auto *gdal_param = param->get();
    GDALAsyncableJob<int> job(0);
    job.persist(info[0].As<Napi::Object>());
    job.main = [gdal_obj, gdal_param](const GDALExecutionProgress &) {
      int err = gdal_obj->transformTo(gdal_param);
      if (err) { throw GDALError(getOGRErrMsg(err)); }
      return err;
    };
    job.rval = [](int, const GetFromPersistentFunc &) { return v8_undefined(); };
    return job.run(info, node_gdal::is_async<Async>, 1);
  }
};
} // namespace

GDAL_ASYNC_METHOD(Geometry::transformTo, transformToBody)

/**
 * Clones the instance.
 *
 * @method clone
 * @instance
 * @memberof Geometry
 * @return {Geometry}
 */
NAPI_METHOD(Geometry::clone) {
  Geometry *geom = node_gdal::UnwrapWrapped<Geometry>(info.This().As<Napi::Object>());
  return Geometry::New(geom->get()->clone());
}

/**
 * Compute convex hull.
 *
 * @method convexHull
 * @instance
 * @memberof Geometry
 * @throws {Error}
 * @return {Geometry}
 */

/**
 * Compute convex hull.
 * @async
 *
 * @method convexHullAsync
 * @instance
 * @memberof Geometry
 * @throws {Error}
 * @param {callback<Geometry>} [callback=undefined]
 * @return {Promise<Geometry>}
 */

namespace {

struct convexHullBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    Geometry *geom = node_gdal::UnwrapWrapped<Geometry>(info.This().As<Napi::Object>());
    OGRGeometry *gdal_geom = geom->get();
    GDALAsyncableJob<OGRGeometry *> job(0);
    job.main = [gdal_geom](const GDALExecutionProgress &) {
      CPLErrorReset();
      auto r = gdal_geom->ConvexHull();
      if (r == nullptr) throw GDALError::fromGDAL();
      return r;
    };
    job.rval = [](OGRGeometry *r, const GetFromPersistentFunc &) { return Geometry::New(r); };
    return job.run(info, node_gdal::is_async<Async>, 0);
  }
};

} // namespace

GDAL_ASYNC_METHOD(Geometry::convexHull, convexHullBody)

/**
 * Compute boundary.
 *
 * @method boundary
 * @instance
 * @memberof Geometry
 * @throws {Error}
 * @return {Geometry}
 */

/**
 * Compute boundary.
 * @async
 *
 * @method boundaryAsync
 * @instance
 * @memberof Geometry
 * @throws {Error}
 * @param {callback<Geometry>} [callback=undefined]
 * @return {Promise<Geometry>}
 */

namespace {

struct boundaryBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    Geometry *geom = node_gdal::UnwrapWrapped<Geometry>(info.This().As<Napi::Object>());
    OGRGeometry *gdal_geom = geom->get();
    GDALAsyncableJob<OGRGeometry *> job(0);
    job.main = [gdal_geom](const GDALExecutionProgress &) {
      CPLErrorReset();
      auto r = gdal_geom->Boundary();
      if (r == nullptr) throw GDALError::fromGDAL();
      return r;
    };
    job.rval = [](OGRGeometry *r, const GetFromPersistentFunc &) { return Geometry::New(r); };
    return job.run(info, node_gdal::is_async<Async>, 0);
  }
};

} // namespace

GDAL_ASYNC_METHOD(Geometry::boundary, boundaryBody)

/**
 * Compute intersection with another geometry.
 *
 * @method intersection
 * @instance
 * @memberof Geometry
 * @param {Geometry} geometry
 * @throws {Error}
 * @return {Geometry}
 */

/**
 * Compute intersection with another geometry.
 * @async
 *
 * @method intersectionAsync
 * @instance
 * @memberof Geometry
 * @param {Geometry} geometry
 * @param {callback<Geometry>} [callback=undefined]
 * @throws {Error}
 * @return {Promise<Geometry>}
 */

namespace {

struct intersectionBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    Args args(info);

    Geometry *geom = node_gdal::UnwrapWrapped<Geometry>(info.This().As<Napi::Object>());
    Geometry *x = NULL;

    if (!args.getWrapped(0, "geometry to use for intersection", "Geometry", x)) return args.failed();

    OGRGeometry *gdal_geom = geom->get();
    OGRGeometry *gdal_x = x->get();
    GDALAsyncableJob<OGRGeometry *> job(0);
    job.main = [gdal_geom, gdal_x](const GDALExecutionProgress &) {
      CPLErrorReset();
      auto r = gdal_geom->Intersection(gdal_x);
      if (r == nullptr) throw GDALError::fromGDAL();
      return r;
    };
    job.rval = [](OGRGeometry *r, const GetFromPersistentFunc &) { return Geometry::New(r); };
    return job.run(info, node_gdal::is_async<Async>, 1);
  }
};

} // namespace

GDAL_ASYNC_METHOD(Geometry::intersection, intersectionBody)

/**
 * Compute the union of this geometry with another.
 *
 * @method union
 * @instance
 * @memberof Geometry
 * @param {Geometry} geometry
 * @throws {Error}
 * @return {Geometry}
 */

/**
 * Compute the union of this geometry with another.
 * @async
 *
 * @method unionAsync
 * @instance
 * @memberof Geometry
 * @param {Geometry} geometry
 * @param {callback<Geometry>} [callback=undefined]
 * @throws {Error}
 * @return {Promise<Geometry>}
 */

namespace {

struct unionGeometryBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    Args args(info);

    Geometry *geom = node_gdal::UnwrapWrapped<Geometry>(info.This().As<Napi::Object>());
    Geometry *x = NULL;

    if (!args.getWrapped(0, "geometry to use for union", "Geometry", x)) return args.failed();

    OGRGeometry *gdal_geom = geom->get();
    OGRGeometry *gdal_x = x->get();
    GDALAsyncableJob<OGRGeometry *> job(0);
    job.main = [gdal_geom, gdal_x](const GDALExecutionProgress &) {
      CPLErrorReset();
      auto r = gdal_geom->Union(gdal_x);
      if (r == nullptr) throw GDALError::fromGDAL();
      return r;
    };
    job.rval = [](OGRGeometry *r, const GetFromPersistentFunc &) { return Geometry::New(r); };
    return job.run(info, node_gdal::is_async<Async>, 1);
  }
};

} // namespace

GDAL_ASYNC_METHOD(Geometry::unionGeometry, unionGeometryBody)

/**
 * Compute the difference of this geometry with another.
 *
 * @method difference
 * @instance
 * @memberof Geometry
 * @param {Geometry} geometry
 * @throws {Error}
 * @return {Geometry}
 */

/**
 * Compute the difference of this geometry with another.
 * @async
 *
 * @method differenceAsync
 * @instance
 * @memberof Geometry
 * @param {Geometry} geometry
 * @param {callback<Geometry>} [callback=undefined]
 * @throws {Error}
 * @return {Promise<Geometry>}
 */

namespace {

struct differenceBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    Args args(info);

    Geometry *geom = node_gdal::UnwrapWrapped<Geometry>(info.This().As<Napi::Object>());
    Geometry *x = NULL;

    if (!args.getWrapped(0, "geometry to use for difference", "Geometry", x)) return args.failed();

    OGRGeometry *gdal_geom = geom->get();
    OGRGeometry *gdal_x = x->get();
    GDALAsyncableJob<OGRGeometry *> job(0);
    job.main = [gdal_geom, gdal_x](const GDALExecutionProgress &) {
      CPLErrorReset();
      auto r = gdal_geom->Difference(gdal_x);
      if (r == nullptr) throw GDALError::fromGDAL();
      return r;
    };
    job.rval = [](OGRGeometry *r, const GetFromPersistentFunc &) { return Geometry::New(r); };
    return job.run(info, node_gdal::is_async<Async>, 1);
  }
};

} // namespace

GDAL_ASYNC_METHOD(Geometry::difference, differenceBody)

/**
 * Computes the symmetric difference of this geometry and the second geometry.
 *
 * @method symDifference
 * @instance
 * @memberof Geometry
 * @param {Geometry} geometry
 * @throws {Error}
 * @return {Geometry}
 */

/**
 * Computes the symmetric difference of this geometry and the second geometry.
 * @async
 *
 * @method symDifferenceAsync
 * @instance
 * @memberof Geometry
 * @param {Geometry} geometry
 * @param {callback<Geometry>} [callback=undefined]
 * @throws {Error}
 * @return {Promise<Geometry>}
 */

namespace {

struct symDifferenceBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    Args args(info);

    Geometry *geom = node_gdal::UnwrapWrapped<Geometry>(info.This().As<Napi::Object>());
    Geometry *x = NULL;

    if (!args.getWrapped(0, "geometry to use for symDifference", "Geometry", x)) return args.failed();

    OGRGeometry *gdal_geom = geom->get();
    OGRGeometry *gdal_x = x->get();
    GDALAsyncableJob<OGRGeometry *> job(0);
    job.main = [gdal_geom, gdal_x](const GDALExecutionProgress &) {
      CPLErrorReset();
      auto r = gdal_geom->SymDifference(gdal_x);
      if (r == nullptr) throw GDALError::fromGDAL();
      return r;
    };
    job.rval = [](OGRGeometry *r, const GetFromPersistentFunc &) { return Geometry::New(r); };
    return job.run(info, node_gdal::is_async<Async>, 1);
  }
};

} // namespace

GDAL_ASYNC_METHOD(Geometry::symDifference, symDifferenceBody)

/**
 * Reduces the geometry complexity.
 *
 * @method simplify
 * @instance
 * @memberof Geometry
 * @param {number} tolerance
 * @throws {Error}
 * @return {Geometry}
 */

/**
 * Reduces the geometry complexity.
 * @async
 *
 * @method simplifyAsync
 * @instance
 * @memberof Geometry
 * @param {number} tolerance
 * @param {callback<Geometry>} [callback=undefined]
 * @throws {Error}
 * @return {Promise<Geometry>}
 */

namespace {

struct simplifyBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    Args args(info);

    Geometry *geom = node_gdal::UnwrapWrapped<Geometry>(info.This().As<Napi::Object>());
    double tolerance;

    if (!args.get(0, "tolerance", tolerance)) return args.failed();
    OGRGeometry *gdal_geom = geom->get();

    GDALAsyncableJob<OGRGeometry *> job(0);
    job.main = [gdal_geom, tolerance](const GDALExecutionProgress &) {
      CPLErrorReset();
      auto r = gdal_geom->Simplify(tolerance);
      if (r == nullptr) throw GDALError::fromGDAL();
      return r;
    };
    job.rval = [](OGRGeometry *r, const GetFromPersistentFunc &) { return Geometry::New(r); };
    return job.run(info, node_gdal::is_async<Async>, 1);
  }
};

} // namespace

GDAL_ASYNC_METHOD(Geometry::simplify, simplifyBody)

/**
 * Reduces the geometry complexity while preserving the topology.
 *
 * @method simplifyPreserveTopology
 * @instance
 * @memberof Geometry
 * @param {number} tolerance
 * @throws {Error}
 * @return {Geometry}
 */

/**
 * Reduces the geometry complexity while preserving the topology.
 * @async
 *
 * @method simplifyPreserveTopologyAsync
 * @instance
 * @memberof Geometry
 * @param {number} tolerance
 * @param {callback<Geometry>} [callback=undefined]
 * @throws {Error}
 * @return {Promise<Geometry>}
 */

namespace {

struct simplifyPreserveTopologyBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    Args args(info);

    Geometry *geom = node_gdal::UnwrapWrapped<Geometry>(info.This().As<Napi::Object>());
    double tolerance;

    if (!args.get(0, "tolerance", tolerance)) return args.failed();
    OGRGeometry *gdal_geom = geom->get();

    GDALAsyncableJob<OGRGeometry *> job(0);
    job.main = [gdal_geom, tolerance](const GDALExecutionProgress &) {
      CPLErrorReset();
      auto r = gdal_geom->SimplifyPreserveTopology(tolerance);
      if (r == nullptr) throw GDALError::fromGDAL();
      return r;
    };
    job.rval = [](OGRGeometry *r, const GetFromPersistentFunc &) { return Geometry::New(r); };
    return job.run(info, node_gdal::is_async<Async>, 1);
  }
};

} // namespace

GDAL_ASYNC_METHOD(Geometry::simplifyPreserveTopology, simplifyPreserveTopologyBody)

/**
 * Buffers the geometry by the given distance.
 *
 * @method buffer
 * @instance
 * @memberof Geometry
 * @param {number} distance
 * @param {number} segments
 * @throws {Error}
 * @return {Geometry}
 */

/**
 * Buffers the geometry by the given distance.
 * @async
 *
 * @method bufferAsync
 * @instance
 * @memberof Geometry
 * @param {number} distance
 * @param {number} segments
 * @param {callback<Geometry>} [callback=undefined]
 * @throws {Error}
 * @return {Promise<Geometry>}
 */

namespace {

struct bufferBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    Args args(info);

    double distance;
    int number_of_segments = 30;

    if (!args.get(0, "distance", distance)) return args.failed();
    if (!args.optional(1, "number of segments", number_of_segments)) return args.failed();

    Geometry *geom = node_gdal::UnwrapWrapped<Geometry>(info.This().As<Napi::Object>());

    OGRGeometry *gdal_geom = geom->get();

    GDALAsyncableJob<OGRGeometry *> job(0);
    job.main = [gdal_geom, distance, number_of_segments](const GDALExecutionProgress &) {
      CPLErrorReset();
      auto r = gdal_geom->Buffer(distance, number_of_segments);
      if (r == nullptr) throw GDALError::fromGDAL();
      return r;
    };
    job.rval = [](OGRGeometry *r, const GetFromPersistentFunc &) { return Geometry::New(r); };
    return job.run(info, node_gdal::is_async<Async>, 2);
  }
};

} // namespace

GDAL_ASYNC_METHOD(Geometry::buffer, bufferBody)

#if GDAL_VERSION_MAJOR >= 3
/**
 * Attempts to make an invalid geometry valid without losing vertices.
 * Requires GDAL 3.0
 *
 * @method makeValid
 * @instance
 * @memberof Geometry
 * @throws {Error}
 * @return {Geometry}
 */

/**
 * Attempts to make an invalid geometry valid without losing vertices.
 * Requires GDAL 3.0
 * @async
 *
 * @method makeValidAsync
 * @instance
 * @memberof Geometry
 * @param {callback<Geometry>} [callback=undefined]
 * @return {Promise<Geometry>}
 */

namespace {

struct makeValidBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {

    Geometry *geom = node_gdal::UnwrapWrapped<Geometry>(info.This().As<Napi::Object>());
    OGRGeometry *gdal_geom = geom->get();
    GDALAsyncableJob<OGRGeometry *> job(0);
    job.main = [gdal_geom](const GDALExecutionProgress &) {
      CPLErrorReset();
      auto r = gdal_geom->MakeValid();
      if (r == nullptr) throw GDALError::fromGDAL();
      return r;
    };
    job.rval = [](OGRGeometry *r, const GetFromPersistentFunc &) { return Geometry::New(r); };
    return job.run(info, node_gdal::is_async<Async>, 0);
  }
};

} // namespace

GDAL_ASYNC_METHOD(Geometry::makeValid, makeValidBody)
#endif

/**
 * Convert a geometry into well known text format.
 *
 * @method toWKT
 * @instance
 * @memberof Geometry
 * @throws {Error}
 * @return {string}
 */

/**
 * Convert a geometry into well known text format.
 * @async
 *
 * @method toWKTAsync
 * @instance
 * @memberof Geometry
 * @param {callback<string>} [callback=undefined]
 * @throws {Error}
 * @return {Promise<string>}
 */

namespace {

struct exportToWKTBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {

    Geometry *geom = node_gdal::UnwrapWrapped<Geometry>(info.This().As<Napi::Object>());

    OGRGeometry *gdal_geom = geom->get();
    uv_sem_t *async_lock = geom->lock();
    GDALAsyncableJob<char *> job(0);
    job.main = [async_lock, gdal_geom](const GDALExecutionProgress &) {
      char *text = NULL;
      uv_sem_wait(async_lock);
      OGRErr err = gdal_geom->exportToWkt(&text);
      uv_sem_post(async_lock);
      if (err) { throw GDALError(getOGRErrMsg(err)); }
      return text;
    };

    job.rval = [](char *text, const GetFromPersistentFunc &) {
      if (text) {
        auto r = SafeString::New(text);
        CPLFree(text);
        return r;
      }
      return node_gdal::napi_env().Undefined().As<Napi::Value>();
    };

    return job.run(info, node_gdal::is_async<Async>, 0);
  }
};

} // namespace

GDAL_ASYNC_METHOD(Geometry::exportToWKT, exportToWKTBody)

/**
 * Convert a geometry into well known binary format.
 *
 * @method toWKB
 * @instance
 * @memberof Geometry
 * @param {string} [byte_order="MSB"] {@link wkbByteOrder|see options}
 * @param {string} [variant="OGC"] ({@link wkbVariant|see options})
 * @throws {Error}
 * @return {Buffer}
 */

/**
 * Convert a geometry into well known binary format.
 * @async
 *
 * @method toWKBAsync
 * @instance
 * @memberof Geometry
 * @param {string} [byte_order="MSB"] {@link wkbByteOrder|see options}
 * @param {string} [variant="OGC"] ({@link wkbVariant|see options})
 * @param {callback<Buffer>} [callback=undefined]
 * @throws {Error}
 * @return {Promise<Buffer>}
 */

namespace {

struct exportToWKBBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    Args args(info);

    Geometry *geom = node_gdal::UnwrapWrapped<Geometry>(info.This().As<Napi::Object>());

    size_t size = geom->get()->WkbSize();

    // byte order
    OGRwkbByteOrder byte_order;
    std::string order = "MSB";
    if (!args.optional(0, "byte order", order)) return args.failed();
    if (order == "MSB") {
      byte_order = wkbXDR;
    } else if (order == "LSB") {
      byte_order = wkbNDR;
    } else {
      Napi::Error::New(node_gdal::napi_env(), "byte order must be 'MSB' or 'LSB'").ThrowAsJavaScriptException();
      return node_gdal::napi_env().Undefined();
    }

    // wkb variant
    OGRwkbVariant wkb_variant;
    std::string variant = "OGC";
    if (!args.optional(1, "wkb variant", variant)) return args.failed();
    if (variant == "OGC") {
      wkb_variant = wkbVariantOldOgc;
    } else if (variant == "ISO") {
      wkb_variant = wkbVariantIso;
    } else {
      Napi::Error::New(node_gdal::napi_env(), "variant must be 'OGC' or 'ISO'").ThrowAsJavaScriptException();
      return node_gdal::napi_env().Undefined();
    }

    unsigned char *data = (unsigned char *)malloc(size);
    if (data == nullptr) {
      Napi::Error::New(node_gdal::napi_env(), "Failed allocating memory").ThrowAsJavaScriptException();
      return node_gdal::napi_env().Undefined();
    }

    OGRGeometry *gdal_geom = geom->get();
    uv_sem_t *async_lock = geom->lock();
    GDALAsyncableJob<unsigned char *> job(0);
    job.main = [async_lock, gdal_geom, data, byte_order, wkb_variant](const GDALExecutionProgress &) {
      uv_sem_wait(async_lock);
      OGRErr err = gdal_geom->exportToWkb(byte_order, data, wkb_variant);
      uv_sem_post(async_lock);
      if (err) {
        free(data);
        throw GDALError(getOGRErrMsg(err));
      }
      return data;
    };

    Napi::MemoryManagement::AdjustExternalMemory(node_gdal::napi_env(), size);

    job.rval = [size](unsigned char *data, const GetFromPersistentFunc &) {
      int *hint = new int{static_cast<int>(size)};
      Napi::Value result = Napi::Buffer<char>::New(
                              node_gdal::napi_env(),
                              reinterpret_cast<char *>(data),
                              size,
                              [](Napi::Env, char *data, void *hint) {
                                int *size = reinterpret_cast<int *>(hint);
                                Napi::MemoryManagement::AdjustExternalMemory(node_gdal::napi_env(), -(*size));
                                delete size;
                                free(data);
                              },
                              hint);
      return result;
    };
    return job.run(info, node_gdal::is_async<Async>, 2);
  }
};

} // namespace

GDAL_ASYNC_METHOD(Geometry::exportToWKB, exportToWKBBody)

/**
 * Convert a geometry into KML format.
 *
 * @method toKML
 * @instance
 * @memberof Geometry
 * @throws {Error}
 * @return {string}
 */

/**
 * Convert a geometry into KML format.
 * @async
 *
 * @method toKMLAsync
 * @instance
 * @memberof Geometry
 * @param {callback<string>} [callback=undefined]
 * @throws {Error}
 * @return {Promise<string>}
 */

namespace {

struct exportToKMLBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {

    Geometry *geom = node_gdal::UnwrapWrapped<Geometry>(info.This().As<Napi::Object>());

    OGRGeometry *gdal_geom = geom->get();
    uv_sem_t *async_lock = geom->lock();
    GDALAsyncableJob<char *> job(0);
    job.main = [async_lock, gdal_geom](const GDALExecutionProgress &) {
      CPLErrorReset();
      uv_sem_wait(async_lock);
      char *text = gdal_geom->exportToKML();
      uv_sem_post(async_lock);
      if (text == nullptr) throw GDALError::fromGDAL();
      return text;
    };
    job.rval = [](char *text, const GetFromPersistentFunc &) {
      if (text) {
        Napi::Value result = Napi::String::New(node_gdal::napi_env(), text);
        CPLFree(text);
        return result;
      }
      return node_gdal::napi_env().Undefined().As<Napi::Value>();
    };
    return job.run(info, node_gdal::is_async<Async>, 0);
  }
};

} // namespace

GDAL_ASYNC_METHOD(Geometry::exportToKML, exportToKMLBody)

/**
 * Convert a geometry into GML format.
 *
 * @method toGML
 * @instance
 * @memberof Geometry
 * @throws {Error}
 * @return {string}
 */

/**
 * Convert a geometry into GML format.
 * @async
 *
 * @method toGMLAsync
 * @instance
 * @memberof Geometry
 * @param {callback<string>} [callback=undefined]
 * @throws {Error}
 * @return {Promise<string>}
 */

namespace {

struct exportToGMLBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {

    Geometry *geom = node_gdal::UnwrapWrapped<Geometry>(info.This().As<Napi::Object>());

    OGRGeometry *gdal_geom = geom->get();
    uv_sem_t *async_lock = geom->lock();
    GDALAsyncableJob<char *> job(0);
    job.main = [async_lock, gdal_geom](const GDALExecutionProgress &) {
      CPLErrorReset();
      uv_sem_wait(async_lock);
      char *text = gdal_geom->exportToGML();
      uv_sem_post(async_lock);
      if (text == nullptr) throw GDALError::fromGDAL();
      return text;
    };
    job.rval = [](char *text, const GetFromPersistentFunc &) {
      if (text) {
        Napi::Value result = Napi::String::New(node_gdal::napi_env(), text);
        CPLFree(text);
        return result;
      }
      return node_gdal::napi_env().Undefined().As<Napi::Value>();
    };
    return job.run(info, node_gdal::is_async<Async>, 0);
  }
};

} // namespace

GDAL_ASYNC_METHOD(Geometry::exportToGML, exportToGMLBody)

/**
 * Convert a geometry into JSON format.
 *
 * @method toJSON
 * @instance
 * @memberof Geometry
 * @throws {Error}
 * @return {string}
 */

/**
 * Convert a geometry into JSON format.
 * @async
 *
 * @method toJSONAsync
 * @instance
 * @memberof Geometry
 * @param {callback<string>} [callback=undefined]
 * @throws {Error}
 * @return {Promise<string>}
 */

namespace {

struct exportToJSONBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {

    Geometry *geom = node_gdal::UnwrapWrapped<Geometry>(info.This().As<Napi::Object>());

    OGRGeometry *gdal_geom = geom->get();
    uv_sem_t *async_lock = geom->lock();
    GDALAsyncableJob<char *> job(0);
    job.main = [async_lock, gdal_geom](const GDALExecutionProgress &) {
      CPLErrorReset();
      uv_sem_wait(async_lock);
      char *text = gdal_geom->exportToJson();
      uv_sem_post(async_lock);
      if (text == nullptr) throw GDALError::fromGDAL();
      return text;
    };
    job.rval = [](char *text, const GetFromPersistentFunc &) {
      if (text) {
        Napi::Value result = Napi::String::New(node_gdal::napi_env(), text);
        CPLFree(text);
        return result;
      }
      return node_gdal::napi_env().Undefined().As<Napi::Value>();
    };
    return job.run(info, node_gdal::is_async<Async>, 0);
  }
};

} // namespace

GDAL_ASYNC_METHOD(Geometry::exportToJSON, exportToJSONBody)

/**
 * Compute the centroid of the geometry.
 *
 * @method centroid
 * @instance
 * @memberof Geometry
 * @throws {Error}
 * @return {Point}
 */

/**
 * Compute the centroid of the geometry.
 * @async
 *
 * @method centroidAsync
 * @instance
 * @memberof Geometry
 * @param {callback<Geometry>} [callback=undefined]
 * @throws {Error}
 * @return {Promise<Geometry>}
 */

namespace {

struct centroidBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    // The Centroid method wants the caller to create the point to fill in.
    // Instead of requiring the caller to create the point geometry to fill in, we
    // new up an OGRPoint and put the result into it and return that.

    Geometry *geom = node_gdal::UnwrapWrapped<Geometry>(info.This().As<Napi::Object>());

    OGRGeometry *gdal_geom = geom->get();
    uv_sem_t *async_lock = geom->lock();
    GDALAsyncableJob<OGRPoint *> job(0);
    job.main = [async_lock, gdal_geom](const GDALExecutionProgress &) {
      OGRPoint *point = new OGRPoint();
      uv_sem_wait(async_lock);
      OGRErr err = gdal_geom->Centroid(point);
      uv_sem_post(async_lock);
      if (err) {
        delete point;
        throw GDALError(getOGRErrMsg(err));
      }
      return point;
    };
    job.rval = [](OGRPoint *point, const GetFromPersistentFunc &) { return Point::New(point); };
    return job.run(info, node_gdal::is_async<Async>, 0);
  }
};

} // namespace

GDAL_ASYNC_METHOD(Geometry::centroid, centroidBody)

/**
 * Computes the bounding box (envelope).
 *
 * @method getEnvelope
 * @instance
 * @memberof Geometry
 * @return {Envelope} Bounding envelope
 */

/**
 * Computes the bounding box (envelope).
 * @async
 *
 * @method getEnvelopeAsync
 * @instance
 * @memberof Geometry
 * @param {callback<Envelope>} [callback=undefined]
 * @return {Promise<Envelope>}
 */
namespace {

struct getEnvelopeBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    // returns object containing boundaries until complete OGREnvelope binding is
    // built

    Geometry *geom = node_gdal::UnwrapWrapped<Geometry>(info.This().As<Napi::Object>());

    OGRGeometry *gdal_geom = geom->get();
    uv_sem_t *async_lock = geom->lock();

    GDALAsyncableJob<OGREnvelope *> job(0);
    job.main = [async_lock, gdal_geom](const GDALExecutionProgress &) {
      OGREnvelope *envelope = new OGREnvelope();
      uv_sem_wait(async_lock);
      gdal_geom->getEnvelope(envelope);
      uv_sem_post(async_lock);
      return envelope;
    };

    job.rval = [](OGREnvelope *envelope, const GetFromPersistentFunc &) {
      Napi::Object obj = Napi::Object::New(node_gdal::napi_env());
      obj.Set( Napi::String::New(node_gdal::napi_env(), "minX"), Napi::Number::New(node_gdal::napi_env(), envelope->MinX));
      obj.Set( Napi::String::New(node_gdal::napi_env(), "maxX"), Napi::Number::New(node_gdal::napi_env(), envelope->MaxX));
      obj.Set( Napi::String::New(node_gdal::napi_env(), "minY"), Napi::Number::New(node_gdal::napi_env(), envelope->MinY));
      obj.Set( Napi::String::New(node_gdal::napi_env(), "maxY"), Napi::Number::New(node_gdal::napi_env(), envelope->MaxY));
      delete envelope;
      return obj;
    };
    return job.run(info, node_gdal::is_async<Async>, 0);
  }
};

} // namespace

GDAL_ASYNC_METHOD(Geometry::getEnvelope, getEnvelopeBody)

/**
 * Computes the 3D bounding box (envelope).
 *
 * @method getEnvelope3D
 * @instance
 * @memberof Geometry
 * @return {Envelope3D} Bounding envelope
 */

/**
 * Computes the 3D bounding box (envelope).
 * @async
 *
 * @method getEnvelope3DAsync
 * @instance
 * @memberof Geometry
 * @param {callback<Geometry>} [callback=undefined]
 * @return {Promise<Geometry>}
 */

namespace {

struct getEnvelope3DBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    // returns object containing boundaries until complete OGREnvelope binding is
    // built

    Geometry *geom = node_gdal::UnwrapWrapped<Geometry>(info.This().As<Napi::Object>());

    OGRGeometry *gdal_geom = geom->get();
    uv_sem_t *async_lock = geom->lock();

    GDALAsyncableJob<OGREnvelope3D *> job(0);
    job.main = [async_lock, gdal_geom](const GDALExecutionProgress &) {
      OGREnvelope3D *envelope = new OGREnvelope3D();
      uv_sem_wait(async_lock);
      gdal_geom->getEnvelope(envelope);
      uv_sem_post(async_lock);
      return envelope;
    };

    job.rval = [](OGREnvelope3D *envelope, const GetFromPersistentFunc &) {
      Napi::Object obj = Napi::Object::New(node_gdal::napi_env());
      obj.Set( Napi::String::New(node_gdal::napi_env(), "minX"), Napi::Number::New(node_gdal::napi_env(), envelope->MinX));
      obj.Set( Napi::String::New(node_gdal::napi_env(), "maxX"), Napi::Number::New(node_gdal::napi_env(), envelope->MaxX));
      obj.Set( Napi::String::New(node_gdal::napi_env(), "minY"), Napi::Number::New(node_gdal::napi_env(), envelope->MinY));
      obj.Set( Napi::String::New(node_gdal::napi_env(), "maxY"), Napi::Number::New(node_gdal::napi_env(), envelope->MaxY));
      obj.Set( Napi::String::New(node_gdal::napi_env(), "minZ"), Napi::Number::New(node_gdal::napi_env(), envelope->MinZ));
      obj.Set( Napi::String::New(node_gdal::napi_env(), "maxZ"), Napi::Number::New(node_gdal::napi_env(), envelope->MaxZ));
      delete envelope;
      return obj;
    };
    return job.run(info, node_gdal::is_async<Async>, 0);
  }
};

} // namespace

GDAL_ASYNC_METHOD(Geometry::getEnvelope3D, getEnvelope3DBody)

/**
 * Convert geometry to strictly 2D.
 *
 * @method flattenTo2D
 * @instance
 * @memberof Geometry
 * @return {void}
 */

/**
 * Convert geometry to strictly 2D.
 * @async
 *
 * @method flattenTo2DAsync
 * @instance
 * @memberof Geometry
 * @param {callback<void>} [callback=undefined]
 * @return {Promise<void>}
 */

namespace {
// Geometry.flattenTo2D()
struct flattenTo2DBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    auto obj = node_gdal::unwrapThis<Geometry>(info, "Geometry", async);
    if (!obj) { return obj.failure; }
    auto *gdal_obj = obj->get();
    GDALAsyncableJob<int> job(0);
    job.main = [gdal_obj](const GDALExecutionProgress &) {
      gdal_obj->flattenTo2D();
      return 0;
    };
    job.rval = [](int, const GetFromPersistentFunc &) { return v8_undefined(); };
    return job.run(info, node_gdal::is_async<Async>, 0);
  }
};
} // namespace

GDAL_ASYNC_METHOD(Geometry::flattenTo2D, flattenTo2DBody)

// --- JS static methods (OGRGeometryFactory) ---

/**
 * Creates a Geometry from a WKT string.
 *
 * @static
 * @method fromWKT
 * @instance
 * @memberof Geometry
 * @throws {Error}
 * @param {string} wkt
 * @param {SpatialReference} [srs]
 * @return {Geometry}
 */

/**
 * Creates a Geometry from a WKT string.
 * @async
 *
 * @static
 * @method fromWKTAsync
 * @instance
 * @memberof Geometry
 * @throws {Error}
 * @param {string} wkt
 * @param {SpatialReference} [srs]
 * @param {callback<Geometry>} [callback=undefined]
 * @return {Promise<Geometry>}
 */

namespace {

struct createFromWktBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    Args args(info);

    std::string *wkt_string = new std::string;
    SpatialReference *srs = NULL;

    if (!args.get(0, "wkt", *wkt_string)) return args.failed();
    if (!args.optionalWrapped(1, "srs", "SpatialReference", srs)) return args.failed();

    OGRSpatialReference *ogr_srs = NULL;
    if (srs) { ogr_srs = srs->get(); }

    GDALAsyncableJob<OGRGeometry *> job(0);
    job.main = [wkt_string, ogr_srs](const GDALExecutionProgress &) {
      std::unique_ptr<std::string> wkt_string_ptr(wkt_string);
      OGRGeometry *geom = NULL;
      OGRChar *wkt = (OGRChar *)wkt_string->c_str();
      OGRErr err = OGRGeometryFactory::createFromWkt(&wkt, ogr_srs, &geom);
      if (err) throw GDALError(getOGRErrMsg(err));
      return geom;
    };
    job.rval = [](OGRGeometry *geom, const GetFromPersistentFunc &) { return Geometry::New(geom, true); };
    return job.run(info, node_gdal::is_async<Async>, 2);
  }
};

} // namespace

GDAL_ASYNC_METHOD(Geometry::createFromWkt, createFromWktBody)

/**
 * Creates a Geometry from a WKB buffer.
 *
 * @static
 * @method fromWKB
 * @instance
 * @memberof Geometry
 * @throws {Error}
 * @param {Buffer} wkb
 * @param {SpatialReference} [srs]
 * @return {Geometry}
 */

/**
 * Creates a Geometry from a WKB buffer.
 * @async
 *
 * @static
 * @method fromWKBAsync
 * @instance
 * @memberof Geometry
 * @throws {Error}
 * @param {Buffer} wkb
 * @param {SpatialReference} [srs]
 * @param {callback<Geometry>} [callback=undefined]
 * @return {Promise<Geometry>}
 */

namespace {

struct createFromWkbBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    Args args(info);

    std::string wkb_string;
    SpatialReference *srs = NULL;

    Napi::Object wkb_obj;
    if (!args.get(0, "wkb", wkb_obj)) return args.failed();
    if (!args.optionalWrapped(1, "srs", "SpatialReference", srs)) return args.failed();

    // N-API has no GetConstructorName: check the type directly
    if (!wkb_obj.IsBuffer() && !wkb_obj.IsTypedArray()) {
      Napi::Error::New(node_gdal::napi_env(), "Argument must be a buffer object").ThrowAsJavaScriptException();
      return node_gdal::napi_env().Undefined();
    }

    unsigned char *data = (unsigned char *)wkb_obj.As<Napi::Buffer<uint8_t>>().Data();
    size_t length = wkb_obj.As<Napi::Buffer<uint8_t>>().Length();

    OGRSpatialReference *ogr_srs = NULL;
    if (srs) { ogr_srs = srs->get(); }

    GDALAsyncableJob<OGRGeometry *> job(0);
    job.main = [data, length, ogr_srs](const GDALExecutionProgress &) {
      OGRGeometry *geom = NULL;
      OGRErr err = OGRGeometryFactory::createFromWkb(data, ogr_srs, &geom, length);
      if (err) throw GDALError(getOGRErrMsg(err));
      return geom;
    };
    job.rval = [](OGRGeometry *geom, const GetFromPersistentFunc &) { return Geometry::New(geom, true); };
    return job.run(info, node_gdal::is_async<Async>, 2);
  }
};

} // namespace

GDAL_ASYNC_METHOD(Geometry::createFromWkb, createFromWkbBody)

/**
 * Creates a Geometry from a GeoJSON object fragment.
 * The async version depends on V8 for object serialization and this part is not parallelizable.
 * V8 objects cannot be accessed outside of the main thread. This function should not be used
 * for importing objects of more than few tens of Kbytes when low latency is needed. If you need
 * to import very large GeoJSON geometries in server code, use the much faster and completely
 * parallel fromGeoJonBuffer(Async)
 *
 * @static
 * @method fromGeoJson
 * @instance
 * @memberof Geometry
 * @throws {Error}
 * @param {object} geojson
 * @return {Geometry}
 */

/**
 * Creates a Geometry from a GeoJSON object fragment.
 * The async version depends on V8 for object serialization and this part is not parallelizable.
 * V8 objects cannot be accessed outside of the main thread. This function should not be used
 * for importing objects of more than few tens of Kbytes when low latency is needed. If you need
 * to import very large GeoJSON geometries in server code, use the much faster and completely
 * parallel fromGeoJonBuffer(Async)
 * @async
 *
 * @static
 * @method fromGeoJsonAsync
 * @instance
 * @memberof Geometry
 * @throws {Error}
 * @param {object} geojson
 * @param {callback<Geometry>} [callback=undefined]
 * @return {Promise<Geometry>}
 */
namespace {

struct createFromGeoJsonBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    Args args(info);
  #if GDAL_VERSION_MAJOR == 2 && GDAL_VERSION_MINOR < 3
    Napi::Error::New(node_gdal::napi_env(), "GDAL < 2.3 does not support parsing GeoJSON directly").ThrowAsJavaScriptException();
    return node_gdal::napi_env().Undefined();
  #else

    Napi::Object geo_obj;
    if (!args.get(0, "geojson", geo_obj)) return args.failed();

    // goes to text to pass it in, there isn't a performant way to
    // go from v8 JSON -> CPLJSON anyways
    // node-addon-api has no JSON class: reach the global JSON.stringify
    Napi::Value json_stringified = node_gdal::napi_env()
                                     .Global()
                                     .Get("JSON")
                                     .As<Napi::Object>()
                                     .Get("stringify")
                                     .As<Napi::Function>()
                                     .Call({geo_obj});
    if (json_stringified.IsEmpty() || !json_stringified.IsString()) {
      Napi::Error::New(node_gdal::napi_env(), "Invalid GeoJSON").ThrowAsJavaScriptException();
      return node_gdal::napi_env().Undefined();
    }
    std::string *val = new std::string(json_stringified.As<Napi::String>().Utf8Value());

    GDALAsyncableJob<OGRGeometry *> job(0);
    job.main = [val](const GDALExecutionProgress &) {
      CPLErrorReset();
      std::unique_ptr<std::string> val_ptr(val);
      OGRGeometry *geom = OGRGeometryFactory::createFromGeoJson(val->c_str());
      if (geom == nullptr) throw GDALError::fromGDAL();
      return geom;
    };
    job.rval = [](OGRGeometry *geom, const GetFromPersistentFunc &) { return Geometry::New(geom, true); };
    return job.run(info, node_gdal::is_async<Async>, 1);
  #endif
  }
};

} // namespace

GDAL_ASYNC_METHOD(Geometry::createFromGeoJson, createFromGeoJsonBody)

/**
 * Creates a Geometry from a buffer containing a GeoJSON fragment in UT8 format.
 *
 * @static
 * @method fromGeoJsonBuffer
 * @instance
 * @memberof Geometry
 * @throws {Error}
 * @param {Buffer} geojson
 * @return {Geometry}
 */

/**
 * Creates a Geometry from a buffer containing a GeoJSON fragment in UT8 format.
 * @async
 *
 * @static
 * @method fromGeoJsonBufferAsync
 * @instance
 * @memberof Geometry
 * @throws {Error}
 * @param {Buffer} geojson
 * @param {callback<Geometry>} [callback=undefined]
 * @return {Promise<Geometry>}
 */

namespace {

struct createFromGeoJsonBufferBody : node_gdal::AsyncMethodBody {
  template <class Async> static Napi::Value run(const Napi::CallbackInfo &info, Async async) {
    Args args(info);
  #if GDAL_VERSION_MAJOR == 2 && GDAL_VERSION_MINOR < 3
    Napi::Error::New(node_gdal::napi_env(), "GDAL < 2.3 does not support parsing GeoJSON directly").ThrowAsJavaScriptException();
    return node_gdal::napi_env().Undefined();
  #else
    std::string geojson_string;

    Napi::Object geojson_obj;
    if (!args.get(0, "geojson", geojson_obj)) return args.failed();

    // N-API has no GetConstructorName
    if (!geojson_obj.IsBuffer() && !geojson_obj.IsTypedArray()) {
      Napi::Error::New(node_gdal::napi_env(), "Argument must be a buffer object").ThrowAsJavaScriptException();
      return node_gdal::napi_env().Undefined();
    }

    unsigned char *data = (unsigned char *)geojson_obj.As<Napi::Buffer<uint8_t>>().Data();
    size_t length = geojson_obj.As<Napi::Buffer<uint8_t>>().Length();

    GDALAsyncableJob<OGRGeometry *> job(0);
    job.main = [data, length](const GDALExecutionProgress &) {
      CPLErrorReset();
      CPLJSONDocument oDocument;
      if (!oDocument.LoadMemory(reinterpret_cast<const GByte *>(data), length)) {
        throw GDALError("Parsing the GeoJSON fragment failed");
      }
      OGRGeometry *geom = OGRGeometryFactory::createFromGeoJson(oDocument.GetRoot());

      if (geom == nullptr) throw GDALError::fromGDAL();
      return geom;
    };
    job.rval = [](OGRGeometry *geom, const GetFromPersistentFunc &) { return Geometry::New(geom, true); };
    return job.run(info, node_gdal::is_async<Async>, 1);
  #endif
  }
};

} // namespace

GDAL_ASYNC_METHOD(Geometry::createFromGeoJsonBuffer, createFromGeoJsonBufferBody)

/**
 * @kind member
 * @name srs
 * @instance
 * @memberof Geometry
 * @type {SpatialReference|null}
 */
NAPI_GETTER(Geometry::srsGetter) {
  Geometry *geom = node_gdal::UnwrapWrapped<Geometry>(info.This().As<Napi::Object>());
  return SpatialReference::New(geom->get()->getSpatialReference());
}

NAPI_SETTER(Geometry::srsSetter) {
  Geometry *geom = node_gdal::UnwrapWrapped<Geometry>(info.This().As<Napi::Object>());

  OGRSpatialReference *srs = NULL;
  if (IS_WRAPPED(value, SpatialReference)) {
    SpatialReference *srs_obj = node_gdal::UnwrapWrapped<SpatialReference>(value.As<Napi::Object>());
    srs = srs_obj->get();
  } else if (!value.IsNull() && !value.IsUndefined()) {
    Napi::Error::New(node_gdal::napi_env(), "srs must be SpatialReference object").ThrowAsJavaScriptException();
    return;
  }

  geom->get()->assignSpatialReference(srs);
}

/**
 * @readonly
 * @kind member
 * @name name
 * @instance
 * @memberof Geometry
 * @type {string}
 */
NAPI_GETTER(Geometry::nameGetter) {
  Geometry *geom = node_gdal::UnwrapWrapped<Geometry>(info.This().As<Napi::Object>());
  return SafeString::New(geom->get()->getGeometryName());
}

/**
 * See {@link wkbGeometryType}.
 * @readonly
 * @kind member
 * @name wkbType
 * @static
 * @memberof Geometry
 * @type {number}
 */

/**
 * See {@link wkbGeometryType}.
 * @readonly
 * @kind member
 * @name wkbType
 * @instance
 * @memberof Geometry
 * @type {number}
 */
NAPI_GETTER(Geometry::typeGetter) {
  Geometry *geom = node_gdal::UnwrapWrapped<Geometry>(info.This().As<Napi::Object>());
  return Napi::Number::New(node_gdal::napi_env(), getGeometryType_fixed(geom->get()));
}

/**
 * @readonly
 * @kind member
 * @name wkbSize
 * @instance
 * @memberof Geometry
 * @type {number}
 */
NAPI_GETTER(Geometry::wkbSizeGetter) {
  Geometry *geom = node_gdal::UnwrapWrapped<Geometry>(info.This().As<Napi::Object>());
  return Napi::Number::New(node_gdal::napi_env(), static_cast<int>(geom->get()->WkbSize()));
}

/**
 * @readonly
 * @kind member
 * @name dimension
 * @instance
 * @memberof Geometry
 * @type {number}
 */
NAPI_GETTER(Geometry::dimensionGetter) {
  Geometry *geom = node_gdal::UnwrapWrapped<Geometry>(info.This().As<Napi::Object>());
  return Napi::Number::New(node_gdal::napi_env(), geom->get()->getDimension());
}

/**
 * @kind member
 * @name coordinateDimension
 * @instance
 * @memberof Geometry
 * @type {number}
 */
NAPI_GETTER(Geometry::coordinateDimensionGetter) {
  Geometry *geom = node_gdal::UnwrapWrapped<Geometry>(info.This().As<Napi::Object>());
  return Napi::Number::New(node_gdal::napi_env(), geom->get()->getCoordinateDimension());
}

NAPI_SETTER(Geometry::coordinateDimensionSetter) {
  Geometry *geom = node_gdal::UnwrapWrapped<Geometry>(info.This().As<Napi::Object>());

  if (!value.IsNumber()) {
    Napi::Error::New(node_gdal::napi_env(), "coordinateDimension must be an integer").ThrowAsJavaScriptException();
    return;
  }
  int dim = value.As<Napi::Number>().Int64Value();
  if (dim != 2 && dim != 3) {
    Napi::Error::New(node_gdal::napi_env(), "coordinateDimension must be 2 or 3").ThrowAsJavaScriptException();
    return;
  }

  geom->get()->setCoordinateDimension(dim);
}

Napi::Value Geometry::getConstructor(OGRwkbGeometryType type) {

  type = wkbFlatten(type);
  switch (type) {
    case wkbPoint: return Point::constructor().Value();
    case wkbLineString: return LineString::constructor().Value();
    case wkbLinearRing: return LinearRing::constructor().Value();
    case wkbPolygon: return Polygon::constructor().Value();
    case wkbGeometryCollection:
      return GeometryCollection::constructor().Value();
    case wkbMultiPoint: return MultiPoint::constructor().Value();
    case wkbMultiLineString:
      return MultiLineString::constructor().Value();
    case wkbMultiPolygon: return MultiPolygon::constructor().Value();
    case wkbCircularString:
      return CircularString::constructor().Value();
    case wkbCompoundCurve: return CompoundCurve::constructor().Value();
    case wkbMultiCurve: return MultiCurve::constructor().Value();
    default: return node_gdal::napi_env().Null();
  }
}

/**
 * Returns the Geometry subclass that matches the
 * given WKB geometry type.
 *
 * @static
 * @method getConstructor
 * @instance
 * @memberof Geometry
 * @param {number} type WKB geometry type {@link wkbGeometryType|available options}
 * @return {Function}
 */
NAPI_METHOD(Geometry::getConstructor) {
  Args args(info);
  OGRwkbGeometryType type;
  if (!args.getEnum(0, "wkbType", "OGRwkbGeometryType", type)) return args.failed();
  return getConstructor(type);
}

/**
 * Returns the Geometry subclass name that matches the
 * given WKB geometry type.
 *
 * @static
 * @method getName
 * @instance
 * @memberof Geometry
 * @param {number} type WKB geometry type {@link wkbGeometryType|available options}
 * @return {string}
 */
NAPI_METHOD(Geometry::getName) {
  Args args(info);
  OGRwkbGeometryType type;
  if (!args.getEnum(0, "wkbType", "OGRwkbGeometryType", type)) return args.failed();
  return SafeString::New(OGRGeometryTypeToName(type));
}

} // namespace node_gdal
