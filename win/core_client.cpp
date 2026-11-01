#include "pch.h"
#include "core_client.hpp"
#include <thread>
#include "card.hpp"

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
  // A device never named takes this computer's name (Settings → System → About's device name).
  wchar_t name[256];
  DWORD size = 256;
  std::string computer = GetComputerNameExW(ComputerNamePhysicalDnsHostname, name, &size) ? winrt::to_string(name) : "";
  auto e = engine;
  indexing.start([this, e, computer] {
    e.name_device(computer);
    if (e.sync(true)) ui([] { notes_bus().changed(); });
  });
  prepare();
  // Every 30 s the folder is looked at; read only when its files changed.
  sync_timer_ = ui_.CreateTimer();
  sync_timer_.Interval(std::chrono::seconds(30));
  sync_timer_.Tick([this](auto &&, auto &&) { sync(false); });
  sync_timer_.Start();
}

void CoreClient::sync(bool force) {
  auto e = engine;
  syncing.start([this, e, force] {
    if (e.sync(force)) ui([] { notes_bus().changed(); });
  });
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
