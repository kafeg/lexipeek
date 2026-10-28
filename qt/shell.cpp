#include "shell.hpp"
#include <QApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QStyleHints>
#include <QUrl>
#include <QUrlQuery>
#include <algorithm>

namespace lexipeek::qt {

namespace {
constexpr int translate_key = 1, input_key = 2;

// In the middle of the screen with the cursor (where it already is when it is open).
void present(QWidget *w) {
  if (!w->isVisible()) {
    auto *s = QGuiApplication::screenAt(QCursor::pos());
    auto g = (s ? s : QGuiApplication::primaryScreen())->availableGeometry();
    // A window with its own size keeps it (the LexiPeek window, Settings); the others fit their content.
    if (w->sizeHint().isValid() && w->size().width() < w->sizeHint().width()) w->adjustSize();
    w->move(g.center() - QPoint(w->width() / 2, w->height() / 2));
  }
  w->show();
  w->raise();
  w->activateWindow();
}
}  // namespace

Shell::Shell(CoreClient &client) : c_(client), popup_(client) {
  QApplication::setQuitOnLastWindowClosed(false);
  QApplication::setWindowIcon(QIcon(":/icons/icon_256.png"));
  tray_.setIcon(QIcon(":/icons/icon_32.png"));
  tray_.setToolTip("LexiPeek");
  tray_.setContextMenu(&menu_);
  connect(&menu_, &QMenu::aboutToShow, this, &Shell::fill_menu);
  connect(&tray_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason r) {
    if (r == QSystemTrayIcon::Trigger || r == QSystemTrayIcon::DoubleClick) open_main();
  });
  // The AI key per request, from the OS's store (never kept by the core).
  auto key = [this] { return platform::secret(app::ai_provider(c_.data())); };
  popup_.model.key = key;
  connect(popup_.card, &CardWidget::openPane, this, &Shell::open_settings);
  connect(&icon_, &SelectionIcon::open, this, [this](const platform::Selected &s) { popup_.show_card(s.text, Source::hotkey, s.bounds, false); });
  tray_.show();
  apply();
}

void Shell::apply() {
  s_ = app::settings(c_.data());
  popup_.pin_moved = s_.pin_moved;
  icon_.set_enabled(s_.selection_icon, s_.ignore_apps);
  QGuiApplication::styleHints()->setColorScheme(s_.theme == "light"  ? Qt::ColorScheme::Light
                                                : s_.theme == "dark" ? Qt::ColorScheme::Dark
                                                                     : Qt::ColorScheme::Unknown);
  register_hotkeys();
  c_.configure();
}

void Shell::register_hotkeys() {
  bool a = platform::register_hotkey(translate_key, s_.lookup, [this] { translate_selection(); });
  bool b = platform::register_hotkey(input_key, s_.input, [this] { type_text(); });
  if (settings_) settings_->hotkey_errors(a ? "" : t("hk_taken").toStdString(), b ? "" : t("hk_taken").toStdString());
  if (main_) main_->lookup_key = platform::hotkey_label(s_.lookup), main_->input_key = platform::hotkey_label(s_.input);
}

// With a card open: another selection opens its card at once; the same one or none closes it.
void Shell::translate_selection() {
  auto app = platform::front_app();
  bool off = std::find(s_.ignore_apps.begin(), s_.ignore_apps.end(), app) != s_.ignore_apps.end();
  if (off) return popup_.close_card();
  bool open = popup_.open();
  auto s = platform::selection();
  if (open && (!s || s->text == popup_.shown)) return popup_.close_card();
  if (s) return popup_.show_card(s->text, Source::hotkey, s->bounds, false);
  // Nothing read: say which app, type meanwhile.
  popup_.show_card(std::nullopt, Source::input, {}, true);
  if (!app.empty()) {
    popup_.model.unread = app;
    popup_.card->rebuild();
  }
}

