#include "../utils/task.h"
#include "../example/example.h"
#include "../utils/thread_pool.h"
#include "../utils/timer.h"

#include <benchmark/benchmark.h>

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
