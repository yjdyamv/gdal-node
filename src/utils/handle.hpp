#ifndef NODE_GDAL_HANDLE_HPP
#define NODE_GDAL_HANDLE_HPP

namespace node_gdal {

/**
 * The pointer a wrapper holds to its GDAL object, together with the deleter
 * which frees it - or no deleter at all, when the wrapper only borrows the
 * object.
 *
 * The ownership used to be a `bool owned_` next to the raw pointer, set in one
 * place and read in another. Forgetting to set it is how the `FieldDefn` leak
 * and the algebra band leak happened, and setting it twice is how a wrapper
 * could free an object its parent still owned. Here the deleter is fixed where
 * the handle is created - in a factory, which knows whether it copied the
 * object or borrowed it from its parent - and there is no way to change it
 * afterwards.
 *
 * The handle converts to the raw pointer, so the code which uses the object
 * reads as it did with a bare pointer. Assigning a raw pointer is deliberately
 * not allowed: the ownership has to be spelled out, which is what the deleter
 * is for.
 */
template <typename T> class Handle {
    public:
  using Deleter = void (*)(void *);

  Handle() = default;
  Handle(T *ptr, Deleter deleter) : ptr_(ptr), deleter_(ptr == nullptr ? nullptr : deleter) {
  }

  // The object belongs to the parent and has to outlive the wrapper
  static Handle borrowed(T *ptr) {
    return Handle(ptr, nullptr);
  }

  // The object belongs to the wrapper, which frees it with `deleter`
  static Handle owned(T *ptr, Deleter deleter) {
    return Handle(ptr, deleter);
  }

  Handle(Handle &&other) noexcept : ptr_(other.ptr_), deleter_(other.deleter_) {
    other.forget();
  }

  Handle &operator=(Handle &&other) noexcept {
    if (this != &other) {
      reset();
      ptr_ = other.ptr_;
      deleter_ = other.deleter_;
      other.forget();
    }
    return *this;
  }

  Handle(const Handle &) = delete;
  Handle &operator=(const Handle &) = delete;

  ~Handle() {
    reset();
  }

  // Frees the object, if this handle owns it
  void reset() {
    T *ptr = ptr_;
    Deleter deleter = deleter_;
    forget();
    if (ptr != nullptr && deleter != nullptr) { deleter(ptr); }
  }

  // Forgets the object without freeing it, for the objects which another owner
  // frees (the object store, the dataset the band belongs to)
  T *release() {
    T *ptr = ptr_;
    forget();
    return ptr;
  }

  T *get() const {
    return ptr_;
  }

  T *operator->() const {
    return ptr_;
  }

  operator T *() const {
    return ptr_;
  }

  explicit operator bool() const {
    return ptr_ != nullptr;
  }

  // True when the wrapper frees the object itself
  bool owned() const {
    return ptr_ != nullptr && deleter_ != nullptr;
  }

    private:
  void forget() {
    ptr_ = nullptr;
    deleter_ = nullptr;
  }

  T *ptr_ = nullptr;
  Deleter deleter_ = nullptr;
};

// The raw pointer behind a member, for the code which does not care whether
// the class holds a Handle or a bare pointer
template <typename T> inline T *ptr_of(T *ptr) {
  return ptr;
}

template <typename T> inline T *ptr_of(const Handle<T> &handle) {
  return handle.get();
}

} // namespace node_gdal

#endif
