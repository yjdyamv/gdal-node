#ifndef NODE_GDAL_GDAL_ERROR_HPP
#define NODE_GDAL_GDAL_ERROR_HPP

#include "../napi-wrapper.h"

#include <exception>
#include <string>

namespace node_gdal {

/**
 * A failure of an operation.
 *
 * GDAL reports its failures with an error code and a message in a thread-local
 * buffer, and the addon used to pass them up as bare `const char *` and to build
 * the JS error at whichever call site happened to notice. Nothing but the text
 * survived that trip, so the only errors which could have a class of their own
 * (a `TypeError`, a `RangeError`, the `AbortError` of an interrupted operation)
 * were the ones built directly in the JS-facing code.
 *
 * A GDALError carries the class and the code of the error it should become along
 * with the message, so a failure raised a few layers down still arrives in JS as
 * the right kind of error. It is turned into a JS error in one place, see
 * toJS(), which is also the only place which decides which `Napi::Error`
 * subclass to build.
 *
 * @class GDALError
 */
class GDALError : public std::exception {
    public:
  explicit GDALError(std::string message, std::string name = "Error", std::string code = std::string())
    // An empty message would be taken for a success by node-addon-api, which
    // decides whether an asynchronous worker failed by the length of its error
    : message_(message.empty() ? "Operation failed" : std::move(message)), name_(std::move(name)),
      code_(std::move(code)) {
  }

  // The failure which GDAL or OGR last reported
  static GDALError fromGDAL();

  // The operation was interrupted by its AbortSignal, the error has the same
  // shape as the AbortError of the Node.js core (node:internal/errors)
  static GDALError aborted() {
    return GDALError("The operation was aborted", "AbortError", "ABORT_ERR");
  }

  const char *what() const noexcept override {
    return message_.c_str();
  }

  const std::string &message() const {
    return message_;
  }

  const std::string &name() const {
    return name_;
  }

  const std::string &code() const {
    return code_;
  }

    private:
  std::string message_;
  std::string name_;
  std::string code_;
};

// The JS error for `error`: the subclass selected by its name, with the name and
// the code set when they are not the defaults. This is the single place which
// builds an error out of a C++ failure.
Napi::Error toJS(Napi::Env env, const GDALError &error);

// Throws the JS error of `error`. `name` and `code` are kept, so a `TypeError`
// raised while checking an argument is still a `TypeError` when it reaches the
// caller of an asynchronous method. An exception which is already pending is
// left alone - nothing can be called into JS while one is.
void throwError(Napi::Env env, const GDALError &error);

// The subclasses are what the `NODE_THROW_*` macros and the argument checks
// raise, so that the class of the error is decided where it is known
Napi::Value throwTypeError(Napi::Env env, const std::string &message);
Napi::Value throwRangeError(Napi::Env env, const std::string &message);

} // namespace node_gdal

#endif
