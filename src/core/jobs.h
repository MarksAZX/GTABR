// Tiny thread pool used for asynchronous asset decoding and chunk mesh generation.
#pragma once
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace gtabr {
class JobSystem {
 public:
  explicit JobSystem(unsigned threads);
  ~JobSystem();
  void submit(std::function<void()> fn);
  // Number of jobs submitted but not yet finished.
  int pending() const { return pending_.load(); }
  void waitIdle();
 private:
  void worker();
  std::vector<std::thread> threads_;
  std::queue<std::function<void()>> q_;
  std::mutex m_;
  std::condition_variable cv_, idleCv_;
  std::atomic<int> pending_{0};
  bool stop_ = false;
};
}  // namespace gtabr