void Shell::type_text() {
  if (popup_.open() && popup_.model.input) return popup_.close_card();
  popup_.show_card(std::nullopt, Source::input, {}, true);
}

void Shell::open_main(int page) {
  if (!main_) {
    main_ = new MainWindow(c_);
    main_->card.key = popup_.model.key;
    main_->lookup_key = platform::hotkey_label(s_.lookup), main_->input_key = platform::hotkey_label(s_.input);
    connect(main_, &MainWindow::openSettings, this, &Shell::open_settings);
    connect(main_, &MainWindow::openAbout, this, &Shell::open_about);
    connect(&popup_.model, &CardModel::changed, main_, &MainWindow::refresh);
  }
  if (page >= 0) main_->show_page(page);
  main_->refresh();
  present(main_);
}

void Shell::open_settings(int section, int tab) {
  if (!settings_) {
    settings_ = new SettingsWindow(c_);
    connect(settings_, &SettingsWindow::changed, this, &Shell::apply);
    connect(settings_, &SettingsWindow::openFirstRun, this, &Shell::open_first_run);
  }
  if (section >= 0) settings_->select(static_cast<SettingsSection>(section), tab);
  settings_->refresh();
  present(settings_);
}

// The first pair and its download, the hotkey; closed, it opens at launch only while no pair is installed.
void Shell::open_first_run() {
  if (!first_) {
    first_ = new FirstRunWindow(c_);
    connect(first_, &FirstRunWindow::finished, this, &Shell::apply);
  }
  first_->state.hotkey = platform::hotkey_label(s_.lookup);
  first_->state.input = platform::hotkey_label(s_.input);
  first_->refresh();
  present(first_);
  first_->start();
}

void Shell::open_about() {
  if (!about_) about_ = new AboutWindow(c_);
  about_->refresh();
  present(about_);
}

// lexipeek://lookup?text=… (the card with it) and lexipeek://input (the card to type in); a launch without one opens the window.
void Shell::handle(const QStringList &args) {
  for (auto &a : args) {
    QUrl url(a);
    if (url.scheme().toLower() != "lexipeek") continue;
    auto host = url.host().toLower();
    if (host == "input") return popup_.show_card(std::nullopt, Source::input, {}, true);
    auto text = QUrlQuery(url).queryItemValue("text", QUrl::FullyDecoded).trimmed();
    if (host == "lookup" && !text.isEmpty()) return popup_.show_card(text.toStdString(), Source::url, {}, false);
    return;
  }
  open_main();
}

// Built on every open: Recents and the memory line stay current.
void Shell::fill_menu() {
  menu_.clear();
  menu_.addAction(t("menu_open"), this, [this] { open_main(); });
  auto *recents = menu_.addMenu(t("recents"));
  for (auto &r : c_.engine.recents(10)) {
    auto title = r.meaning.empty() ? r.text : r.text + " — " + r.meaning;
    auto text = r.text;
    recents->addAction(QFontMetrics(menu_.font()).elidedText(q(title), Qt::ElideRight, 360),
                       this, [this, text] { popup_.show_card(text, Source::input, {}, false); });
  }
  if (recents->isEmpty()) recents->addAction(t("nothing_yet"))->setEnabled(false);
  menu_.addSeparator();
  auto m = c_.engine.memory();
  menu_.addAction(q(m.line))->setEnabled(false);
  if (m.held) {
    menu_.addAction(q(m.detail))->setEnabled(false);
    menu_.addAction(q(m.unload), this, [this] { c_.engine.release(); });
  }
  menu_.addSeparator();
  menu_.addAction(t("menu_answer", app::language(s_.answer)))->setEnabled(false);
  menu_.addSeparator();
  menu_.addAction(t("settings_more"), this, [this] { open_settings(-1, 0); });
  menu_.addAction(t("menu_about"), this, &Shell::open_about);
  menu_.addAction(t("quit"), qApp, &QApplication::quit);
}

}  // namespace lexipeek::qt
