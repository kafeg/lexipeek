#include "core_client.hpp"

namespace lexipeek::qt {

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

bool CardLane::current(int id) {
  std::lock_guard g(mu_);
  return latest_ == id;
}

void CardLane::stop() {
  bool running = false;
  {
    std::lock_guard g(mu_);
    running = translating_ != 0;
  }
  if (running) c_.engine.cancel();
}

int CardLane::lookup(const std::string &text, Source source, const std::string &lang, const std::string &answer,
                     std::function<void(int, Card)> done) {
  int id = 0;
  {
    std::lock_guard g(mu_);
    id = ++latest_;
    cards_.clear();
  }
  stop();
  auto e = c_.engine;
  c_.run<Card>(
      c_.lookups,
      [this, e, id, text, source, lang, answer] {
        if (!current(id)) return Card{};
        auto card = e.lookup_as(text, source, lang, answer);
        std::lock_guard g(mu_);
        cards_[id] = card;
        return card;
      },
      [this, id, done](Card card) {
        if (current(id) && card.kind != Card::Kind::empty) done(id, card);
      });
  return id;
}

int CardLane::restore(const Card &card) {
  std::lock_guard g(mu_);
  ++latest_;
  cards_ = {{latest_, card}};
  return latest_;
}

void CardLane::translate(int id, Method m, std::function<void(Translation)> done) {
  stop();
  auto e = c_.engine;
  c_.run<Translation>(
      c_.worker,
      [this, e, id, m] {
        Card card;
        {
          std::lock_guard g(mu_);
          if (latest_ != id || !cards_.contains(id)) return Translation{"", "", "", "stale"};
          card = cards_[id];
          translating_ = id;
        }
        auto t = e.translate(card, m);
        std::lock_guard g(mu_);
        if (translating_ == id) translating_ = 0;
        return t;
      },
      [this, id, done](Translation t) {
        if (current(id)) done(t);
      });
}

void CardLane::ask(int id, const Card &card, ai::Ask a, const std::string &key, std::function<void(AiState)> done) {
  auto e = c_.engine;
  c_.run<AiState>(
      c_.ai, [e, card, a, key] { return e.ask(card, a, key); },
      [this, id, done](AiState st) {
        if (current(id)) done(st);
      });
}

}  // namespace lexipeek::qt
