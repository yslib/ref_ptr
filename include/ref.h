#pragma once
#include <atomic>
#include <cassert>
#include <cstdint>
#include <mutex>
#include <type_traits>

#define USE_VIRTUAL_GETTER

template <typename Interface> class IRefCnt {
public:
  using object_type = Interface;
  using size_type = int;

  virtual size_type ref() = 0;
  virtual size_type deref() = 0;
  virtual size_type ref_count() const = 0;
  virtual size_type weak_ref() = 0;
  virtual size_type weak_deref() = 0;
  virtual size_type weak_ref_count() const = 0;
  virtual Interface *object() = 0;
  virtual ~IRefCnt() = default;
};

class IAlloc {};

class SpinLock {
private:
  std::atomic<bool> _lock{false};

public:
  void lock() {
    for (;;) {
      if (!_lock.exchange(true, std::memory_order_acquire)) {
        return;
      }
      while (_lock.load(std::memory_order_relaxed)) {
#if defined(__x86_64__) || defined(__i386__)
        __builtin_ia32_pause();
#endif
      }
    }
  }

  bool try_lock() {
    return !_lock.load(std::memory_order_acquire) &&
           !_lock.exchange(true, std::memory_order_acquire);
  }

  void unlock() { _lock.store(false, std::memory_order_release); }

  ~SpinLock() { unlock(); }
};

class EmptyLock {
public:
  void lock() {}
  bool try_lock() { return true; }
  void unlock() {}
};

template <typename Interface>
class RefCntImpl final : public IRefCnt<Interface> {
  enum class EObjectState : uint8_t { UNINITIALIZED, ALIVE, DESTROYED };
  class ObjectWrapperBase {
  public:
    using object_fptr_t = void *(*)(ObjectWrapperBase *);

    virtual void destroy() const = 0;

#ifdef USE_VIRTUAL_GETTER
    virtual Interface *object() = 0;
#else
    object_fptr_t fptr_object;
#endif
  }; // This is used to eliminate the template parameter

  template <typename ObjectType, typename Allocator>
  class ObjectWrapper final : public ObjectWrapperBase {
  public:
    using object_type = ObjectType;
    ObjectWrapper(ObjectType *obj, Allocator *allocator)
        : _obj(obj), _alloc(allocator) {}
    void destroy() const override final {
      if (_alloc) {
        _obj->~ObjectType();
        _alloc->dealloc(_obj);
      } else {
        delete _obj;
      }
    }

#ifdef USE_VIRTUAL_GETTER
    ObjectType *object() override final { return _obj; }
#endif

    ObjectType *const _obj = nullptr;
    Allocator *const _alloc = nullptr;

  private:
  };

public:
  template <typename WrapperType>
  static typename WrapperType::object_type *f_object(WrapperType *ctx) {
    return (typename WrapperType::object_type *)ctx->_obj;
  };

  using base_type = IRefCnt<Interface>;
  using size_type = typename IRefCnt<Interface>::size_type;
  template <typename ManagedObjectType, typename AllocatorType>
  void init(AllocatorType *allocator, ManagedObjectType *obj) {

    using wrapper_type = ObjectWrapper<ManagedObjectType, AllocatorType>;
    new (_object_buf) wrapper_type(obj, allocator);
    _object_state = EObjectState::ALIVE;

#ifndef USE_VIRTUAL_GETTER
    auto objWrapper = reinterpret_cast<ObjectWrapperBase *>(_object_buf);
    // replace virtual function by function pointer
    objWrapper->fptr_object =
        typename ObjectWrapperBase::object_fptr_t(f_object<wrapper_type>);
#endif
  }

  size_type ref() override final { return _cnt++; }

