#include <vector>
#include <random>

constexpr auto MAX_TASK_NUM = 20;
constexpr auto TASK_NUM = 20;
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