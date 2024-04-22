#pragma once
#include <atomic>
#include <cassert>
#include <cstdint>
#include <mutex>
#include <type_traits>

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

template <typename Interface>
class RefCntImpl final : public IRefCnt<Interface> {
  enum class EObjectState : uint8_t { UNINITIALIZED, ALIVE, DESTROYED };
  class ObjectWrapperBase {
  public:
    virtual void destroy() const = 0;
    virtual Interface *object() = 0;
  }; // This is used to eliminate the template parameter

  template <typename ObjectType, typename Allocator>
  class ObjectWrapper final : public ObjectWrapperBase {
  public:
    ObjectWrapper(ObjectType *obj, Allocator *allocator)
        : m_obj(obj), m_allocator(allocator) {}
    void destroy() const override final {
      if (m_allocator) {
        m_obj->~ObjectType();
        m_allocator->dealloc(m_obj);
      } else {
        delete m_obj;
      }
    }

    ObjectType *object() override final { return m_obj; }

  private:
    ObjectType *const m_obj = nullptr;
    Allocator *const m_allocator = nullptr;
  };

public:
  using base_type = IRefCnt<Interface>;
  template <typename ManagedObjectType, typename AllocatorType>
  void init(AllocatorType *allocator, ManagedObjectType *obj) {
    new (m_objectBuffer)
        ObjectWrapper<ManagedObjectType, AllocatorType>(obj, allocator);
    m_objectState = EObjectState::ALIVE;
  }

  base_type::size_type ref() override final { return m_cnt++; }

  base_type::size_type deref() override final {
    auto cnt = --m_cnt;
    if (cnt == 0) {
      // 1. delete managed object

      std::unique_lock<Lock> lk(m_mtx);
      auto objWrapper = reinterpret_cast<ObjectWrapperBase *>(m_objectBuffer);
      m_objectState = EObjectState::DESTROYED;
      auto deleteSelf = m_weakCnt.load() == 0;
      lk.unlock();
      objWrapper->destroy();
      if (deleteSelf)
        destroy();
    }
    return cnt;
  }
  base_type::size_type ref_count() const override final { return m_cnt; }
  base_type::size_type weak_ref() override final { return ++m_weakCnt; }
  base_type::size_type weak_deref() override final {
    auto cnt = --m_weakCnt;
    std::unique_lock<Lock> lk(m_mtx);
    if (cnt == 0 && m_objectState == EObjectState::DESTROYED) {
      lk.unlock();
      destroy();
    }
    return cnt;
  }
  base_type::size_type weak_ref_count() const override final {
    return m_weakCnt;
  }

  IRefCnt<Interface>::object_type *object() override final {
    if (m_objectState != EObjectState::ALIVE)
      return nullptr;
    std::unique_lock<Lock> lk(m_mtx);
    auto cnt = ++m_cnt;
    if (m_objectState == EObjectState::ALIVE && cnt > 1) {
      lk.unlock();
      auto objectWrapper =
          reinterpret_cast<ObjectWrapperBase *>(m_objectBuffer);
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

  std::atomic<typename base_type::size_type> m_cnt = {1};
  std::atomic<typename base_type::size_type> m_weakCnt = {0};

  static size_t constexpr BUFSIZE =
      sizeof(ObjectWrapper<Interface, IAlloc>) / sizeof(size_t);
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

template <typename T, typename RefCntType = RefCntImpl<T>>
class RefCountedObject : public T {
public:
  using refcnt_type = RefCntType;
  using size_type = RefCntType::size_type;
  using base_type = RefCntType::object_type;
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

template <typename T> class ref_ptr {
public:
  T *obj{nullptr};
  ref_ptr(T *obj) : obj(obj) {
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
  typename T::refcnt_type *cnt{nullptr};
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
    if (cnt) {
      auto pp = cnt->object();
      return ref_ptr<T>(static_cast<T *>(pp));
    }
    return nullptr;
  }
  ~obs_ptr() {
    if (cnt) {
      cnt->weak_deref();
    }
  }
};

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
