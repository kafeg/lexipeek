// One heavy model in memory at a time; freed after its idle time, at once on release().
#pragma once
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace lexipeek::mt {

struct Held {
  virtual ~Held() = default;
};

class ModelHost {
 public:
  using clock = std::chrono::steady_clock;
  ModelHost();
  ~ModelHost();

  // Runs use with the model of key under the host's lock, loading it (after freeing another) when it is not held.
  // idle: how long it stays after this use (0: until release). A throwing load leaves nothing held.
  void with(const std::string &key, std::chrono::milliseconds idle, const std::function<std::unique_ptr<Held>()> &load,
            const std::function<void(Held &)> &use);
  void release();
  std::string held() const;
  // When the held model goes (time_point::max(): kept until release; meaningless when none is held).
  clock::time_point until() const;

 private:
  void watch();
  mutable std::mutex mu_;
  std::condition_variable cv_;
  std::unique_ptr<Held> model_;
  std::string key_;
  clock::time_point deadline_;
  bool stop_ = false;
  std::thread idle_;
};

}  // namespace lexipeek::mt
