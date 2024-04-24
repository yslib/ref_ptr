#include "../example/example.h"
#include "../utils/thread_pool.h"
#include "../utils/timer.h"

#include <benchmark/benchmark.h>
#include <random>

constexpr auto MAX_TASK_NUM = 20;
constexpr auto TASK_NUM = 20;
constexpr auto OPS_NUM = 1000000;

struct A {
  int a;
  int b;
  int c;
  float d;
};

int g_object_call_count = 0;

using RefPtr = ref_ptr<DerivedObject>;
using ObsPtr = obs_ptr<DerivedObject>;

using SharedPtr = std::shared_ptr<A>;
using WeakPtr = std::weak_ptr<A>;

template <typename T, typename U> struct Task {
  const std::vector<std::vector<int>> &tasks_ops;
  T ref;
  Task(const std::vector<std::vector<int>> &task_ops, T ref)
      : tasks_ops(task_ops), ref(std::move(ref)) {}
  void operator()(int task_op_idx) {
    const auto &task_op = tasks_ops[task_op_idx];
    std::vector<T> refv;
    benchmark::DoNotOptimize(refv);
    refv.reserve(OPS_NUM);
    std::vector<U> obsv;
    benchmark::DoNotOptimize(obsv);
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
    benchmark::ClobberMemory();

  }
};

struct TaskOps {
  thread_pool pool{MAX_TASK_NUM};
  std::vector<std::vector<int>> tasks_ops{TASK_NUM};
  TaskOps() {
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
};

TaskOps *init() {
  static TaskOps data;
  return &data;
}

template <typename StrongPtrType, typename WeakPtrType>
void BM_Concurrency(benchmark::State &st, StrongPtrType ptr) {
  auto data = init();
  const auto task_num = st.range(0);
  for (auto _ : st) {
    Timer t;
    for (auto task_id = 0; task_id < task_num; task_id++) {
      data->pool.append_task(
          Task<StrongPtrType, WeakPtrType>(data->tasks_ops, ptr), task_id);
    }
    data->pool.wait();
    st.SetIterationTime(t.elapse_s());
  }
}

BENCHMARK_TEMPLATE2_CAPTURE(BM_Concurrency, RefPtr, ObsPtr, ref_ptr,
                            make_ref<DerivedObject>())
    ->Name("ref_ptr")
    ->UseManualTime()
    ->DenseRange(1, 20);

BENCHMARK_TEMPLATE2_CAPTURE(BM_Concurrency, SharedPtr, WeakPtr, shared_ptr,
                            make_shared<A>())
    ->Name("shared_ptr")
    ->UseManualTime()
    ->DenseRange(1, 20);

BENCHMARK_MAIN();
