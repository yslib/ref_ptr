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
  using refcnt_type = refcnt_type;
  int &flag;
  TestObject(refcnt_type *cnt, int &flag)
      : CountedAbstractObject(cnt), flag(flag) {}
  void foo() override { std::cout << "TestObject::Foo\n"; }
  ~TestObject() { flag = 0; }
};

class DerivedTestObject : public TestObject {
public:
  DerivedTestObject(refcnt_type *cnt, int &flag) : TestObject(cnt, flag) {}
  void foo() override { std::cout << "DerivedTestObject::Foo\n"; }
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

void test_ctor_nullptr(ref_ptr<TestObject> ptr) {
  ASSERT_EQ(ptr.get(), nullptr);
  ASSERT_EQ(ptr, nullptr);
  ASSERT_EQ(nullptr, ptr);
  ASSERT_FALSE(ptr);
  ASSERT_FALSE(bool(ptr));
  ASSERT_EQ(ptr.get(), nullptr);
}

TEST(Test, ref_ptr_ctor) {
  test_ctor_nullptr(nullptr);               // explict ref_ptr(std::nullptr_t);
  test_ctor_nullptr(ref_ptr<TestObject>()); // constexpr ref_ptr();

  TestObject *raw_ptr = nullptr;
  test_ctor_nullptr(ref_ptr<TestObject>(raw_ptr)); // explict ref_ptr(T *ptr);
}

TEST(Test, ref_ptr_copy_constructor) {
  int flag = 1;
  auto ptr = make_ref<TestObject>(flag);
  auto ptr2(ptr);
  ASSERT_EQ(ptr, ptr2);
  ASSERT_EQ(ptr.get(), ptr2.get());
  ASSERT_EQ(ptr->ref_count(), 2);
  ASSERT_EQ(ptr2->ref_count(), 2);
}

TEST(Test, ref_ptr_copy_assignment) {
  int flag = 1;
  auto ptr = make_ref<TestObject>(flag);
  auto ptr2 = ptr;
  ASSERT_EQ(ptr, ptr2);
  ASSERT_EQ(ptr.get(), ptr2.get());
  ASSERT_EQ(ptr->ref_count(), 2);
  ASSERT_EQ(ptr2->ref_count(), 2);
}

TEST(Test, ref_ptr_move_constructor) {
  int flag = 1;
  auto ptr = make_ref<TestObject>(flag);
  auto ptr2(std::move(ptr));
  ASSERT_EQ(ptr, nullptr);
  ASSERT_EQ(ptr2->ref_count(), 1);
  ASSERT_EQ(flag, 1);
}

TEST(Test, ref_ptr_move_assignment) {
  int flag = 1;
  auto ptr = make_ref<TestObject>(flag);
  auto ptr2 = std::move(ptr);
  ASSERT_EQ(ptr, nullptr);
  ASSERT_EQ(ptr2->ref_count(), 1);
}

TEST(Test, ref_ptr_reset) {
  int flag = 1;
  auto ptr = make_ref<TestObject>(flag);
  ASSERT_EQ(ptr->ref_count(), 1);
  ptr.reset();
  ASSERT_EQ(ptr, nullptr);
  ASSERT_EQ(flag, 0);
}

TEST(Test, ref_ptr_operator_bool) {
  int flag = 1;
  auto ptr = make_ref<TestObject>(flag);
  ASSERT_TRUE(ptr);
  ptr.reset();
  ASSERT_FALSE(ptr);
}

TEST(Test, ref_ptr_operator_arrow) {
  int flag = 1;
  auto ptr = make_ref<TestObject>(flag);
  ASSERT_EQ(ptr->flag, flag);
}

TEST(Test, ref_ptr_operator_star) {
  int flag = 1;
  auto ptr = make_ref<TestObject>(flag);
  ASSERT_EQ((*ptr).flag, flag);
}

TEST(Test, ref_ptr_operator_equal) {
  int flag = 1;
  auto ptr = make_ref<TestObject>(flag);
  auto ptr2 = ptr;
  ASSERT_EQ(ptr, ptr2);
  ASSERT_EQ(ptr.get(), ptr2.get());
}

TEST(Test, ref_ptr_operator_not_equal) {
  int flag = 1;
  auto ptr = make_ref<TestObject>(flag);
  auto ptr2 = make_ref<TestObject>(flag);
  ASSERT_NE(ptr, ptr2);
  ASSERT_NE(ptr.get(), ptr2.get());
}

TEST(Test, ref_ptr_use_count) {
  int flag = 1;
  auto ptr = make_ref<TestObject>(flag);
  ASSERT_EQ(ptr.use_count(), 1);
}

TEST(Test, ref_ptr_get) {
  int flag = 1;
  auto ptr = make_ref<TestObject>(flag);
  ASSERT_EQ(ptr.get()->flag, flag);
}

TEST(Test, ref_ptr_reset_nullptr) {
  int flag = 1;
  auto ptr = make_ref<TestObject>(flag);
  ASSERT_EQ(ptr->ref_count(), 1);
  ptr.reset();
  ASSERT_EQ(ptr, nullptr);
  ASSERT_EQ(flag, 0);
}

TEST(Test, ref_ptr_destructor) {
  int flag = 1;
  {
    auto ptr = make_ref<TestObject>(flag);
    ASSERT_EQ(ptr->ref_count(), 1);
  }
  ASSERT_EQ(flag, 0);
}

TEST(Test, obs_ptr_constructor) {
  int flag = 1;
  auto ptr = make_ref<TestObject>(flag);
  auto obs = obs_ptr<TestObject>(ptr);
  ASSERT_EQ(obs.lock()->ref_count(), 2);

  auto obs2 = obs_ptr<TestObject>(obs);
  ASSERT_EQ(obs.lock()->ref_count(), 2);
}

TEST(Test, obs_ptr_copy_constructor) {
  int flag = 1;
  auto ptr = make_ref<TestObject>(flag);
  auto obs = obs_ptr<TestObject>(ptr);
  auto obs2(obs);
  ASSERT_EQ(obs.lock()->ref_count(), 2);
  ASSERT_EQ(obs2.lock()->ref_count(), 2);
}

TEST(Test, obs_ptr_copy_assignment) {
  int flag = 1;
  auto ptr = make_ref<TestObject>(flag);
  auto obs = obs_ptr<TestObject>(ptr);
  auto obs2 = obs;
  ASSERT_EQ(obs.lock()->ref_count(), 2);
  ASSERT_EQ(obs2.lock()->ref_count(), 2);
}

TEST(Test, obs_ptr_move_constructor) {
  int flag = 1;
  auto ptr = make_ref<TestObject>(flag);
  auto obs = obs_ptr<TestObject>(ptr);
  auto obs2(std::move(obs));
  ASSERT_EQ(obs.lock(), nullptr);
  ASSERT_EQ(obs2.lock()->ref_count(), 2);
}

TEST(Test, obs_ptr_move_assignment) {
  int flag = 1;
  auto ptr = make_ref<TestObject>(flag);
  auto obs = obs_ptr<TestObject>(ptr);
  auto obs2 = std::move(obs);
  ASSERT_EQ(obs.lock(), nullptr);
  ASSERT_EQ(obs2.lock()->ref_count(), 2);
}

TEST(Test, obs_ptr_reset) {
  int flag = 1;
  auto ptr = make_ref<TestObject>(flag);
  auto obs = obs_ptr<TestObject>(ptr);
  ASSERT_EQ(obs.lock()->ref_count(), 2);
  obs.reset();
  ASSERT_EQ(obs.lock(), nullptr);
  ASSERT_EQ(ptr->ref_count(), 1);
}

TEST(Test, obs_ptr_use_count) {
  int flag = 1;
  auto ptr = make_ref<TestObject>(flag);
  auto obs = obs_ptr<TestObject>(ptr);
  ASSERT_EQ(obs.use_count(), 1);
}

TEST(Test, obs_ptr_expired) {
  int flag = 1;
  auto ptr = make_ref<TestObject>(flag);
  auto obs = obs_ptr<TestObject>(ptr);
  ASSERT_FALSE(obs.expired());
  obs.reset();
  ASSERT_TRUE(obs.expired());
}

TEST(Test, obs_ptr_lock) {
  int flag = 1;
  auto ptr = make_ref<TestObject>(flag);
  auto obs = obs_ptr<TestObject>(ptr);
  auto lock = obs.lock();
  ASSERT_EQ(lock->ref_count(), 2);
}

TEST(Test, obs_ptr_reset_nullptr) {
  int flag = 1;
  auto ptr = make_ref<TestObject>(flag);
  auto obs = obs_ptr<TestObject>(ptr);
  ASSERT_EQ(obs.lock()->ref_count(), 2);
  obs.reset();
  ASSERT_EQ(obs.lock(), nullptr);
  ASSERT_EQ(ptr->ref_count(), 1);
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
    auto p = ref_ptr<TestObject>(
        vm_make<TestObject, IObject, TestAlloc>(&alloc, flag));
    {
      TestData data;
      for (auto task_id = 0; task_id < NUM; task_id++) {
        data.pool.append_task(
            Task<ref_ptr<TestObject>, obs_ptr<TestObject>>(data.tasks_ops, p),
            task_id);
      }
      data.pool.wait();
    }
    ASSERT_EQ(p->ref_count(), 1);
    ASSERT_EQ(p->weak_ref_count(), 0);
    ASSERT_EQ(alloc.allocCount.load(), 1);
  }
  ASSERT_EQ(alloc.allocCount.load(), 0);
}
