#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QMessageBox>
#include <QPushButton>
#include <QProcess>
#include <QFile>
#include <QThread>
#include <QTextStream>
#include <QLocale>
#include <QStyle>
#include <QStyleHints>
#include <QStyleFactory>
#include <QTimer>
#include <cstdlib>
#include <sstream>
#include "card.hpp"
#include "first_run.hpp"
#include "fluent.hpp"
#include "ui.hpp"
#include <QImage>
#include <QPainter>
#include "main_window.hpp"
#include "platform.hpp"
#include "settings_window.hpp"
#include "shell.hpp"
#include "snapshots.hpp"

using namespace lexipeek;
using namespace lexipeek::qt;

namespace {

// `--self-test <file>`: the OS parts that need no desktop (the shared layer's checks) and the Qt shell's icons, one
// line each "ok|FAIL what", into file; the exit code the failures' count.
int self_test(const QString &path) {
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return 1;
  std::ostringstream lines;
  int failed = os::self_test(lines);
  QTextStream out(&f);
  out << q(lines.str());
  auto check = [&](bool ok, const QString &what) {
    out << (ok ? "ok   " : "FAIL ") << what << "\n";
    failed += !ok;
  };
  {
    // The system's icon glyphs: the font found, and an icon drawn from it leaves pixels.
    auto img = fluent::icon(fluent::Glyph::copy).pixmap(40, 40).toImage();
    int ink = 0;
    for (int y = 0; y < img.height(); ++y)
      for (int x = 0; x < img.width(); ++x) ink += qAlpha(img.pixel(x, y)) > 0;
    check(!fluent::icon_font().isEmpty() && ink > 20, "icons: " + fluent::icon_font() + ", the copy icon's pixels " + QString::number(ink));
  }
  out << "info front app: " << q(platform::front_app()) << ", selection: " << (platform::selection() ? "some" : "none") << "\n";
  out << "info data dir: " << q(app::data_dir()) << ", version " << q(version()) << ", cpu " << q(cpu_arch());
  for (auto &x : cpu_features()) out << " " << q(x);
  out << "\n";
  return failed;
}

}  // namespace

int run(int argc, char **argv) {
#ifndef _WIN32
  for (int i = 1; i < argc; ++i)
    if (std::string(argv[i]) == "--snapshots" || std::string(argv[i]) == "--self-test") qputenv("QT_QPA_PLATFORM", "minimal:enable_fonts");
#endif
  QApplication app(argc, argv);
#ifdef _WIN32
  QApplication::setFont(QFont("Segoe UI", 10));  // Windows 11's own (Qt's default there is 9 pt)
#endif
  auto args = app.arguments();
  std::string data;
  if (auto i = args.indexOf("--data"); i > 0 && i + 1 < args.size()) data = args[i + 1].toStdString();
  else if (auto *env = std::getenv("LEXIPEEK_DATA")) data = env;
  set_ui_platform("windows");  // the Qt shell's words (Linux's own later); on macOS it only runs to work on it
  if (auto run = platform::cli_runner()) ai::set_cli_runner(run);
  // The system's UI language ("ru" of "ru-RU"), not its formats' locale.
  auto ui = QLocale::system().uiLanguages();
  app::set_language(data, (ui.isEmpty() ? QLocale::system().name() : ui.first()).left(2).toStdString());
  // Settings → Apps → LexiPeek → Uninstall (--yes: without asking): a running copy quits first.
  if (args.contains("--uninstall")) {
    if (args.contains("--yes") || ui::ask(nullptr, t("uninst_q", app::data_dir()), {}, t("delete"), t("cancel"))) {
      platform::quit_running();
      platform::uninstall();
    }
    return 0;
  }
  // --install: installed for this user, nothing asked; started by itself from elsewhere (a download): asked.
  auto install = [&] {
    platform::quit_running();  // an older copy running gives way
    return platform::install(version(), false);
  };
  if (args.contains("--install")) {
    auto e = install();
    QTextStream(stderr) << (e.empty() ? q(platform::installed_program()) : q(e)) << "\n";
    return e.empty() ? 0 : 1;
  }
  bool tool = args.contains("--snapshots") || args.contains("--self-test") || args.contains("--probe-selection");
  if (!tool && args.size() == 1 && !platform::installed_here() && ui::ask(nullptr, t("inst_q_t"), t("inst_q_d"), t("inst_btn"), t("inst_run"))) {
    if (auto e = install(); !e.empty()) QMessageBox::warning(nullptr, "LexiPeek", q(e));
    else return QProcess::startDetached(q(platform::installed_program()), {}) ? 0 : 1;
  }
  // One LexiPeek per user: a second launch hands its arguments (a lexipeek:// link) over and quits.
  if (!tool && platform::hand_over(args.mid(1))) return 0;
  platform::remove_old();  // left by an update
  CoreClient client(data, app::device(platform::ram_mb(), cpu_features(), cpu_arch()));
  auto opt = [&](const char *name) {
    auto i = args.indexOf(name);
    return i > 0 && i + 1 < args.size() ? args[i + 1] : QString();
  };
  if (auto out = opt("--snapshots"); !out.isEmpty())
    return snapshots(app, client, {out, opt("--cases"), opt("--ai-fixtures"), opt("--firstrun-from"), !args.contains("--no-settings"),
                                   opt("--suffix")});
  if (auto i = args.indexOf("--self-test"); i > 0 && i + 1 < args.size()) return self_test(args[i + 1]);
  // --probe-selection <file>: what UI Automation sees at the focus, after 3 s (time to focus the app to look at).
  if (auto i = args.indexOf("--probe-selection"); i > 0 && i + 1 < args.size()) {
    QThread::sleep(3);
    QFile f(args[i + 1]);
    if (f.open(QIODevice::WriteOnly)) f.write(QByteArray::fromStdString(platform::probe_selection() + "front app: " + platform::front_app() + "\n"));
    return 0;
  }
  int code = 0;
  {
    Shell shell(client);
    platform::receive([&shell](const QStringList &a) {
      if (a.contains("--quit")) return QApplication::quit();  // a newer copy installed, or uninstalled
      shell.handle(a);
    });
    platform::register_scheme();
    if (app::first_run_at_launch(data, true)) shell.open_first_run();
    if (std::any_of(args.begin(), args.end(), [](const QString &a) { return a.startsWith("lexipeek:", Qt::CaseInsensitive); }))
      shell.handle(args.mid(1));
    code = app.exec();
  }
  return code;  // the tray icon gone with the shell
}

// Every way out without static destructors: a translation still running is not waited for, and llama.cpp's Metal
// device asserts at exit while a model it kept is alive.
int main(int argc, char **argv) { app::leave(run(argc, argv)); }