  size_type deref() override final {
    auto cnt = --_cnt;
    if (cnt == 0) {
      // 1. delete managed object

      std::unique_lock<Lock> lk(_mtx);
      auto objWrapper = reinterpret_cast<ObjectWrapperBase *>(_object_buf);
      _object_state = EObjectState::DESTROYED;
      auto deleteSelf = _weak_cnt.load() == 0;
      lk.unlock();
      objWrapper->destroy();
      if (deleteSelf)
        destroy();
    }
    return cnt;
  }
  size_type ref_count() const override final { return _cnt; }
  size_type weak_ref() override final { return ++_weak_cnt; }
  size_type weak_deref() override final {
    auto cnt = --_weak_cnt;
    std::unique_lock<Lock> lk(_mtx);
    if (cnt == 0 && _object_state == EObjectState::DESTROYED) {
      lk.unlock();
      destroy();
    }
    return cnt;
  }
  size_type weak_ref_count() const override final { return _weak_cnt; }

  typename IRefCnt<Interface>::object_type *object() override final {
    if (_object_state != EObjectState::ALIVE)
      return nullptr;
    std::unique_lock<Lock> lk(_mtx);
    auto cnt = ++_cnt;
    if (_object_state == EObjectState::ALIVE && cnt > 1) {
      lk.unlock();
      const auto objectWrapper =
          reinterpret_cast<ObjectWrapperBase *>(_object_buf);
#ifdef USE_VIRTUAL_GETTER
      return objectWrapper->object();
#else
      return (typename IRefCnt<Interface>::object_type
                  *)(objectWrapper->fptr_object(objectWrapper));
#endif
    }
    --_cnt;
    return nullptr;
  }

private:
  void destroy() {
    delete this;
    static_assert(sizeof(std::atomic_int) == sizeof(int));
  }

  // std::atomic_size_t _cnt = {1};
  // std::atomic_size_t _weak_cnt = {0};

  std::atomic<typename base_type::size_type> _cnt = {1};
  std::atomic<typename base_type::size_type> _weak_cnt = {0};

  static size_t constexpr BUFSIZE =
      sizeof(ObjectWrapper<Interface, IAlloc>) / sizeof(size_t);
  ;
  size_t _object_buf[BUFSIZE];

  using Lock = EmptyLock;
  Lock _mtx;
  std::atomic<EObjectState> _object_state;
  // EObjectState _object_state{EObjectState::UNINITIALIZED};

  // using Lock = std::mutex;
  // std::mutex _mtx;
  // SpinLock _mtx;
  // using Lock = SpinLock;
};

template <typename T, typename RefCntType = RefCntImpl<T>>
class RefCountedObject : public T {
public:
  using refcnt_type = RefCntType;
  using size_type = typename RefCntType::size_type;
  using base_type = typename RefCntType::object_type;
  static_assert(std::is_same_v<T, base_type>);
  RefCountedObject(refcnt_type *cnt) { _ref_cnt = cnt; }

  RefCountedObject(const RefCountedObject &) = delete;
  RefCountedObject &operator=(const RefCountedObject &) = delete;

  RefCountedObject(RefCountedObject &&) noexcept = delete;
  RefCountedObject &operator=(RefCountedObject &&) noexcept = delete;

  size_type ref() { return this->_ref_cnt->ref(); };
  size_type deref() { return this->_ref_cnt->deref(); }
  size_type ref_count() const { return this->_ref_cnt->ref_count(); }
  size_type weak_ref() { return this->_ref_cnt->weak_ref(); }
  size_type weak_deref() { return this->_ref_cnt->weak_deref(); }
  size_type weak_ref_count() const { return this->_ref_cnt->weak_ref_count(); }

  refcnt_type *cnt() const { return _ref_cnt; }
  base_type *object() {
    return static_cast<base_type *>(this->_ref_cnt->object());
  }

  void operator delete(void *ptr) { delete[] reinterpret_cast<uint8_t *>(ptr); }
  template <typename ObjectAllocatorType>
  void operator delete(void *ptr, ObjectAllocatorType &allocator,
                       const char *dbgDescription, const char *dbgFileName,
                       const uint32_t dbgLineNumber) {
    return allocator.dealloc(ptr);
  }

  void *operator new(size_t size) { return new uint8_t[size]; }

