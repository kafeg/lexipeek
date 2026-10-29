#include "core_client.hpp"

namespace lexipeek::qt {

NotesBus &notes_bus() {
  static NotesBus bus;
  return bus;
}

QString t(const char *key) { return q(app::text(key)); }
QString t(const char *key, const std::string &a) { return q(app::text(key, a)); }
QString t(const char *key, const std::string &a, const std::string &b) { return q(app::text(key, a, b)); }

CoreClient::CoreClient(const std::string &data, const Device &d) : device(d), engine(app::Engine::make(data, d)), data_(data) {
  lookups.setMaxThreadCount(1);
  worker.setMaxThreadCount(1);
  indexing.setMaxThreadCount(1);
  ai.setMaxThreadCount(4);
  prepare();
}

void CoreClient::prepare() {
  auto e = engine;
  indexing.start([e] { e.prepare(); });
}

void CoreClient::configure() {
  auto e = engine;
  auto d = data_;
  lookups.start([this, e, d] {
    e.configure(d);
    QMetaObject::invokeMethod(this, [this] { prepare(); }, Qt::QueuedConnection);
  });
}

void CoreClient::reload() {
  auto e = engine;
  lookups.start([this, e] {
    e.reload();
    QMetaObject::invokeMethod(this, [this] { prepare(); }, Qt::QueuedConnection);
  });
}

}  // namespace lexipeek::qt
