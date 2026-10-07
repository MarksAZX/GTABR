#include "jobs.h"

namespace gtabr {
JobSystem::JobSystem(unsigned n) {
  if (n < 1) n = 1;
  for (unsigned i = 0; i < n; ++i) threads_.emplace_back([this] { worker(); });
}
JobSystem::~JobSystem() {
  {
    std::lock_guard<std::mutex> l(m_);
    stop_ = true;
  }
  cv_.notify_all();
  for (auto& t : threads_) t.join();
}
void JobSystem::submit(std::function<void()> fn) {
  pending_++;
  {
    std::lock_guard<std::mutex> l(m_);
    q_.push(std::move(fn));
  }
  cv_.notify_one();
}
void JobSystem::worker() {
  for (;;) {
    std::function<void()> fn;
    {
      std::unique_lock<std::mutex> l(m_);
      cv_.wait(l, [&] { return stop_ || !q_.empty(); });
      if (stop_ && q_.empty()) return;
      fn = std::move(q_.front());
      q_.pop();
    }
    fn();
    if (--pending_ == 0) {
      std::lock_guard<std::mutex> l(m_);
      idleCv_.notify_all();
    }
  }
}
void JobSystem::waitIdle() {
  std::unique_lock<std::mutex> l(m_);
  idleCv_.wait(l, [&] { return pending_.load() == 0; });
}
}  // namespace gtabr