  template <typename ObjectAllocatorType>
  void *operator new(size_t size, ObjectAllocatorType &allocator,
                     const char *dbgDescription, const char *dbgFileName,
                     const uint32_t dbgLineNumber) {
    return allocator.alloc(size);
  }

private:
  refcnt_type *_ref_cnt{nullptr};
};

template <typename T> class obs_ptr;

template <typename T> class ref_ptr {
public:
  using element_type = T;
  T *obj{nullptr};

  constexpr ref_ptr() noexcept = default;

  constexpr ref_ptr(std::nullptr_t) noexcept { obj = nullptr; }

  template <typename U> explicit ref_ptr(U *obj) : obj(obj) { assert(obj); }

  template <typename U>
  explicit ref_ptr(const obs_ptr<U> &r); // defined outside

  ref_ptr(const ref_ptr &r) noexcept : obj(r.obj) {
    if (obj)
      obj->cnt()->ref();
  }

  template <typename U> ref_ptr(const ref_ptr<U> &r) noexcept : obj(r.obj) {
    if (obj) {
      obj->cnt()->ref();
    }
  }

  ref_ptr(ref_ptr &&o) noexcept {
    obj = o.obj;
    o.obj = nullptr;
  }

  template <typename U> ref_ptr(ref_ptr<U> &&o) noexcept {
    obj = o.obj;
    o.obj = nullptr;
  }

  ref_ptr &operator=(const ref_ptr &r) noexcept {
    reset(r.get());
    return *this;
  }

  template <typename U> ref_ptr &operator=(const ref_ptr<U> &r) noexcept {
    reset(r.get());
    return *this;
  }

  ref_ptr &operator=(ref_ptr &&o) noexcept {
    reset();
    obj = o.obj;
    o.obj = nullptr;
    return *this;
  }

  template <typename U> ref_ptr &operator=(ref_ptr<U> &&o) noexcept {
    reset();
    obj = o.obj;
    o.obj = nullptr;
    return *this;
  }

  explicit operator bool() const noexcept { return get() != nullptr; }

  long use_count() const noexcept {
    return get() ? obj->cnt()->ref_count() : 0;
  }

  T *operator->() const noexcept { return get(); }

  T &operator*() const noexcept { return *obj; }

  element_type *get() const noexcept { return obj; }

  template <typename U> void reset(U *p) noexcept {
    if (obj) {
      obj->cnt()->deref();
    }
    obj = p;
    if (obj) {
      obj->cnt()->ref();
    }
  }

  void reset() noexcept {
    if (obj) {
      obj->cnt()->deref();
      obj = nullptr;
    }
  }

  ~ref_ptr() { reset(); }
};

template <typename T>
inline bool operator==(const ref_ptr<T> &lhs, const ref_ptr<T> &rhs) {
  return lhs.get() == rhs.get();
} // ref_ptr == ref_ptr

template <typename T>
inline bool operator!=(const ref_ptr<T> &lhs, const ref_ptr<T> &rhs) {
  return !(lhs == rhs);
} // ref_ptr != ref_ptr

template <typename T>
inline bool operator==(const ref_ptr<T> &lhs, std::nullptr_t) {
  return lhs.get() == nullptr;
} // ref_ptr == nullptr

template <typename T>
inline bool operator!=(const ref_ptr<T> &lhs, std::nullptr_t) {
  return !(lhs == nullptr);
} // ref_ptr != nullptr

template <typename T>
inline bool operator==(std::nullptr_t, const ref_ptr<T> &rhs) {
  return nullptr == rhs.get();
} // nullptr == ref_ptr

template <typename T>
inline bool operator!=(std::nullptr_t, const ref_ptr<T> &rhs) {
  return !(nullptr == rhs);
} // nullptr != ref_ptr

template <typename T>
inline bool operator==(const ref_ptr<T> &lhs, const T *rhs) {
  return lhs.get() == rhs;
} // ref_ptr == T*
template <typename T>
inline bool operator!=(const ref_ptr<T> &lhs, const T *rhs) {
  return !(lhs == rhs);
} // ref_ptr != T*

