#include <chrono>
struct Timer {
  decltype(std::chrono::high_resolution_clock::now()) _start;
  const char *name{nullptr};
  Timer(const char *name = 0) : name(name) {
    _start = std::chrono::high_resolution_clock::now();
  }

  auto elapse_s() const {
    const auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end - _start;
    return diff.count();
  }
  // ~Timer() {
  //   auto end = std::chrono::high_resolution_clock::now();
  //   std::chrono::duration<double> diff = end - _start;
  //   std::cout << "[" << name << "]\tTime Cost: " << diff.count() << " s\n";
  // }
};
