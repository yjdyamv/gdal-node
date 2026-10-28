#include "linestring_points.hpp"
#include "../utils/args.hpp"
#include "../gdal_common.hpp"
#include "../geometry/gdal_geometry.hpp"
#include "../geometry/gdal_linestring.hpp"
#include "../geometry/gdal_point.hpp"

namespace node_gdal {

Napi::FunctionReference &LineStringPoints::constructor() {
  return node_gdal::env_context().constructor<LineStringPoints>();
}

LineStringPoints::LineStringPoints(const Napi::CallbackInfo &info) : GDALObject<LineStringPoints>(info) {
  if (info.Length() < 1 || !info[0].IsObject()) {
    Napi::Error::New(info.Env(), "Cannot create LineStringPoints directly").ThrowAsJavaScriptException();
    return;
  }
  node_gdal::set_private(info.This(), "parent_", info[0]);
}

LineStringPoints::~LineStringPoints() {
}

/**
 * An encapsulation of a {@link LineString}'s points.
 *
 * @class LineStringPoints
 */

Napi::Value LineStringPoints::New(Napi::Value geom) {

  std::vector<napi_value> args = {geom};
  Napi::Object obj = LineStringPoints::constructor().Value().New(args);

  return obj;
}

/**
 * Returns the number of points that are part of the line string.
 *
 * @method count
 * @instance
 * @memberof LineStringPoints
 * @return {number}
 */
/**
 * Reverses the order of all the points.
 *
 * @method reverse
 * @instance
 * @memberof LineStringPoints
 */
/**
 * Adjusts the number of points that make up the line string.
 *
 * @method resize
 * @instance
 * @memberof LineStringPoints
 * @param {number} count
 */
/**
 * Returns the point at the specified index.
 *
 * @method get
 * @instance
 * @memberof LineStringPoints
 * @param {number} index 0-based index
 * @throws {Error}
 * @return {Point}
 */
/**
 * Sets the point at the specified index.
 *
 * @example
 *
 * lineString.points.set(0, new gdal.Point(1, 2));
 *
 * @method set
 * @instance
 * @memberof LineStringPoints
 * @throws {Error}
 * @param {number} index 0-based index
 * @param {Point|xyz} point
 */

/**
 * @method set
 * @instance
 * @memberof LineStringPoints
 * @throws {Error}
 * @param {number} index 0-based index
 * @param {number} x
 * @param {number} y
 * @param {number} [z]
 */
/**
 * Adds point(s) to the line string. Also accepts any object with an x and y
 * property.
 *
 * @example
 *
 * lineString.points.add(new gdal.Point(1, 2));
 * lineString.points.add([
 *     new gdal.Point(1, 2),
 *     new gdal.Point(3, 4)
 * ]);
 *
 * @method add
 * @instance
 * @memberof LineStringPoints
 * @throws {Error}
 * @param {Point|xyz|(Point|xyz)[]} points
 */

/**
 *
 * @method add
 * @instance
 * @memberof LineStringPoints
 * @throws {Error}
 * @param {number} x
 * @param {number} y
 * @param {number} [z]
 */
namespace {

Napi::Value toString(const Napi::CallbackInfo &info) {
  return Napi::String::New(node_gdal::napi_env(), "LineStringPoints");
}


Napi::Value count(const Napi::CallbackInfo &info) {

  Napi::Object parent =
    node_gdal::get_private(info.This(), "parent_").As<Napi::Object>();
  LineString *geom = node_gdal::UnwrapWrapped<LineString>(parent);

  return Napi::Number::New(node_gdal::napi_env(), geom->get()->getNumPoints());
}


Napi::Value reverse(const Napi::CallbackInfo &info) {

  Napi::Object parent =
    node_gdal::get_private(info.This(), "parent_").As<Napi::Object>();
  LineString *geom = node_gdal::UnwrapWrapped<LineString>(parent);

  geom->get()->reversePoints();

  return node_gdal::napi_env().Undefined();
}


Napi::Value resize(const Napi::CallbackInfo &info) {
  Args args(info);

  Napi::Object parent =
    node_gdal::get_private(info.This(), "parent_").As<Napi::Object>();
  LineString *geom = node_gdal::UnwrapWrapped<LineString>(parent);

  int count;
  if (!args.get(0, "point count", count)) return args.failed();
  geom->get()->setNumPoints(count);

  return node_gdal::napi_env().Undefined();
}


Napi::Value get(const Napi::CallbackInfo &info) {
  Args args(info);

  Napi::Object parent =
    node_gdal::get_private(info.This(), "parent_").As<Napi::Object>();
  LineString *geom = node_gdal::UnwrapWrapped<LineString>(parent);

  OGRPoint pt;
  int i;

  if (!args.get(0, "index", i)) return args.failed();
  if (i < 0 || i >= geom->get()->getNumPoints()) {
    Napi::Error::New(node_gdal::napi_env(), "Invalid point requested").ThrowAsJavaScriptException();
    return node_gdal::napi_env().Undefined();
  }

  geom->get()->getPoint(i, &pt);

  // New will copy the point with GDAL clone()
  return Point::New(&pt, false);
}


Napi::Value set(const Napi::CallbackInfo &info) {
  Args args(info);

  Napi::Object parent =
    node_gdal::get_private(info.This(), "parent_").As<Napi::Object>();
  LineString *geom = node_gdal::UnwrapWrapped<LineString>(parent);

  int i;
  if (!args.get(0, "index", i)) return args.failed();
  if (i < 0 || i >= geom->get()->getNumPoints()) {
    Napi::Error::New(node_gdal::napi_env(), "Point index out of range").ThrowAsJavaScriptException();
    return node_gdal::napi_env().Undefined();
  }

  int n = info.Length() - 1;

  if (n == 0) {
    Napi::Error::New(node_gdal::napi_env(), "Point must be given").ThrowAsJavaScriptException();
    return node_gdal::napi_env().Undefined();
  } else if (n == 1) {
    if (!info[1].IsObject()) {
      Napi::Error::New(node_gdal::napi_env(), "Point or object expected for second argument").ThrowAsJavaScriptException();
      return node_gdal::napi_env().Undefined();
    }
    if (node_gdal::IsInstanceOf<Point>(info[1])) {
      // set from Point object
      Point *pt = node_gdal::UnwrapWrapped<Point>(info[1].As<Napi::Object>());
      geom->get()->setPoint(i, pt->get());
    } else {
      Napi::Object obj = info[1].As<Napi::Object>();
      // set from object {x: 0, y: 5}
      double x, y;
      if (!args.from(obj, "x", x)) return args.failed();
      if (!args.from(obj, "y", y)) return args.failed();

      Napi::String z_prop_name = Napi::String::New(node_gdal::napi_env(), "z");
      if (obj.As<Napi::Object>().HasOwnProperty(z_prop_name)) {
        Napi::Value z_val = obj.As<Napi::Object>().Get(z_prop_name);
        if (!z_val.IsNumber()) {
          Napi::Error::New(node_gdal::napi_env(), "z property must be number").ThrowAsJavaScriptException();
          return node_gdal::napi_env().Undefined();
        }
        geom->get()->setPoint(i, x, y, z_val.As<Napi::Number>().DoubleValue());
      } else {
        geom->get()->setPoint(i, x, y);
      }
    }
  } else {
    // set x, y, z from numeric arguments
    if (!info[1].IsNumber()) {
      Napi::Error::New(node_gdal::napi_env(), "Number expected for second argument").ThrowAsJavaScriptException();
      return node_gdal::napi_env().Undefined();
    }
    if (!info[2].IsNumber()) {
      Napi::Error::New(node_gdal::napi_env(), "Number expected for third argument").ThrowAsJavaScriptException();
      return node_gdal::napi_env().Undefined();
    }
    if (n == 2) {
      geom->get()->setPoint(i, info[1].As<Napi::Number>().DoubleValue(), info[2].As<Napi::Number>().DoubleValue());
    } else {
      if (!info[3].IsNumber()) {
        Napi::Error::New(node_gdal::napi_env(), "Number expected for fourth argument").ThrowAsJavaScriptException();
        return node_gdal::napi_env().Undefined();
      }

      geom->get()->setPoint(
        i,
        info[1].As<Napi::Number>().DoubleValue(),
        info[2].As<Napi::Number>().DoubleValue(),
        info[3].As<Napi::Number>().DoubleValue());
    }
  }

  return node_gdal::napi_env().Undefined();
}


Napi::Value add(const Napi::CallbackInfo &info) {
  Args args(info);

  Napi::Object parent =
    node_gdal::get_private(info.This(), "parent_").As<Napi::Object>();
  LineString *geom = node_gdal::UnwrapWrapped<LineString>(parent);

  int n = info.Length();

  if (n == 0) {
    Napi::Error::New(node_gdal::napi_env(), "Point must be given").ThrowAsJavaScriptException();
    return node_gdal::napi_env().Undefined();
  } else if (n == 1) {
    if (!info[0].IsObject()) {
      Napi::Error::New(node_gdal::napi_env(), "Point, object, or array of points expected").ThrowAsJavaScriptException();
      return node_gdal::napi_env().Undefined();
    }
    if (node_gdal::IsInstanceOf<Point>(info[0])) {
      // set from Point object
      Point *pt = node_gdal::UnwrapWrapped<Point>(info[0].As<Napi::Object>());
      geom->get()->addPoint(pt->get());
    } else if (info[0].IsArray()) {
      // set from array of points
      Napi::Array array = info[0].As<Napi::Array>();
      int length = array.Length();
      for (int i = 0; i < length; i++) {
        Napi::Value element = array.As<Napi::Object>().Get(i);
        if (!element.IsObject()) {
          Napi::Error::New(node_gdal::napi_env(), "All points must be Point objects or objects").ThrowAsJavaScriptException();
          return node_gdal::napi_env().Undefined();
        }
        Napi::Object element_obj = element.As<Napi::Object>();
        if (node_gdal::IsInstanceOf<Point>(element_obj)) {
          // set from Point object
          Point *pt = node_gdal::UnwrapWrapped<Point>(element_obj);
          geom->get()->addPoint(pt->get());
        } else {
          // set from object {x: 0, y: 5}
          double x, y;
          if (!args.from(element_obj, "x", x)) return args.failed();
          if (!args.from(element_obj, "y", y)) return args.failed();

          Napi::String z_prop_name = Napi::String::New(node_gdal::napi_env(), "z");
          if (element_obj.As<Napi::Object>().HasOwnProperty(z_prop_name)) {
            Napi::Value z_val = element_obj.As<Napi::Object>().Get(z_prop_name);
            if (!z_val.IsNumber()) {
              Napi::Error::New(node_gdal::napi_env(), "z property must be number").ThrowAsJavaScriptException();
              return node_gdal::napi_env().Undefined();
            }
            geom->get()->addPoint(x, y, z_val.As<Napi::Number>().DoubleValue());
          } else {
            geom->get()->addPoint(x, y);
          }
        }
      }
    } else {
      // set from object {x: 0, y: 5}
      Napi::Object obj = info[0].As<Napi::Object>();
      double x, y;
      if (!args.from(obj, "x", x)) return args.failed();
      if (!args.from(obj, "y", y)) return args.failed();

      Napi::String z_prop_name = Napi::String::New(node_gdal::napi_env(), "z");
      if (obj.As<Napi::Object>().HasOwnProperty(z_prop_name)) {
        Napi::Value z_val = obj.As<Napi::Object>().Get(z_prop_name);
        if (!z_val.IsNumber()) {
          Napi::Error::New(node_gdal::napi_env(), "z property must be number").ThrowAsJavaScriptException();
          return node_gdal::napi_env().Undefined();
        }
        geom->get()->addPoint(x, y, z_val.As<Napi::Number>().DoubleValue());
      } else {
        geom->get()->addPoint(x, y);
      }
    }
  } else {
    // set x, y, z from numeric arguments
    if (!info[0].IsNumber()) {
      Napi::Error::New(node_gdal::napi_env(), "Number expected for first argument").ThrowAsJavaScriptException();
      return node_gdal::napi_env().Undefined();
    }
    if (!info[1].IsNumber()) {
      Napi::Error::New(node_gdal::napi_env(), "Number expected for second argument").ThrowAsJavaScriptException();
      return node_gdal::napi_env().Undefined();
    }
    if (n == 2) {
      geom->get()->addPoint(info[0].As<Napi::Number>().DoubleValue(), info[1].As<Napi::Number>().DoubleValue());
    } else {
      if (!info[2].IsNumber()) {
        Napi::Error::New(node_gdal::napi_env(), "Number expected for third argument").ThrowAsJavaScriptException();
        return node_gdal::napi_env().Undefined();
      }

      geom->get()->addPoint(
        info[0].As<Napi::Number>().DoubleValue(),
        info[1].As<Napi::Number>().DoubleValue(),
        info[2].As<Napi::Number>().DoubleValue());
    }
  }

  return node_gdal::napi_env().Undefined();
}

} // namespace

void LineStringPoints::Initialize(Napi::Object target) {
  Napi::Env env = target.Env();
  static constexpr MethodEntry methods[] = {
    method("toString", toString),
    method("count", count),
    method("get", get),
    method("set", set),
    method("add", add),
    method("reverse", reverse),
    method("resize", resize),
  };

  Napi::Function lcons = GDALDefineClass<LineStringPoints>(env, "LineStringPoints", methods);

  target.Set("LineStringPoints", lcons);

  constructor() = Napi::Persistent(lcons);
}

} // namespace node_gdal
