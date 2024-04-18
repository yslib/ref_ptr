#include <functional>
#include <future>
#include <queue>
#include <vector>
using namespace std;
struct thread_pool {
  thread_pool(const thread_pool &) = delete;
  thread_pool &operator=(const thread_pool &) = delete;
  thread_pool(size_t);
  ~thread_pool();
  template <typename F, typename... Args>
  auto append_task(F &&f, Args &&...args);
  void wait();

private:
  vector<thread> workers;
  queue<function<void()>> tasks;
  mutex mut;
  atomic<size_t> idle;
  condition_variable cond;
  condition_variable waitCond;
  size_t nthreads;
  bool stop;
};
inline thread_pool::thread_pool(size_t threads)
    : idle(threads), nthreads(threads), stop(false) {
  for (size_t i = 0; i < threads; ++i)
    workers.emplace_back([this] {
      while (true) {
        function<void()> task;
        {
          unique_lock<mutex> lock(this->mut);
          this->cond.wait(
              lock, [this] { return this->stop || !this->tasks.empty(); });
          if (this->stop) {
            return;
          }
          idle--;
          task = std::move(this->tasks.front());
          this->tasks.pop();
        }
        task();
        idle++;
        {
          lock_guard<mutex> lk(this->mut);
          if (idle.load() == this->nthreads && this->tasks.empty()) {
            waitCond.notify_all();
          }
        }
      }
    });
}

// add new work item to the pool
template <class F, class... Args>
auto thread_pool::append_task(F &&f, Args &&...args) {
  using return_type = invoke_result_t<F, Args...>;

  // perfect capture couble be used in C++20.
  auto task = make_shared<packaged_task<return_type()>>(
      std::bind(std::forward<F>(f), std::forward<Args>(args)...));
  future<return_type> res = task->get_future();
  {
    unique_lock<mutex> lock(mut);
    // don't allow enqueueing after stopping the pool
    if (stop) {
      throw runtime_error("enqueue on stopped ThreadPool");
    }
    tasks.emplace([task]() { (*task)(); });
  }
  cond.notify_one();
  return res;
}

inline void thread_pool::wait() {
  mutex m;
  unique_lock<mutex> l(m);
  waitCond.wait(
      l, [this]() { return this->idle.load() == nthreads && tasks.empty(); });
}

// the destructor joins all threads
inline thread_pool::~thread_pool() {
  {
    unique_lock<mutex> lock(mut);
    stop = true;
  }
  cond.notify_all();
  for (thread &worker : workers) {
    if (worker.joinable())
      worker.join();
  }
}
