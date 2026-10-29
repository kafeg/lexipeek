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

// Notes changed (☆, a look-up's history, a removal, an import): the cards' ★ and the Notes list follow.
class NotesBus : public QObject {
  Q_OBJECT
 signals:
  void changed();
};
NotesBus &notes_bus();

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

}  // namespace lexipeek::qt
