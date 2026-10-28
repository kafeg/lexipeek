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

void CardLane::stop() {
  bool running = false;
  {
    std::lock_guard g(s_->mu);
    running = s_->translating != 0;
  }
  if (running) c_.engine.cancel(tag_);
}

int CardLane::lookup(const std::string &text, Source source, const std::string &lang, const std::string &answer,
                     std::function<void(int, Card)> done) {
  int id = 0;
  {
    std::lock_guard g(s_->mu);
    id = ++s_->latest;
    s_->cards.clear();
  }
  stop();
  auto e = c_.engine;
  auto s = s_;
  c_.run<Card>(
      c_.lookups,
      [s, e, id, text, source, lang, answer] {
        if (!s->current(id)) return Card{};
        auto card = e.lookup_as(text, source, lang, answer);
        std::lock_guard g(s->mu);
        s->cards[id] = card;
        return card;
      },
      [s, id, done](Card card) {
        if (s->current(id) && card.kind != Card::Kind::empty) done(id, card);
      });
  return id;
}

int CardLane::restore(const Card &card) {
  std::lock_guard g(s_->mu);
  ++s_->latest;
  s_->cards = {{s_->latest, card}};
  return s_->latest;
}

void CardLane::translate(int id, Method m, std::function<void(Translation)> done) {
  stop();
  auto e = c_.engine;
  auto s = s_;
  auto tag = tag_;
  c_.run<Translation>(
      c_.worker,
      [s, e, id, m, tag] {
        Card card;
        {
          std::lock_guard g(s->mu);
          if (s->latest != id || !s->cards.contains(id)) return Translation{"", "", "", "stale"};
          card = s->cards[id];
          s->translating = id;
        }
        auto t = e.translate(card, m, tag);
        std::lock_guard g(s->mu);
        if (s->translating == id) s->translating = 0;
        return t;
      },
      [s, id, done](Translation t) {
        if (s->current(id)) done(t);
      });
}

void CardLane::ask(int id, const Card &card, ai::Ask a, const std::string &key, std::function<void(AiState)> done) {
  auto e = c_.engine;
  auto s = s_;
  c_.run<AiState>(
      c_.ai,
      [s, e, id, card, a, key] {
        // A newer card by now: no request (an AI answer costs).
        if (!s->current(id)) return AiState{};
        return e.ask(card, a, key);
      },
      [s, id, done](AiState st) {
        if (s->current(id)) done(st);
      });
}

}  // namespace lexipeek::qt
