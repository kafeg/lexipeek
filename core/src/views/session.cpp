#include "lexipeek/session.hpp"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace lexipeek::app {

namespace {

// Work one at a time on a thread of its own; never destroyed (a job may still run at exit).
class Queue {
 public:
  Queue() {
    std::thread([this] {
      for (;;) {
        std::function<void()> job;
        {
          std::unique_lock l(mu_);
          cv_.wait(l, [this] { return !jobs_.empty(); });
          job = std::move(jobs_.front());
          jobs_.pop_front();
        }
        job();
      }
    }).detach();
  }
  void add(std::function<void()> job) {
    {
      std::lock_guard g(mu_);
      jobs_.push_back(std::move(job));
    }
    cv_.notify_one();
  }

 private:
  std::mutex mu_;
  std::condition_variable cv_;
  std::deque<std::function<void()>> jobs_;
};

// Look-ups and sections, translations: one at a time each, for every card (a model in memory once).
Queue &lookups() {
  static auto *q = new Queue;
  return *q;
}
Queue &translations() {
  static auto *q = new Queue;
  return *q;
}

}  // namespace

struct CardSession::Impl {
  Engine e;
  int tag = Engine::new_tag();
  // The UI thread's: the card and its requests.
  CardNav nav;
  bool waiting = false, dirty = false, notes = false;
  std::string key;
  std::atomic<int> request{0};
  int last = 0;
  int section = 0;  // the section page asked last (← makes an earlier one stale)
  bool section_waiting = false;
  std::atomic<int> translation{0};  // the translation asked last (another method makes an earlier one stale)
  CardNav before;                   // the card before a look-up (text that cleans to nothing brings it back)
  // From the workers: what to apply on the UI thread at the next poll.
  std::mutex mu;
  std::vector<std::function<void(Impl &)>> inbox;

  void post(std::function<void(Impl &)> f) {
    std::lock_guard g(mu);
    inbox.push_back(std::move(f));
  }
  bool current(int id) const { return request == id; }

  void run(const CardStep &step, const std::shared_ptr<Impl> &self) {
    dirty = true;
    switch (step.kind) {
      case CardStep::Kind::lookup: {
        e.cancel(tag);  // its own translation gives way
        waiting = true;
        ++section, section_waiting = false;  // a section page on its way belongs to the card before
        int id = request = ++last;
        lookups().add([self, step, id] {
          if (!self->current(id)) return;
          auto card = self->e.lookup_as(step.text, step.source, step.lang, step.answer);
          self->post([card, id](Impl &s) mutable {
            if (!s.current(id)) return;
            s.waiting = false;
            if (card.kind == Card::Kind::empty) return void(s.nav = s.before);  // nothing to look up: the card as it was
            card.ai.no_key = card.ai.key && s.key.empty();
            s.notes = true;
            s.nav.arrived(card);
            if (s.nav.state.has_method) s.translate(s.nav.state.method);
            s.ask();
          });
        });
        break;
      }
      case CardStep::Kind::restore:
        e.cancel(tag);
        request = ++last;
        waiting = false;  // what was on its way is dropped
        ++section, section_waiting = false;
        if (step.translate) translate(nav.state.method);
        ask();
        break;
      case CardStep::Kind::section: {
        int id = request, sec = ++section;
        section_waiting = true;
        auto card = nav.card();
        lookups().add([self, card, step, id, sec] {
          if (!self->current(id)) return;
          auto page = self->e.section(card, step.entry, step.key, step.page);
          self->post([page, id, sec](Impl &s) {
            if (!s.current(id) || s.section != sec) return;  // ← closed it meanwhile
            s.section_waiting = false;
            s.nav.section_arrived(page);
          });
        });
        break;
      }
      default:
        // ← out of a section: a page still on its way is not wanted (a look-up on its way still is).
        if (section_waiting && nav.state.section.key.empty()) ++section, section_waiting = false;
        break;
    }
  }

  std::weak_ptr<Impl> me;

  void translate(Method m) {
    if (!request || !nav.has_card()) return;
    e.cancel(tag);  // another method picked: its own running translation gives way
    nav.translating(m);
    dirty = true;
    int id = request, n = ++translation;
    auto card = nav.card();
    auto self = me.lock();
    translations().add([self, card, m, id, n] {
      Translation t;
      if (!self->current(id) || self->translation != n) t.error = "stale";
      else if (!card.study.empty()) {
        // Languages you learn: one after another, each shown as it comes; the first is the card's translation.
        for (int i = 0; i < static_cast<int>(card.study.size()); i++) {
          if (!self->current(id) || self->translation != n) break;
          auto x = self->e.translate_into(card, i, m, self->tag);
          if (i == 0) t = x;
          self->post([x, m, id, n, i](Impl &s) {
            if (s.current(id) && s.translation == n) s.nav.learned(i, m, x);
          });
        }
      } else t = self->e.translate(card, m, self->tag);
      self->post([t, m, id, n](Impl &s) {
        if (s.current(id) && s.translation == n) s.nav.translated(m, t);
      });
    });
  }

