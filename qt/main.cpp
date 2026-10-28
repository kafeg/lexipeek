#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSettings>
#include <QTextStream>
#include <QLocale>
#include <QStyle>
#include <QStyleFactory>
#include <QTimer>
#include <cstdlib>
#include "card.hpp"
#include "first_run.hpp"
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
  // Windows: its own style, as the app shows; elsewhere Fusion (the same on every Mac).
#ifndef _WIN32
  QApplication::setStyle(QStyleFactory::create("Fusion"));
#endif
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
  auto *first = new FirstRunWindow(client);
  first->state.hotkey = "Alt+D", first->state.input = "Alt+A";
  shots.push_back({"firstrun", [first] {
                     first->refresh();
                     return first;
                   }});

  int i = 0, failed = 0, settled = 0;
  QWidget *shown = nullptr;
  QElapsedTimer since;
  auto start = [&] {
    if (i == static_cast<int>(shots.size()) * 2) return app.quit();
    QApplication::setPalette(i >= static_cast<int>(shots.size()) ? dark() : light);
    if (shown && shown != main && shown != settings && shown != about && shown != first) delete shown;
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
    if (shown != main && shown != settings && shown != first) shown->adjustSize();  // a window keeps its size
    shown->grab().save(dir + "/" + s.name + (i >= static_cast<int>(shots.size()) ? "-dark" : "") + ".png");
    ++i;
    start();
  });
  start();
  poll->start(100);
  app.exec();
  return failed;
}

// `--self-test <file>`: the OS parts that need no desktop (the Credential Manager, the Run key put back as it was,
// hotkeys, memory), one line each "ok|FAIL what", into file; the exit code the failures' count.
int self_test(const QString &path) {
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return 1;
  QTextStream out(&f);
  int failed = 0;
  auto check = [&](bool ok, const QString &what) {
    out << (ok ? "ok   " : "FAIL ") << what << "\n";
    failed += !ok;
  };
  const std::string provider = "self-test";
  auto said = [](bool ok) { return ok ? QString() : " " + q(platform::last_error()); };
  bool w = platform::set_secret(provider, "k-123 ключ");
  check(w, "secret: written" + said(w));
  check(platform::secret(provider) == "k-123 ключ", "secret: read back as written");
  bool r = platform::set_secret(provider, "");
  check(r, "secret: removed" + said(r));
  check(platform::secret(provider).empty(), "secret: gone");
  bool again = platform::set_secret(provider, "");
  check(again, "secret: removing a missing one is fine" + said(again));
  bool was = platform::login();
  platform::set_login(!was);
  check(platform::login() == !was, "login: switched");
  platform::set_login(was);
  check(platform::login() == was, "login: back as it was");
  check(platform::ram_mb() > 512, "ram: " + QString::number(platform::ram_mb()) + " MB");
  Hotkey h{"Q", true, false, true, true};
  bool reg = platform::register_hotkey(90, h, [] {});
  check(reg, "hotkey: Ctrl+Alt+Shift+Q registered" + said(reg));
  platform::unregister_hotkey(90);
  check(platform::register_hotkey(90, h, [] {}), "hotkey: registered again after unregistering");
  platform::unregister_hotkey(90);
  check(platform::hotkey_label(Hotkey{"D", true, false, false, false}) == "Alt+D", "hotkey: label Alt+D");
  check(!platform::register_hotkey(91, Hotkey{"%", true, false, false, false}, [] {}), "hotkey: a key it cannot take refused");
  if (auto run = platform::cli_runner()) {
    ai::CliCall echo{"findstr", {"ein"}, {{"LEXIPEEK_X", "1"}}, "das ist ein Haus\nnichts\n", ""};
    auto r = run(echo);
    check(r.code == 0 && r.out.find("das ist ein Haus") != std::string::npos && r.out.find("nichts") == std::string::npos,
          "cli: a program with the prompt on stdin (" + QString::number(r.code) + ")");
    check(run({"no-such-program-lexipeek", {}, {}, "", ""}).code == 127, "cli: a missing program is 127");
  }
  platform::register_scheme();
  QSettings scheme("HKEY_CURRENT_USER\\Software\\Classes\\lexipeek\\shell\\open\\command", QSettings::NativeFormat);
  check(scheme.value("Default").toString().contains("LexiPeek.exe"), "lexipeek:// opens " + scheme.value("Default").toString());
  out << "info front app: " << q(platform::front_app()) << ", selection: " << (platform::selection() ? "some" : "none") << "\n";
  out << "info data dir: " << q(app::data_dir()) << ", version " << q(version()) << ", cpu " << q(cpu_arch());
  for (auto &x : cpu_features()) out << " " << q(x);
  out << "\n";
  return failed;
}

}  // namespace

int main(int argc, char **argv) {
  for (int i = 1; i < argc; ++i)
    if (std::string(argv[i]) == "--snapshots" || std::string(argv[i]) == "--self-test") qputenv("QT_QPA_PLATFORM", "minimal:enable_fonts");
  QApplication app(argc, argv);
#ifdef _WIN32
  QApplication::setFont(QFont("Segoe UI", 10));  // Windows 11's own (Qt's default there is 9 pt)
#endif
  auto args = app.arguments();
  std::string data;
  if (auto i = args.indexOf("--data"); i > 0 && i + 1 < args.size()) data = args[i + 1].toStdString();
  else if (auto *env = std::getenv("LEXIPEEK_DATA")) data = env;
  set_ui_platform("windows");
  if (auto run = platform::cli_runner()) ai::set_cli_runner(run);  // the Qt shell's words (Linux's own later); on macOS it only runs to work on it
  app::set_language(data, QLocale::system().name().left(2).toStdString());
  CoreClient client(data, app::device(platform::ram_mb(), cpu_features(), cpu_arch()));
  if (auto i = args.indexOf("--snapshots"); i > 0 && i + 1 < args.size()) return snapshots(app, client, args[i + 1]);
  if (auto i = args.indexOf("--self-test"); i > 0 && i + 1 < args.size()) return self_test(args[i + 1]);
  // One LexiPeek per user: a second launch hands its arguments (a lexipeek:// link) over and quits.
  if (platform::hand_over(args.mid(1))) return 0;
  Shell shell(client);
  platform::receive([&shell](const QStringList &a) { shell.handle(a); });
  platform::register_scheme();
  if (app::first_run_at_launch(data, true)) shell.open_first_run();
  else if (args.size() > 1) shell.handle(args.mid(1));
  return app.exec();
}
