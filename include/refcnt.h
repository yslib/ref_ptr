#include <atomic>
#include <cassert>
#include <cstdint>
#include <mutex>

class IObject;

class IRefCnt {
public:
  virtual size_t ref() = 0;
  virtual size_t deref() = 0;
  virtual size_t ref_count() const = 0;
  virtual size_t weak_ref() = 0;
  virtual size_t weak_deref() = 0;
  virtual size_t weak_ref_count() const = 0;
  virtual IObject *object() = 0;
  virtual ~IRefCnt() = default;
};

class IAlloc {
public:
  virtual void dealloc(void *ptr) = 0;
  virtual void *alloc(size_t size) = 0;
  virtual ~IAlloc() = default;
};

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
        __builtin_ia32_pause();
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

class AllocImpl : public IAlloc {
public:
  void dealloc(void *ptr) override { return; }
  void *alloc(size_t size) override { return nullptr; }
};

template <typename ManagedObjectType, typename AllocatorType>
class RefCntImpl final : public IRefCnt {
  template <typename ObjectType, typename Allocator> friend class VNew;

  enum class EObjectState : uint8_t { UNINITIALIZED, ALIVE, DESTROYED };
  template <typename ObjectType, typename Allocator> class ObjectWrapper {
  public:
    ObjectWrapper(ObjectType *obj, Allocator *allocator)
        : m_obj(obj), m_allocator(allocator) {}
    void destroy() const {
      if (m_allocator) {
        m_obj->~ObjectType();
        m_allocator->dealloc(m_obj);
      } else {
        delete m_obj;
      }
    }

    ObjectType *object() { return m_obj; }

  private:
    ObjectType *const m_obj = nullptr;
    Allocator *const m_allocator = nullptr;
  };

public:
  void init(AllocatorType *allocator, ManagedObjectType *obj) {
    new (m_objectBuffer)
        ObjectWrapper<ManagedObjectType, AllocatorType>(obj, allocator);
    m_objectState = EObjectState::ALIVE;
  }

  size_t ref() { return m_cnt++; }

  size_t deref() {
    auto cnt = --m_cnt;
    if (cnt == 0) {
      // 1. delete managed object

      std::unique_lock<Lock> lk(m_mtx);
      auto objWrapper =
          reinterpret_cast<ObjectWrapper<ManagedObjectType, AllocatorType> *>(
              m_objectBuffer);
      m_objectState = EObjectState::DESTROYED;
      auto deleteSelf = m_weakCnt.load() == 0;
      lk.unlock();
      objWrapper->destroy();
      if (deleteSelf)
        destroy();
    }
    return cnt;
  }
  size_t ref_count() const { return m_cnt; }

  size_t weak_ref() { return ++m_weakCnt; }
  size_t weak_deref() {
    auto cnt = --m_weakCnt;
    std::unique_lock<Lock> lk(m_mtx);
    if (cnt == 0 && m_objectState == EObjectState::DESTROYED) {
      lk.unlock();
      destroy();
    }
    return cnt;
  }
  size_t weak_ref_count() const { return m_weakCnt; }

  IObject *object() {
    if (m_objectState != EObjectState::ALIVE)
      return nullptr;
    std::unique_lock<Lock> lk(m_mtx);
    auto cnt = ++m_cnt;
    if (m_objectState == EObjectState::ALIVE && cnt > 1) {
      lk.unlock();
      auto objectWrapper =
          reinterpret_cast<ObjectWrapper<ManagedObjectType, AllocatorType> *>(
              m_objectBuffer);
      return objectWrapper->object();
    }
    --m_cnt;
    return nullptr;
  }

private:
  void destroy() {
    delete this;
    static_assert(sizeof(std::atomic_int) == sizeof(int));
  }

  // std::atomic_size_t m_cnt = {1};
  // std::atomic_size_t m_weakCnt = {0};

  std::atomic_uint m_cnt = {1};
  std::atomic_uint m_weakCnt = {0};

  static size_t constexpr BUFSIZE =
      sizeof(ObjectWrapper<ManagedObjectType, AllocatorType>) / sizeof(size_t);
  ;
  size_t m_objectBuffer[BUFSIZE];

