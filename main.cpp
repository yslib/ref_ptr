#include "refcnt.h"
#include "thread_pool.h"
#include <chrono>

#include <mutex>
#include <random>
//
constexpr auto OPS_NUM = 1000000;
constexpr auto TASK_NUM = 10;
constexpr auto LOOP_NUM = 1000;

// constexpr auto OPS_NUM = 10;
// constexpr auto TASK_NUM = 10;
// constexpr auto LOOP_NUM = 10;
struct Timer {
  decltype(std::chrono::high_resolution_clock::now()) _start;
  const char *name{nullptr};
  Timer(const char *name = 0) : name(name) {
    _start = std::chrono::high_resolution_clock::now();
  }
  ~Timer() {
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end - _start;
    std::cout << "[" << name << "]\tTime Cost: " << diff.count() << " s\n";
  }
};

template <typename T, typename U> struct Task {
  const std::vector<std::vector<int>> &tasks_ops;
  T ref;
  Task(const std::vector<std::vector<int>> &task_ops, T ref)
      : tasks_ops(task_ops), ref(ref) {}
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
        obsv.push_back(U(ref));
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

int main() {
  AllocImpl alloc;
  unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();
  std::default_random_engine generator(seed);
  std::uniform_int_distribution<int> distribution(0, 4);

  thread_pool pool(20);

  auto my_ptr = ref_ptr<IObject>(
      NEW<IObject, AllocImpl, RefCntImpl<IObject, AllocImpl>>(nullptr));

  auto std_ptr = std::make_shared<int>(1);

  std::vector<std::vector<int>> tasks_ops(TASK_NUM);

  // randomly init ops sequence
  for (auto &ops : tasks_ops) {
    ops.reserve(OPS_NUM);
    for (int i = 0; i < OPS_NUM; i++) {
      ops.push_back(distribution(generator));
    }
  }

  {
    Timer t("ref_ptr");
    for (auto task_id = 0; task_id < TASK_NUM; task_id++) {
      pool.append_task(
          Task<ref_ptr<IObject>, obs_ptr<IObject>>(tasks_ops, my_ptr), task_id);
    }
    pool.wait();
  }

  {
    Timer t("shared_ptr");
    for (auto task_id = 0; task_id < TASK_NUM; task_id++) {
      pool.append_task(
          Task<std::shared_ptr<int>, std::weak_ptr<int>>(tasks_ops, std_ptr),
          task_id);
    }
    pool.wait();
  }

  return 0;
}
