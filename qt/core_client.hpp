// The core on thread pools (look-ups, translations, AI, indexing); each card asks through its own CardLane.
#pragma once
#include <QObject>
#include <QPointer>
#include <QThreadPool>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include "lexipeek/app.hpp"

namespace lexipeek::qt {

// The app's texts (the core's table), as Qt strings.
QString t(const char *key);
QString t(const char *key, const std::string &a);
QString t(const char *key, const std::string &a, const std::string &b);
inline QString q(const std::string &s) { return QString::fromStdString(s); }

class CoreClient : public QObject {
 public:
  CoreClient(const std::string &data, const Device &device);

  Device device;  // this machine: memory, the CPU's features, its architecture
  app::Engine engine;
  QThreadPool lookups, worker, ai, indexing;

  // work on a pool, done back on the main thread (not when ctx is gone by then).
  template <class R>
  void run(QThreadPool &pool, std::function<R()> work, std::function<void(R)> done, QObject *ctx = nullptr) {
    QPointer<QObject> alive(ctx);
    bool guarded = ctx;
    pool.start([this, work, done, alive, guarded] {
      auto r = work();
      QMetaObject::invokeMethod(this, [done, r, alive, guarded] {
        if (!guarded || alive) done(r);
      }, Qt::QueuedConnection);
    });
  }

  void configure();
  void reload();
  const std::string &data() const { return data_; }

 private:
  void prepare();
  std::string data_;
};

// One card's requests: a newer one makes its older ones no-ops and stops only its own translation.
class CardLane {
 public:
  explicit CardLane(CoreClient &c) : c_(c) {}
  int lookup(const std::string &text, Source source, const std::string &lang, const std::string &answer,
             std::function<void(int, Card)> done);
  int restore(const Card &card);
  void translate(int id, Method m, std::function<void(Translation)> done);
  void ask(int id, const Card &card, ai::Ask a, const std::string &key, std::function<void(AiState)> done);

 private:
  // Shared with the pools' work: a request still running at quit outlives the lane.
  struct State {
    std::mutex mu;
    int latest = 0, translating = 0;
    std::map<int, Card> cards;
    bool current(int id) {
      std::lock_guard g(mu);
      return latest == id;
    }
  };
  void stop();
  CoreClient &c_;
  int tag_ = app::Engine::new_tag();  // this card's translations, for cancel
  std::shared_ptr<State> s_ = std::make_shared<State>();
};

}  // namespace lexipeek::qt
