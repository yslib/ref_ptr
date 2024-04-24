#include "../example/example.h"
#include "../utils/thread_pool.h"
#include "../utils/timer.h"

#include <gtest/gtest.h>

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
  op_ref,
  op_deref,
  op_weak_ref,
  op_weak_deref,
};

int TestOp_ref(int test, void *u) { return 0; }
int TestOp_deref(int test, void *u) { return 0; }
int TestOp_weak_ref(int test, void *u) { return 0; }
int TestOp_weak_deref(int test, void *u) { return 0; }

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

TEST(Test, single_thread_memory_leak) {}
TEST(Test, multi_thread_memory_leak) {}

TEST(Test, weak_ptr_operator) {}
TEST(Test, ref_ptr_operator) {}
