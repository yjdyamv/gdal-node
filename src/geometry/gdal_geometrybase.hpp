#ifndef __NODE_OGR_GEOMETRYBASE_H__
#define __NODE_OGR_GEOMETRYBASE_H__

#include "../gdal_common.hpp"

namespace node_gdal {

// The wrapper owns the geometry it holds: the factory clones the one of a
// feature, or the geometry was created by JS
template <typename OGRT> inline void free_geometry(void *ptr) {
  OGRGeometryFactory::destroyGeometry(static_cast<OGRGeometry *>(ptr));
}

/*
 * Geometry class inheritance hierarchy.
 * It uses CRTP - https://en.wikipedia.org/wiki/Curiously_recurring_template_pattern
 * to get around the fact that the methods exposed to JS are static and cannot be virtual.
 *
 *
 * C++
 * (maximizes code reuse)
 *
 * GeometryBase<>
 *    Geometry
 *    Point
 *    CurveBase<>
 *        SimpleCurve
 *        LineString
 *        CircularString
 *        LinearRing
 *        Polygon
 *        CompoundCurve
 *    GeometryCollectionBase<>
 *        GeometryCollection
 *        Multi*
 *
 *
 * JS
 * (tries to follow GDAL and the ISO specification)
 *
 * Geometry
 *    Point
 *    SimpleCurve
 *        LineString
 *            LinearRing
 *        CircularString
 *    Polygon
 *    CompoundCurve
 *    GeometryCollection
 *        Multi*
 *
 *
 * The full GDAL OGRGeometry class hierarchy
 * https://gdal.org/doxygen/classOGRGeometry.html
 *
 * Every concrete class passes itself as T, which is exactly what
 * Napi::ObjectWrap needs, so the CRTP maps onto it one to one.
 */

// OGRSimpleCurve and OGRGeometry are abstract (Geometry/SimpleCurve only exist
// so that the concrete classes can be `instanceof` them) - nothing to construct
template <typename OGRT> [[nodiscard]] inline OGRT *MakeGeometry() {
  return new OGRT();
}
template <> [[nodiscard]] inline OGRSimpleCurve *MakeGeometry<OGRSimpleCurve>() {
  return nullptr;
}
template <> [[nodiscard]] inline OGRGeometry *MakeGeometry<OGRGeometry>() {
  return nullptr;
}

template <class T, class OGRT> class GeometryBase : public GDALObject<T> {
    public:
  static Napi::Value New(OGRT *geom);
  static Napi::Value New(OGRT *geom, bool owned);

  GeometryBase(const Napi::CallbackInfo &info);
  // The lock the bodies of the methods wait on, which used to be reachable
  // because they were members of the class
  inline uv_sem_t *lock() {
    return async_lock;
  }
  inline OGRT *get() {
    return this_;
  }
  inline bool isAlive() {
    return this_;
  }

  //
  // A geometry which the binding owns reports its size to V8; a copy reports
  // the size of the buffer it was copied into, not the size of the geometry
  //
  inline void updateAmountOfGeometryMemory() {
    if (this_.owned()) this->setExternalMemory(this_->WkbSize());
  }

    protected:
  ~GeometryBase();
  Handle<OGRT> this_;
  // true when the wrapper was created by a factory rather than from JS: the base
  // constructor then takes the OGR object from the thread-local slot
  bool from_factory_;
  uv_sem_t *async_lock;
};

template <class T, class OGRT> Napi::Value GeometryBase<T, OGRT>::New(OGRT *geom) {
  return T::New(geom, true);
}

template <class T, class OGRT> Napi::Value GeometryBase<T, OGRT>::New(OGRT *geom, bool owned) {
  Napi::Env env = node_gdal::napi_env();

  if (!geom) { return env.Null(); }

  // make a copy of geometry owned by a feature
  // + no need to track when a feature is destroyed
  // + no need to throw errors when a method tries to modify an owned read-only
  // geometry
  // - is slower

  if (!owned) { geom = static_cast<OGRT *>(geom->clone()); }

  // node-addon-api allocates the wrapper itself, so the factory hands the OGR
  // object over directly - unlike the old binding
  node_gdal::detail::PendingObjectGuard pending(geom, &free_geometry<OGRT>);
  std::vector<napi_value> args;
  Napi::Object obj = T::constructor().Value().New(args);

  T *wrapped = node_gdal::UnwrapWrapped<T>(obj);

  wrapped->updateAmountOfGeometryMemory();

  return obj;
}

template <class T, class OGRT>
GeometryBase<T, OGRT>::GeometryBase(const Napi::CallbackInfo &info)
  : GDALObject<T>(info), from_factory_(false) {
  // The async locks must live outside the V8 memory management,
  // otherwise they won't be accessible from the async threads
  async_lock = new uv_sem_t;
  uv_sem_init(async_lock, 1);

  Handle<OGRT> pending = node_gdal::TakePendingHandle<OGRT>();
  if (pending) {
    from_factory_ = true;
    this_ = std::move(pending);
  } else {
    // Constructed from JS: the derived class interprets its own arguments
    this_ = Handle<OGRT>::owned(MakeGeometry<OGRT>(), &free_geometry<OGRT>);
  }
  LOG("Created Geometry %s [%p]", typeid(T).name(), this_);
}

template <class T, class OGRT> GeometryBase<T, OGRT>::~GeometryBase() {
  if (this_) {
    LOG("Disposing Geometry %s [%p]", typeid(T).name(), this_.get());
    this->setExternalMemory(0);
    // The handle destroys the geometry, if this wrapper owns it
    this_.reset();
  }
  uv_sem_destroy(async_lock);
  delete async_lock;
}

} // namespace node_gdal
#endif
