#include "../include/ref.h"
#include <iostream>

class AllocImpl {
public:
  void dealloc(void *ptr) { return; }
  void *alloc(size_t size) { return nullptr; }
};

// 1. define your base class
class IObject {
  virtual void foo() = 0;
};

// 2. place the default-implemented reference counter into your base class
class CountedAbstractObject : public RefCountedObject<IObject> {
public:
  //  define the constructor for  initiating forward ref counter object to base
  //  class.
  CountedAbstractObject(IRefCnt<IObject> *cnt)
      : RefCountedObject<IObject>(static_cast<refcnt_type *>(cnt)) {}
};

// 3. Derive your class as normal, AbstractObject is still the base class of
// DerivedObject
class DerivedObject : public CountedAbstractObject {

public:
  DerivedObject(refcnt_type *cnt) : CountedAbstractObject(cnt) {}
  void foo() override { std::cout << "Foo\n"; }
};

// Optional: define helper function for allocating the object
template <typename T, typename... Args> inline T *make_ptr(Args &&...args) {
  return vm_make<T, IObject, AllocImpl>(nullptr, std::forward<Args>(args)...);
}

template <typename T, typename... Args>
inline ref_ptr<T> make_ref(Args &&...args) {
  return make_ptr<T>(std::forward<Args>(args)...);
}

inline void example_test() { auto a = make_ptr<DerivedObject>(); }
