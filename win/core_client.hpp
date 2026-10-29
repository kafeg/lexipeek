// The core on worker threads (look-ups, the model, indexing), results back on the UI thread.
#pragma once
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include "lexipeek/app.hpp"

namespace lexipeek::win {

// Work one after another on a thread of its own (never joined: the process leaves by app::leave).
class Queue {
 public:
  void start(std::function<void()> work);

 private:
  std::mutex m_;
  std::condition_variable cv_;
  std::deque<std::function<void()>> work_;
  bool running_ = false;
};

class CoreClient {
 public:
  CoreClient(const std::string &data, const Device &device);
  Device device;  // this machine: memory, the CPU's features, its architecture
  app::Engine engine;
  Queue lookups, worker, indexing;

  void configure();
  void reload();
  const std::string &data() const { return data_; }
  // f on the UI thread, after the current message.
  void ui(std::function<void()> f) const;

 private:
  void prepare();
  std::string data_;
  winrt::Microsoft::UI::Dispatching::DispatcherQueue ui_{nullptr};
};

}  // namespace lexipeek::win
