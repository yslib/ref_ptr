#include "../example/example.h"
#include "../utils/task.h"
#include "../utils/thread_pool.h"
#include "../utils/timer.h"

#include <random>

#include <gtest/gtest.h>

struct TestAlloc {
  std::atomic<size_t> allocCount{0};
  void *alloc(size_t size) {
    allocCount++;
    return ::malloc(size);
  }
  void dealloc(void *ptr) {
    allocCount--;
    ::free(ptr);
  }
};

class TestObject : public CountedAbstractObject {
public:
  int &flag;
  TestObject(refcnt_type *cnt, int &flag)
      : CountedAbstractObject(cnt), flag(flag) {}
  void foo() override { std::cout << "TestObject::Foo\n"; }
  ~TestObject() { flag = 0; }
};

using Op = int (*)(int testId, void *userData);

enum TestOp {
  op_ref = 0,
  op_deref = 1,
  op_weak_ref = 2,
  op_weak_deref = 3,
};

int TestOp_ref(int test, void *u) { return 0; }
int TestOp_deref(int test, void *u) { return 0; }
int TestOp_weak_ref(int test, void *u) { return 0; }
int TestOp_weak_deref(int test, void *u) { return 0; }

class TestObjectB {
  int a;
};

#define D(e)                                                                   \
  { TestOp::op_##e, TestOp_##e }

struct OpMap {
  TestOp testOp;
  Op func{nullptr};
} s_map[] = {D(ref), D(deref), D(weak_ref), D(weak_deref)};

TEST(Test, BasicCounter) {
  int flag = 1;
  auto ptr = make_ptr<TestObject>(flag);
  auto cnt = ptr->cnt();

  constexpr int testId = 0;

  std::vector<TestOp> ops;

  for (const auto &op : ops) {
    ASSERT_EQ(s_map[op].func(testId, nullptr), 1);
  }

  ASSERT_EQ(ptr->ref_count(), 1);
  ASSERT_EQ(flag, 1);
  ASSERT_EQ(ptr->weak_ref_count(), 0);
  ASSERT_EQ(flag, 1);
  ASSERT_EQ(ptr->deref(), 0);
  ASSERT_EQ(flag, 0);
}

TEST(Test, ref_ptr) {
  int flag1 = 1;
  int flag2 = 1;
  // constructors
  {
    ref_ptr<TestObject> ptr1;
    ref_ptr<TestObject> ptr2 = nullptr;
    ref_ptr<TestObject> ptr3 = make_ref<TestObject>(flag1);

    auto ptr4_raw = make_ptr<TestObject>(flag2);
    ref_ptr<TestObject> ptr4{ptr4_raw};
    ref_ptr<TestObject> ptr5 = ptr4;

    // get
    ASSERT_EQ(ptr1.get(), nullptr);
    ASSERT_EQ(ptr4.get(), ptr4_raw);

    ASSERT_TRUE(ptr1 == nullptr); // ref_ptr == nullptr
    ASSERT_TRUE(nullptr == ptr1); // nullptr == ref_ptr

    ASSERT_FALSE(ptr1 != nullptr); // ref_ptr != nullptr
    ASSERT_FALSE(nullptr != ptr1); // nullptr != ref_ptr

    ASSERT_TRUE(ptr4 == ptr5);  // ref_ptr == ref_ptr
    ASSERT_FALSE(ptr4 != ptr5); // ref_ptr != ref_ptr

    ASSERT_TRUE(ptr4 == ptr4_raw);  // ref_ptr == raw pointer
    ASSERT_FALSE(ptr4 != ptr4_raw); // ref_ptr != raw pointer

    ASSERT_TRUE(ptr4_raw == ptr4);  // raw pointer == ref_ptr
    ASSERT_FALSE(ptr4_raw != ptr4); // raw pointer != ref_ptr

    // move
    ASSERT_TRUE(ptr3 != nullptr);
    auto ptr3_raw = ptr3.get();
    auto p = std::move(ptr3);
    ASSERT_TRUE(ptr3 == nullptr);
    ASSERT_TRUE(p == ptr3_raw);

    ptr3 = p;
    ASSERT_TRUE(ptr3 != nullptr);
    ptr3_raw = ptr3.get();
    p = std::move(ptr3);
    ASSERT_TRUE(ptr3 == nullptr);
    ASSERT_TRUE(p == ptr3_raw);

    // reset
    ASSERT_TRUE(ptr4 == ptr4_raw);
    ptr4.reset();
    ASSERT_TRUE(ptr4 == nullptr);
  }

  ASSERT_EQ(flag1, 0);
  ASSERT_EQ(flag2, 0);
}

constexpr auto NUM = 10;

struct TestData {
  thread_pool pool{NUM};
  std::vector<std::vector<int>> tasks_ops{NUM};
  TestData() {
    unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();
    std::default_random_engine generator(seed);
    std::uniform_int_distribution<int> distribution(0, 5);
    for (auto &ops : tasks_ops) {
      ops.reserve(OPS_NUM);
      for (int i = 0; i < OPS_NUM; i++) {
        ops.push_back(distribution(generator));
      }
    }
  }
};

TEST(Test, multi_thread_memory_leak) {
  TestAlloc alloc;
  int flag = 1;

  {
    TestData data;
    auto p = ref_ptr<TestObject>(
        vm_make<TestObject, IObject, TestAlloc>(&alloc, flag));
    for (auto task_id = 0; task_id < NUM; task_id++) {
      data.pool.append_task(
          Task<ref_ptr<TestObject>, obs_ptr<TestObject>>(data.tasks_ops, p),
          task_id);
    }
    data.pool.wait();
  }
  ASSERT_EQ(alloc.allocCount.load(), 0);
}
