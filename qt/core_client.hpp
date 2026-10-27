// The core on thread pools (look-ups, translations, AI, indexing); each card asks through its own CardLane.
#pragma once
#include <QObject>
#include <QThreadPool>
#include <functional>
#include <map>
#include <mutex>
#include "lexipeek/app.hpp"

namespace lexipeek::qt {

// The app's texts (the core's table), as Qt strings.
QString t(const char *key);
QString t(const char *key, const std::string &a);
inline QString q(const std::string &s) { return QString::fromStdString(s); }

class CoreClient : public QObject {
 public:
  explicit CoreClient(const std::string &data = "");

  app::Engine engine;
  QThreadPool lookups, worker, ai, indexing;

  // work on a pool, done back on the main thread.
  template <class R>
  void run(QThreadPool &pool, std::function<R()> work, std::function<void(R)> done) {
    pool.start([this, work, done] {
      auto r = work();
      QMetaObject::invokeMethod(this, [done, r] { done(r); }, Qt::QueuedConnection);
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
  bool current(int id);
  void stop();
  CoreClient &c_;
  std::mutex mu_;
  int latest_ = 0, translating_ = 0;
  std::map<int, Card> cards_;
};

}  // namespace lexipeek::qt