template <typename T>
inline bool operator==(const T *lhs, const ref_ptr<T> &rhs) {
  return lhs == rhs.get();
} // T* == ref_ptr

template <typename T>
inline bool operator!=(const T *lhs, const ref_ptr<T> &rhs) {
  return !(lhs == rhs);
} // T* != ref_ptr

template <typename T> class obs_ptr {
public:
  typename T::refcnt_type *cnt{nullptr};
  using element_type = T;

  template <typename U> obs_ptr(const ref_ptr<U> &ref) noexcept {
    if (ref.obj) {
      cnt = ref.obj->cnt();
      cnt->weak_ref();
    }
  }

  obs_ptr(const obs_ptr &o) noexcept {
    if (o.cnt) {
      cnt = o.cnt;
      cnt->weak_ref();
    }
  }

  template <typename U> obs_ptr(const obs_ptr<U> &o) noexcept {
    if (o.cnt) {
      cnt = o.cnt;
      cnt->weak_ref();
    }
  }

  obs_ptr(obs_ptr &&o) noexcept {
    cnt = o.cnt;
    o.cnt = nullptr;
  }

  template <typename U> obs_ptr(obs_ptr<U> &&o) noexcept {
    cnt = o.cnt;
    o.cnt = nullptr;
  }

  template <typename U> obs_ptr &operator==(const ref_ptr<U> &r) noexcept {
    reset();
    if (r) {
      cnt = r.get().cnt();
      cnt->weak_ref();
    }
  }

  obs_ptr &operator==(const obs_ptr &o) noexcept {
    reset();
    if (o.cnt) {
      cnt = o.cnt;
      cnt->weak_ref();
    }
  }

  template <typename U> obs_ptr &operator==(const obs_ptr<U> &o) noexcept {
    reset();
    if (o.cnt) {
      cnt = o.cnt;
      cnt->weak_ref();
    }
  }

  obs_ptr &operator=(obs_ptr &&o) noexcept {
    reset();
    if (o.cnt) {
      cnt = o.cnt;
      o.cnt = nullptr;
    }
  }

  template <typename U> obs_ptr &operator=(obs_ptr<U> &&o) noexcept {
    reset();
    if (o.cnt) {
      cnt = o.cnt;
      o.cnt = nullptr;
    }
  }

  bool expired() const noexcept { return cnt && cnt->ref_count() > 0; }

  long use_count() const noexcept { return cnt ? cnt->ref_count() : 0; }

  ref_ptr<element_type> lock() const noexcept {
    if (cnt) {
      auto pp = cnt->object();
      return ref_ptr<T>(static_cast<T *>(pp));
    }
    return nullptr;
  }

  void reset() noexcept {
    if (cnt) {
      cnt->weak_deref();
      cnt = nullptr;
    }
  }

  ~obs_ptr() { reset(); }
};

template <typename T>
template <typename U>
ref_ptr<T>::ref_ptr(const obs_ptr<U> &r) {
  *this = r.lock();
}

template <typename ObjectType, typename Interface, typename AllocatorType,
          typename RefCounterType = RefCntImpl<Interface>, typename... Args>
inline ObjectType *vm_make(AllocatorType *alloc, Args &&...args) {
  auto refcnt = new RefCounterType();
  ObjectType *obj = nullptr;
  if (alloc) {
    obj = new (*alloc, 0, 0, 0) ObjectType(refcnt, std::forward<Args>(args)...);
  } else {
    obj = new ObjectType(refcnt, std::forward<Args>(args)...);
  }
  refcnt->init(alloc, obj);
  return obj;
}

template <typename ObjectType, typename Interface, typename AllocatorType,
          typename RefCounterType = RefCntImpl<Interface>, typename... Args>
inline ref_ptr<ObjectType> vm_make_ptr(AllocatorType *alloc, Args &&...args) {
  return ref_ptr<ObjectType>(
      vm_make<ObjectType, Interface, AllocatorType, RefCounterType>(
          alloc, std::forward<Args>(args)...));
}
