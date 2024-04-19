#include "../include/refcnt.h"
#include "../utils/thread_pool.h"
#include "../utils/timer.h"

#include <benchmark/benchmark.h>
#include <random>

constexpr auto MAX_TASK_NUM = 20;
constexpr auto TASK_NUM = 10;
constexpr auto OPS_NUM = 1000000;

template <typename T, typename U> struct Task {
  const std::vector<std::vector<int>> &tasks_ops;
  T ref;
  Task(const std::vector<std::vector<int>> &task_ops, T ref)
      : tasks_ops(task_ops), ref(std::move(ref)) {}
  void operator()(int task_op_idx) {
    const auto &task_op = tasks_ops[task_op_idx];
    std::vector<T> refv;
    refv.reserve(OPS_NUM);
    std::vector<U> obsv;
    obsv.reserve(OPS_NUM);
    for (const auto &op : task_op) {
      switch (op) {
      case 0: {
        refv.push_back(ref);
      } break;
      case 1: {
        if (!refv.empty()) {
          refv.pop_back();
        }
      } break;
      case 2: {
        obsv.push_back(ref);
      } break;
      case 3: {
        if (!obsv.empty()) {
          obsv.pop_back();
        }
      } break;
      case 4: {
        if (!obsv.empty()) {
          if (auto ptr = obsv.back().lock(); ptr) {
            refv.push_back(ptr);
          }
        }
      } break;
      default:
        break;
      }
    }
  }
};

class ConcurrencyBench : public benchmark::Fixture {
public:
  thread_pool pool{MAX_TASK_NUM};
  std::vector<std::vector<int>> tasks_ops{TASK_NUM};

  void SetUp(::benchmark::State &state) {
    unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();
    std::default_random_engine generator(seed);
    std::uniform_int_distribution<int> distribution(0, 4);
    for (auto &ops : tasks_ops) {
      ops.reserve(OPS_NUM);
      for (int i = 0; i < OPS_NUM; i++) {
        ops.push_back(distribution(generator));
      }
    }
  }

  void TearDown(::benchmark::State &state) {}
};

BENCHMARK_DEFINE_F(ConcurrencyBench, ref_ptr)(benchmark::State &st) {

  auto my_ptr = ref_ptr<IObject>(
      NEW<IObject, AllocImpl, RefCntImpl<IObject, AllocImpl>>(nullptr));
  for (auto _ : st) {
    Timer t;
    for (auto task_id = 0; task_id < TASK_NUM; task_id++) {
      pool.append_task(
          Task<ref_ptr<IObject>, obs_ptr<IObject>>(tasks_ops, my_ptr), task_id);
    }
    pool.wait();
    st.SetIterationTime(t.elapse_s());
  }
}
BENCHMARK_REGISTER_F(ConcurrencyBench, ref_ptr)->UseManualTime()->Threads(1);

BENCHMARK_DEFINE_F(ConcurrencyBench, shared_ptr)(benchmark::State &st) {

  auto my_ptr = ref_ptr<IObject>(
      NEW<IObject, AllocImpl, RefCntImpl<IObject, AllocImpl>>(nullptr));

  struct A {
    int a;
    int b;
    int c;
    float d;
  };

  auto std_ptr = std::make_shared<A>(A());
  for (auto _ : st) {
    Timer t;
    for (auto task_id = 0; task_id < TASK_NUM; task_id++) {
      pool.append_task(
          Task<std::shared_ptr<A>, std::weak_ptr<A>>(tasks_ops, std_ptr),
          task_id);
    }
    pool.wait();
    st.SetIterationTime(t.elapse_s());
  }
}

BENCHMARK_REGISTER_F(ConcurrencyBench, shared_ptr)->UseManualTime()->Threads(1);

// Run the benchmark
BENCHMARK_MAIN();
