#include "snapshots.hpp"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include <map>
#include <optional>
#include "card.hpp"
#include "first_run.hpp"
#include "main_window.hpp"
#include "settings_window.hpp"
#include "ui.hpp"

namespace lexipeek::qt {

namespace {

#ifndef _WIN32
QPalette dark() {
  QPalette p;
  p.setColor(QPalette::Window, QColor(0x2b, 0x2b, 0x2b));
  p.setColor(QPalette::WindowText, QColor(0xe8, 0xe8, 0xe8));
  p.setColor(QPalette::Base, QColor(0x1e, 0x1e, 0x1e));
  p.setColor(QPalette::AlternateBase, QColor(0x33, 0x33, 0x33));
  p.setColor(QPalette::Text, QColor(0xe8, 0xe8, 0xe8));
  p.setColor(QPalette::Button, QColor(0x3a, 0x3a, 0x3a));
  p.setColor(QPalette::ButtonText, QColor(0xe8, 0xe8, 0xe8));
  p.setColor(QPalette::PlaceholderText, QColor(0x9a, 0x9a, 0x9a));
  p.setColor(QPalette::Highlight, QColor(0x3d, 0x8b, 0xfd));
  p.setColor(QPalette::Link, QColor(0x6c, 0xa8, 0xff));
  p.setColor(QPalette::Mid, QColor(0x50, 0x50, 0x50));
  return p;
}
#endif

// One picture: make draws it; step, once it is quiet, does what is still asked and says true, or false when it is final.
struct Shot {
  QString name;
  std::function<QWidget *()> make;
  std::function<bool()> quiet = [] { return true; };
  std::function<bool()> step = [] { return false; };
  bool keep_size = false;  // a window keeps its size
};

// A card case's asks (the Mac app's Snapshot.Request), done in its order once the card is quiet.
struct Request {
  std::optional<CardTab> tab;
  bool full = false, save = false, back = false, page = false, ai = false, whole = false;
  std::optional<std::string> open, section, answer;
  std::optional<int> entry;
  std::optional<Method> method;
  bool first = true;
};

// A case's url as the Mac app's script writes it (not percent-encoded: "#" and "+" are text): host and its fields.
struct Url {
  QString host;
  std::map<std::string, std::string> fields;
};
Url parse(const QString &url) {
  Url u;
  auto at = url.indexOf('?');
  u.host = url.left(at < 0 ? url.size() : at);
  if (at >= 0)
    for (auto &kv : url.mid(at + 1).split('&')) {
      auto eq = kv.indexOf('=');
      u.fields[kv.left(eq).toStdString()] = eq < 0 ? "" : kv.mid(eq + 1).toStdString();
    }
  return u;
}
std::string value(const Url &u, const char *k) {
  auto i = u.fields.find(k);
  return i == u.fields.end() ? "" : i->second;
}

// The fixture answers for a case into the replies dir ("error": every request fails with 401).
void answers(const QString &fixtures, const QString &kind) {
  auto replies = qEnvironmentVariable("LEXIPEEK_AI_REPLIES");
  if (replies.isEmpty() || kind.isEmpty()) return;
  QDir r(replies);
  for (auto &f : r.entryList(QDir::Files)) r.remove(f);
  if (kind == "error") {
    QFile s(r.filePath("status"));
    if (s.open(QIODevice::WriteOnly)) s.write("401");
    return;
  }
  QDir from(QDir(fixtures).filePath(kind));
  for (auto &f : from.entryList({"*.json"}, QDir::Files)) QFile::copy(from.filePath(f), r.filePath(f));
}

QScrollArea *scroller(QWidget *w) {
  if (auto *body = w->findChild<QScrollArea *>("body"); body && body->isVisible() && body->verticalScrollBar()->maximum() > 0) return body;
  for (auto *s : w->findChildren<QScrollArea *>())
    if (s->isVisible() && s->verticalScrollBar()->maximum() > 0) return s;
  return nullptr;
}

}  // namespace

int snapshots(QApplication &app, CoreClient &client, const SnapshotOptions &o) {
  QDir().mkpath(o.out);
  app.setQuitOnLastWindowClosed(false);
  // Windows: its own platform and style, windows never shown; elsewhere the minimal platform and Fusion.
#ifndef _WIN32
  QApplication::setStyle(QStyleFactory::create("Fusion"));
  auto light = QApplication::palette();
#endif
  auto *model = new CardModel(client);
  auto *main = new MainWindow(client);
  // AI answers from files: any key will do (the provider would want one).
  if (qEnvironmentVariableIsSet("LEXIPEEK_AI_REPLIES")) model->key = main->card.key = [] { return std::string("snapshot"); };
  main->lookup_key = "Alt+D", main->input_key = "Alt+A";
  auto *settings = new SettingsWindow(client);
  auto *about = new AboutWindow(client);
  auto *first = new FirstRunWindow(client);
  first->state.hotkey = "Alt+D", first->state.input = "Alt+A";
  CardWidget *card = nullptr;
  std::vector<Shot> shots;

  QFile f(o.cases);
  if (!o.cases.isEmpty() && f.open(QIODevice::ReadOnly | QIODevice::Text)) {
    QTextStream in(&f);
    while (!in.atEnd()) {
      auto parts = in.readLine().split('|');
      if (parts.size() < 2 || parts[0].isEmpty()) continue;
      auto name = parts[0], kind = parts.size() > 2 ? parts[2] : QString();
      auto query = parse(parts[1]);
      auto host = query.host;
      if (host == "window") {
        auto page = value(query, "page"), look = value(query, "look");
        int p = page == "words" ? MainWindow::words : page == "phrases" ? MainWindow::phrases : page == "history" ? MainWindow::history
                : page == "help" ? MainWindow::help : MainWindow::lookup;
        shots.push_back({name, [main, p, look] {
                           main->card.reset();
                           if (!look.empty()) main->look(q(look));
                           main->show_page(p);
                           main->refresh();
                           return main;
                         },
                         [main, look] { return look.empty() || (main->card.has_card() && !main->card.busy()); }, [] { return false; }, true});
        continue;
      }
      if (host == "firstrun") {
        auto step = value(query, "step");
        auto from = o.firstrun_from;
        shots.push_back({name, [first, step, from, &client] {
                           FirstRunState s = first->state;
                           s.list_error.clear(), s.error.clear(), s.downloading = false, s.done = s.total = 0;
                           QDir to(q(client.data()));
                           for (auto *file : {"packs.json", "installed.json"}) to.remove(file);
                           auto copy = [&](const char *file) { QFile::copy(QDir(from).filePath(file), to.filePath(file)); };
                           if (step == "pair" || step == "download" || step == "error") copy("packs.json");
                           if (step == "done") copy("packs.json"), copy("installed.json");
                           s.fetching = step == "list";
                           if (step == "list-error") s.list_error = "Could not resolve host: github.com";
                           if (step == "download") s.downloading = true, s.done = 27'000'000, s.total = 68'000'000;
                           if (step == "error") s.error = "Could not connect to server";
                           first->state = s;
                           first->refresh();
                           return first;
                         },
                         [] { return true; }, [] { return false; }, true});
        continue;
      }
      // A card: lookup?…, input (the text field), unread (the app whose selection could not be read).
      Request r;
      if (auto t = value(query, "tab"); !t.empty())
        r.tab = t == "dictionary" ? CardTab::dictionary : t == "analysis" ? CardTab::analysis : t == "context" ? CardTab::context : CardTab::translate;
      r.full = value(query, "full") == "1", r.save = value(query, "save") == "1", r.back = value(query, "back") == "1";
      r.page = value(query, "page") == "1", r.whole = value(query, "whole") == "1";
      if (auto x = value(query, "open"); !x.empty()) r.open = x;
      if (auto x = value(query, "section"); !x.empty()) r.section = x;
      if (auto x = value(query, "answer"); !x.empty()) r.answer = x;
      if (auto x = value(query, "entry"); !x.empty()) r.entry = std::stoi(x);
      auto m = value(query, "method");
      r.ai = m == "ai";
      if (m == "fast") r.method = Method::fast;
      if (m == "direct") r.method = Method::direct;
      if (m == "llm") r.method = Method::llm;
      auto text = value(query, "text");
      auto req = std::make_shared<Request>(r);
      auto fixtures = o.fixtures;
      shots.push_back({name,
                       [model, &card, host, text, kind, fixtures, req, r] {
                         *req = r;  // each pass asks again
                         answers(fixtures, kind);
                         model->reset();
                         model->input = host == "input" || host == "unread";
                         if (host == "unread") model->unread = "Example";
                         delete card;
                         card = new CardWidget(*model);
                         if (host == "lookup") model->lookup(text);
                         return card;
                       },
                       [model, host] { return host != "lookup" || (model->has_card() && !model->busy()); },
                       [model, &card, req] {
                         auto &r = *req;
                         if (!model->has_card()) return false;
                         if (r.first) {
                           r.first = false;
                           if (r.method && model->nav.state.has_method) return model->use(*r.method), true;
                           if (r.ai && model->nav.card().ai.translate) return model->choose_ai(), true;
                         }
                         if (model->look().tabs) {
                           if (r.tab && model->nav.state.tab != *r.tab) return model->select(*r.tab), true;
                           if (r.open) return model->open(*std::exchange(r.open, std::nullopt)), true;
                           if (r.full && !model->nav.state.full_entry && !model->nav.card().entries.empty()) return model->toggle_entry(), true;
                           if (r.section) return model->open_section(0, *std::exchange(r.section, std::nullopt)), true;
                           if (r.entry) return model->open_entry(*std::exchange(r.entry, std::nullopt)), true;
                           if (r.page && model->nav.state.section.page == 0 && !model->nav.state.section.key.empty())
                             return r.page = false, model->turn(1), true;
                           if (r.back) return r.back = false, model->back(), true;
                           if (r.answer) return model->relookup("", *std::exchange(r.answer, std::nullopt)), true;
                         }
                         if (r.save && !model->nav.state.saved) return r.save = false, model->toggle_saved(), true;
                         if (r.whole) return r.whole = false, card->show_whole(true), true;
                         return false;
                       }});
    }
  }
  std::vector<Shot> panes;
  if (o.settings) {
    static const char *keys[] = {"languages", "card", "ai", "general"};
    for (auto s : shown_sections) {
      auto tabs = settings_tab_keys(s);
      auto base = QString("settings-") + keys[static_cast<int>(s)];
      for (int tab = 0; tab < std::max<int>(1, static_cast<int>(tabs.size())); ++tab)
        panes.push_back({(tabs.empty() ? base : base + "-" + q(tabs[tab])) + o.suffix, [settings, s, tab] {
                           settings->select(s, tab);
                           return settings;
                         },
                         [] { return true; }, [] { return false; }, true});
    }
    panes.push_back({"about" + o.suffix, [about] {
                       about->refresh();
                       return about;
                     }});
  }
  // Settings first: the cases after them may change the data dir (☆, a first-run step's files).
  shots.insert(shots.begin(), panes.begin(), panes.end());

  int i = 0, failed = 0, settled = 0, n = static_cast<int>(shots.size());
  QWidget *shown = nullptr;
  QElapsedTimer since;
  auto start = [&] {
    if (i == n * 2) return app.quit();
    bool is_dark = i >= n;
#ifdef _WIN32
    QGuiApplication::styleHints()->setColorScheme(is_dark ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light);
    ui::follow_scheme();
#else
    QApplication::setPalette(is_dark ? dark() : light);
#endif
    if (i % n == 0 && !o.firstrun_from.isEmpty())
      for (auto *file : {"packs.json", "installed.json"}) QDir(q(client.data())).remove(file);
    if (shown && shown != card) shown->hide();
    shown = shots[i % n].make();
    shown->setAttribute(Qt::WA_DontShowOnScreen);
    shown->show();
    settled = 0;
    since.start();
  };
  auto *poll = new QTimer(&app);
  QObject::connect(poll, &QTimer::timeout, [&] {
    auto &s = shots[i % n];
    bool quiet = s.quiet();
    if (!quiet && since.elapsed() < 90000) return;
    // Quiet: three more ticks (a layout shows its new widgets by a queued call), then the next ask or the picture.
    if (++settled < 4) return;
    if (quiet && s.step()) return void(settled = 0);
    if (!quiet) ++failed;
    if (!s.keep_size) shown->adjustSize();
    auto file = QDir(o.out).filePath(s.name + (i >= n ? "-dark" : ""));
    shown->grab().save(file + ".png");
    // The body scrolled to its end, when it scrolls.
    if (auto *scroll = scroller(shown)) {
      scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
      shown->grab().save(file + "-end.png");
      scroll->verticalScrollBar()->setValue(0);
    }
    ++i;
    start();
  });
  start();
  poll->start(100);
  app.exec();
  return failed;
}

}  // namespace lexipeek::qt
