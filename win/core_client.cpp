#include "pch.h"
#include "core_client.hpp"
#include <thread>

namespace lexipeek::win {

void Queue::start(std::function<void()> work) {
  std::lock_guard lock(m_);
  work_.push_back(std::move(work));
  if (!running_) {
    running_ = true;
    std::thread([this] {
      for (;;) {
        std::unique_lock lock(m_);
        cv_.wait(lock, [this] { return !work_.empty(); });
        auto w = std::move(work_.front());
        work_.pop_front();
        lock.unlock();
        w();
      }
    }).detach();
  }
  cv_.notify_one();
}

CoreClient::CoreClient(const std::string &data, const Device &d)
    : device(d), engine(app::Engine::make(data, d)), data_(data), ui_(winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread()) {
  prepare();
}

void CoreClient::ui(std::function<void()> f) const { ui_.TryEnqueue([f] { f(); }); }

void CoreClient::prepare() {
  auto e = engine;
  indexing.start([e] { e.prepare(); });
}

void CoreClient::configure() {
  auto e = engine;
  auto d = data_;
  lookups.start([this, e, d] {
    e.configure(d);
    ui([this] { prepare(); });
  });
}

void CoreClient::reload() {
  auto e = engine;
  lookups.start([this, e] {
    e.reload();
    ui([this] { prepare(); });
  });
}

}  // namespace lexipeek::win
