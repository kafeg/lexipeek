#include "mt/model_host.hpp"

namespace lexipeek::mt {

static ModelHost::clock::time_point due(std::chrono::milliseconds idle) {
  return idle.count() <= 0 ? ModelHost::clock::time_point::max() : ModelHost::clock::now() + idle;
}

ModelHost::ModelHost() : idle_([this] { watch(); }) {}

ModelHost::~ModelHost() {
  {
    std::lock_guard g(mu_);
    stop_ = true;
  }
  cv_.notify_all();
  idle_.join();
}

void ModelHost::with(const std::string &key, std::chrono::milliseconds idle, const std::function<std::unique_ptr<Held>()> &load,
                     const std::function<void(Held &)> &use) {
  std::unique_lock g(mu_);
  if (key_ != key) {
    model_.reset();  // the other model first: two would add up
    key_.clear();
    model_ = load();
    key_ = key;
  }
  try {
    use(*model_);
  } catch (...) {
    deadline_ = due(idle);
    cv_.notify_all();
    throw;
  }
  deadline_ = due(idle);
  cv_.notify_all();
}

void ModelHost::release() {
  std::lock_guard g(mu_);
  model_.reset();
  key_.clear();
}

std::string ModelHost::held() const {
  std::lock_guard g(mu_);
  return key_;
}

ModelHost::clock::time_point ModelHost::until() const {
  std::lock_guard g(mu_);
  return deadline_;
}

void ModelHost::watch() {
  std::unique_lock g(mu_);
  while (!stop_) {
    if (!model_ || deadline_ == clock::time_point::max()) {
      cv_.wait(g);
    } else if (clock::now() >= deadline_) {
      model_.reset();
      key_.clear();
    } else {
      cv_.wait_until(g, deadline_);
    }
  }
}

}  // namespace lexipeek::mt