  // The AI answers the card shows now (the core says which: only on the person's action), the open one first.
  void ask() {
    if (!nav.has_card() || !request) return;
    auto asks = card_asks(nav.card(), nav.state);
    int id = request;
    auto self = me.lock();
    for (auto a : asks) {
      AiState asking;
      asking.asking = true;
      nav.state = with_answer(nav.state, a, asking);
      dirty = true;
      std::thread([self, card = nav.card(), a, k = key, id] {
        // A newer card by now: no request (an AI answer costs).
        if (!self->current(id)) return;
        auto st = self->e.ask(card, a, k);
        self->post([st, a, id](Impl &s) {
          if (s.current(id)) s.nav.state = with_answer(s.nav.state, a, st);
        });
      }).detach();
    }
  }
};

CardSession CardSession::make(const Engine &e) {
  CardSession s;
  s.impl_ = std::make_shared<Impl>();
  s.impl_->e = e;
  s.impl_->me = s.impl_;
  return s;
}

void CardSession::set_key(const std::string &key) { impl_->key = key; }

void CardSession::reset() {
  auto &i = *impl_;
  i.e.cancel(i.tag);
  i.request = 0, i.waiting = i.section_waiting = false, i.dirty = true;
  i.nav.reset();
}

void CardSession::look(const std::string &text, Source source, const std::string &lang, const std::string &answer) {
  impl_->before = impl_->nav;
  impl_->run(impl_->nav.look(text, source, false, lang, answer), impl_);
}
void CardSession::open(const std::string &word) {
  impl_->before = impl_->nav;
  impl_->run(impl_->nav.open(word), impl_);
}
void CardSession::relookup(const std::string &lang, const std::string &answer) {
  impl_->before = impl_->nav;
  impl_->run(impl_->nav.relookup(lang, answer), impl_);
}
void CardSession::back() { impl_->run(impl_->nav.back(), impl_); }
void CardSession::open_section(int entry, const std::string &key) { impl_->run(impl_->nav.open_section(entry, key, 0), impl_); }
void CardSession::turn(int by) { impl_->run(impl_->nav.turn(by), impl_); }

void CardSession::open_entry(int entry) {
  impl_->nav.open_entry(entry);
  impl_->dirty = true;
}

void CardSession::select(CardTab tab) {
  impl_->nav.select(tab);
  impl_->ask();
  impl_->dirty = true;
}

void CardSession::next_tab(int by) {
  impl_->nav.next_tab(by);
  impl_->ask();
  impl_->dirty = true;
}

void CardSession::full_entries() {
  impl_->nav.full_entries();
  impl_->dirty = true;
}

void CardSession::use(Method m) {
  if (!impl_->request || !impl_->nav.has_card()) return;
  impl_->nav.pick_method(m);
  impl_->translate(m);
}

void CardSession::choose_ai() {
  impl_->nav.pick_ai();
  impl_->ask();
  impl_->dirty = true;
}

void CardSession::toggle_saved() {
  auto &i = *impl_;
  if (!i.nav.has_card() || !card_view(i.nav.card(), i.nav.state).can_save) return;
  i.nav.state.saved = i.e.toggle_saved(i.nav.card(), shown_translation(i.nav.state));
  i.dirty = i.notes = true;
}

void CardSession::recheck_saved() {
  auto &i = *impl_;
  if (!i.nav.has_card()) return;
  if (bool saved = i.e.saved(i.nav.card()); saved != i.nav.state.saved) i.nav.state.saved = saved, i.dirty = true;
}

void CardSession::cancel() {
  auto &i = *impl_;
  i.e.cancel(i.tag);
  i.request = ++i.last;
  i.waiting = i.section_waiting = false;
  i.dirty = true;
  // Nothing comes for what was on its way: the card no longer waits for it.
  i.nav.state.translating = false;
  for (auto a : {ai::Ask::quick, ai::Ask::analysis, ai::Ask::context})
    if (card_answer(i.nav.state, a).asking) i.nav.state = with_answer(i.nav.state, a, AiState());
}

bool CardSession::poll() {
  auto &i = *impl_;
  std::vector<std::function<void(Impl &)>> got;
  {
    std::lock_guard g(i.mu);
    got.swap(i.inbox);
  }
  for (auto &f : got) f(i);
  bool changed = i.dirty || !got.empty();
  i.dirty = false;
  return changed;
}

bool CardSession::notes_changed() { return std::exchange(impl_->notes, false); }
bool CardSession::busy() const { return impl_->nav.busy() || impl_->waiting || impl_->section_waiting; }
bool CardSession::looking() const { return impl_->waiting && !impl_->nav.has_card(); }
bool CardSession::has_card() const { return impl_->nav.has_card(); }
CardNav CardSession::nav() const { return impl_->nav; }
CardView CardSession::view() const { return impl_->nav.has_card() ? card_view(impl_->nav.card(), impl_->nav.state) : CardView{}; }

}  // namespace lexipeek::app
