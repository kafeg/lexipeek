#include "mt/model_host.hpp"

namespace lexipeek::mt {

static ModelHost::clock::time_point due(ModelHost::clock::time_point from, std::chrono::milliseconds idle) {
  return idle.count() <= 0 ? ModelHost::clock::time_point::max() : from + idle;
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

void ModelHost::show() {
  std::lock_guard g(shown_mu_);
  shown_key_ = model_ ? key_ : "";
  shown_until_ = deadline_;
}

void ModelHost::with(const std::string &key, std::chrono::milliseconds idle, const std::function<std::unique_ptr<Held>()> &load,
                     const std::function<void(Held &)> &use) {
  std::unique_lock g(mu_);
  if (key_ != key) {
    model_.reset();  // the other model first: two would add up
    key_.clear();
    show();
    model_ = load();
    key_ = key;
    deadline_ = due(clock::now(), idle);
    show();
  }
  auto done = [&] {
    last_ = clock::now();
    deadline_ = due(last_, idle);
    show();
    cv_.notify_all();
  };
  try {
    use(*model_);
  } catch (...) {
    done();
    throw;
  }
  done();
}

void ModelHost::release() {
  std::lock_guard g(mu_);
  model_.reset();
  key_.clear();
  show();
}

void ModelHost::set_idle(std::chrono::milliseconds idle) {
  pending_ = idle.count();
  cv_.notify_all();
}

std::string ModelHost::held() const {
  std::lock_guard g(shown_mu_);
  return shown_key_;
}

ModelHost::clock::time_point ModelHost::until() const {
  std::lock_guard g(shown_mu_);
  return shown_until_;
}

void ModelHost::watch() {
  std::unique_lock g(mu_);
  while (!stop_) {
    if (auto p = pending_.exchange(-1); p >= 0 && model_) {
      deadline_ = due(last_, std::chrono::milliseconds(p));
      show();
    }
    if (!model_ || deadline_ == clock::time_point::max()) {
      cv_.wait(g);
    } else if (clock::now() >= deadline_) {
      model_.reset();
      key_.clear();
      show();
    } else {
      cv_.wait_until(g, deadline_);
    }
  }
}

}  // namespace lexipeek::mt
