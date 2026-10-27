// One heavy model in memory at a time; freed after its idle time, at once on release().
#pragma once
#include <atomic>
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
  // A new idle time for the held model too, counted from its last use.
  void set_idle(std::chrono::milliseconds idle);
  // Never waiting for a load or a use: what is held and when it goes (time_point::max(): kept until release).
  std::string held() const;
  clock::time_point until() const;

 private:
  void watch();
  void show();  // copies key_ and deadline_ for held() and until(); under mu_
  std::mutex mu_;
  std::condition_variable cv_;
  std::unique_ptr<Held> model_;
  std::string key_;
  clock::time_point deadline_, last_;
  std::atomic<long long> pending_{-1};  // set_idle's, for the watcher
  bool stop_ = false;
  mutable std::mutex shown_mu_;
  std::string shown_key_;
  clock::time_point shown_until_;
  std::thread idle_;
};

}  // namespace lexipeek::mt
