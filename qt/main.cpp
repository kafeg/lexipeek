#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QLocale>
#include <QStyle>
#include <QStyleFactory>
#include <QTimer>
#include <cstdlib>
#include "card.hpp"
#include "main_window.hpp"
#include "platform.hpp"
#include "settings_window.hpp"
#include "shell.hpp"

using namespace lexipeek;
using namespace lexipeek::qt;

namespace {

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

// One picture: make draws it, ready says it is final (it may first ask for more, e.g. a tab).
struct Shot {
  QString name;
  std::function<QWidget *()> make;
  std::function<bool()> ready = [] { return true; };
};

struct CardCase {
  const char *name, *text;
  CardTab tab;
};

const CardCase cards[] = {
    {"word", "Haus", CardTab::dictionary},
    {"word-translate", "Haus", CardTab::translate},
    {"phrase", "Ich mache die Tür zu", CardTab::translate},
    {"phrase-words", "Ich mache die Tür zu", CardTab::dictionary},
    {"passage", "Das Haus steht am Ende der Straße. Dort wohnt meine Großmutter seit vielen Jahren.", CardTab::translate},
    {"unknown", "Qwxzt", CardTab::translate},
};

// `--snapshots <dir>`: the cards, the LexiPeek window, every Settings section × tab and About, light and dark, on Qt's
// minimal platform (nothing on the screen); nothing is written to the data dir. The count of shots not final in time.
int snapshots(QApplication &app, CoreClient &client, const QString &dir) {
  QDir().mkpath(dir);
  app.setQuitOnLastWindowClosed(false);
  QApplication::setStyle(QStyleFactory::create("Fusion"));
  auto light = QApplication::style()->standardPalette();
  auto *model = new CardModel(client);
  model->source = Source::cli;
  auto *main = new MainWindow(client);
  main->source = Source::cli;
  main->lookup_key = "Alt+D", main->input_key = "Alt+A";
  auto *settings = new SettingsWindow(client);
  auto *about = new AboutWindow(client);
  std::vector<Shot> shots;
  for (auto &c : cards)
    shots.push_back({c.name,
                     [model, &c] {
                       model->reset();
                       model->lookup(c.text);
                       return new CardWidget(*model);
                     },
                     [model, &c] {
                       if (!model->has_card() || model->busy()) return false;
                       if (model->look().tab == c.tab) return true;
                       model->select(c.tab);
                       return false;
                     }});
  auto page = [&](const char *name, int p, const char *look) {
    shots.push_back({name,
                     [main, p, look] {
                       main->card.reset();
                       if (look) main->look(look);
                       main->show_page(p);
                       return main;
                     },
                     [main, look] { return !look || (main->card.has_card() && !main->card.busy()); }});
  };
  page("window-empty", MainWindow::lookup, nullptr);
  page("window-lookup", MainWindow::lookup, "Haus");
  page("window-words", MainWindow::words, nullptr);
  page("window-history", MainWindow::history, nullptr);
  page("window-help", MainWindow::help, nullptr);
  for (auto s : shown_sections) {
    auto keys = settings_tab_keys(s);
    static const char *names[] = {"languages", "card", "ai", "general"};
    auto base = QString("settings-") + names[static_cast<int>(s)];
    for (int tab = 0; tab < std::max<int>(1, static_cast<int>(keys.size())); ++tab)
      shots.push_back({keys.empty() ? base : base + "-" + q(keys[tab]), [settings, s, tab] {
                         settings->select(s, tab);
                         return settings;
                       }});
  }
  shots.push_back({"about", [about] {
                     about->refresh();
                     return about;
                   }});

  int i = 0, failed = 0, settled = 0;
  QWidget *shown = nullptr;
  QElapsedTimer since;
  auto start = [&] {
    if (i == static_cast<int>(shots.size()) * 2) return app.quit();
    QApplication::setPalette(i >= static_cast<int>(shots.size()) ? dark() : light);
    if (shown && shown != main && shown != settings && shown != about) delete shown;
    else if (shown) shown->hide();
    shown = shots[i % shots.size()].make();
    shown->setAttribute(Qt::WA_DontShowOnScreen);
    shown->show();
    settled = 0;
    since.start();
  };
  auto *poll = new QTimer(&app);
  QObject::connect(poll, &QTimer::timeout, [&] {
    auto &s = shots[i % shots.size()];
    bool done = s.ready();
    if (!done && since.elapsed() < 60000) return;
    if (!done) ++failed;
    // Final: two more ticks (a layout shows its new widgets by a queued call).
    if (++settled < 3) return;
    if (shown != main && shown != settings) shown->adjustSize();  // a window keeps its size
    shown->grab().save(dir + "/" + s.name + (i >= static_cast<int>(shots.size()) ? "-dark" : "") + ".png");
    ++i;
    start();
  });
  start();
  poll->start(100);
  app.exec();
  return failed;
}

}  // namespace

int main(int argc, char **argv) {
  for (int i = 1; i < argc; ++i)
    if (std::string(argv[i]) == "--snapshots") qputenv("QT_QPA_PLATFORM", "minimal:enable_fonts");
  QApplication app(argc, argv);
  auto args = app.arguments();
  std::string data;
  if (auto i = args.indexOf("--data"); i > 0 && i + 1 < args.size()) data = args[i + 1].toStdString();
  else if (auto *env = std::getenv("LEXIPEEK_DATA")) data = env;
  app::set_language(data, QLocale::system().name().left(2).toStdString());
  CoreClient client(data, app::device(platform::ram_mb(), cpu_features(), cpu_arch()));
  if (auto i = args.indexOf("--snapshots"); i > 0 && i + 1 < args.size()) return snapshots(app, client, args[i + 1]);
  Shell shell(client);
  // No pair yet: Settings → Languages → Packs (the first-run window later).
  if (!app::has_pair(data)) shell.open_settings(static_cast<int>(SettingsSection::languages), 0);
  return app.exec();
}