  using Lock = EmptyLock;
  Lock m_mtx;
  std::atomic<EObjectState> m_objectState;
  // EObjectState m_objectState{EObjectState::UNINITIALIZED};

  // using Lock = std::mutex;
  // std::mutex m_mtx;
  // SpinLock m_mtx;
  // using Lock = SpinLock;
};

class IObject {
public:
  using CntType = RefCntImpl<IObject, AllocImpl>; // devirtualize
  // using CntType = IRefCnt;

  IObject(CntType *cnt) : _cnt(cnt) {}
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

  CntType *cnt() const { return _cnt; }

private:
  CntType *_cnt;
};

template <typename T> class ref_ptr {
public:
  T *obj{nullptr};
  ref_ptr(IObject *obj) : obj(obj) {
    assert(obj);
  } // assume obj is not null and ref is called
  ref_ptr(const ref_ptr &r) : obj(r.obj) { obj->cnt()->ref(); }
  ref_ptr(ref_ptr &&o) noexcept {
    obj = o.obj;
    o.obj = nullptr;
  }
  ref_ptr &operator=(ref_ptr &&o) noexcept {
    obj = o.obj;
    o.obj = nullptr;
  }
  operator bool() const { return obj != nullptr; }
  ~ref_ptr() {
    if (obj)
      obj->cnt()->deref();
  }
};

template <typename T> class obs_ptr {
public:
  typename T::CntType *cnt{nullptr};
  obs_ptr(const ref_ptr<T> &ref) : cnt(ref.obj->cnt()) { cnt->weak_ref(); }
  obs_ptr(obs_ptr &&o) noexcept {
    cnt = o.cnt;
    o.cnt = nullptr;
  }
  obs_ptr &operator=(obs_ptr &&o) noexcept {
    cnt = o.cnt;
    o.cnt = nullptr;
  }
  ref_ptr<T> lock() const noexcept {
    if (cnt)
      return ref_ptr<T>(cnt->object());
    return nullptr;
  }
  ~obs_ptr() {
    if (cnt) {
      cnt->weak_deref();
    }
  }
};

template <typename Base, typename RefCounterType>
class RefCountedObject : public Base {

  using RefCntType = RefCounterType;
  using NumberType = RefCounterType::NumberType;

  // Base(RefCntType *c) : _refCnt(c) {}

  RefCountedObject() {
    auto refcnt = new RefCounterType();
    refcnt->init(nullptr, this);
  }
  NumberType ref() { return this->_refCnt->ref(); };
  NumberType deref() { return this->_refCnt->deref(); }
  NumberType ref_count() const { return this->_refCnt->ref_count(); }
  NumberType weak_ref() { return this->_refCnt->weak_ref(); }
  NumberType weak_deref() { return this->_refCnt->weak_deref(); }
  NumberType weak_ref_count() const { return this->_refCnt->weak_ref_count(); }
  RefCntType *cnt() const { return _refCnt; }
  Base *object() { return static_cast<Base *>(this->_refCnt->okjbject()); }

protected: // don't allow new the object in ordinary way
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
  RefCntType *_refCnt;
};

template <typename ObjectType, typename AllocatorType,
          typename RefCounterType = RefCntImpl<ObjectType, AllocatorType>,
          typename... Args>
inline ObjectType *NEW(AllocatorType *alloc, Args &&...args) {
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

template <typename ObjectType, typename AllocatorType, typename RefCounterType,
          typename... Args>
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

template <typename ObjectType, typename AllocatorType, typename RefCounterType,
          typename... Args>
inline ref_ptr<ObjectType> vm_make_ptr(AllocatorType *alloc, Args &&...args) {
  return ref_ptr<ObjectType>(vm_make(alloc, std::forward<Args>(args)...));
}

// user define

template <typename T, typename... Args> inline T *make_ptr(Args &&...args) {
  return vm_make<T, AllocImpl, RefCntImpl<T, AllocImpl>>(
      nullptr, std::forward<Args>(args)...);
}

template <typename T, typename... Args>
inline ref_ptr<T> make_ref(Args &&...args) {
  return vm_make_ptr<T, AllocImpl, RefCntImpl<T, AllocImpl>>(
      nullptr, std::forward<Args>(args)...);
}
