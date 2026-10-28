#include "settings_window.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QEvent>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QGroupBox>
#include <QRadioButton>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <thread>
#include "dicts_pane.hpp"
#include "fluent.hpp"
#include "packs_pane.hpp"
#include "version.h"
#include "platform.hpp"
#include "ui.hpp"

namespace lexipeek::qt {

std::string version() { return LEXIPEEK_VERSION; }

SettingsState SettingsShell::state() const {
  SettingsState s;
  auto dir = client.data();
  s.trusted = true;  // no permission to read the selection (the core drops Accessibility off macOS)
  s.has_pair = app::has_pair(dir);
  s.login = platform::login();
  s.version = version();
  s.answers = app::answers(dir);
  s.input = platform::hotkey_label(app::settings(dir).input);
  s.checking = checking;
  s.update = update.text, s.update_version = update.version, s.update_url = update.url;
  s.ai_key = !platform::secret(app::ai_provider(dir)).empty();
  s.ai_testing = testing;
  s.ai_test = test;
  s.device = client.device;
  s.downloading = downloading;
  s.progress = progress;
  s.download_error = download_error;
  return s;
}

namespace {


// A row's title and its one-line description.
QWidget *titled(const SettingRow &r) {
  auto *w = new QWidget;
  auto *l = new QVBoxLayout(w);
  l->setContentsMargins(0, 0, 0, 0);
  l->setSpacing(1);
  l->addWidget(ui::text(r.title));
  if (!r.about.empty()) l->addWidget(ui::text(r.about, -1.5, true));
  w->setToolTip(q(r.about));
  return w;
}

// The title on the left, the control on the right.
QWidget *beside(const SettingRow &r, QWidget *control) {
  auto *w = new QWidget;
  auto *l = new QHBoxLayout(w);
  l->setContentsMargins(0, 0, 0, 0);
  l->addWidget(titled(r), 1);
  l->addWidget(control, 0, Qt::AlignVCenter);
  return w;
}

// The title, then the control under it.
QWidget *under(const SettingRow &r, QWidget *control) {
  auto *w = new QWidget;
  auto *l = new QVBoxLayout(w);
  l->setContentsMargins(0, 0, 0, 0);
  l->setSpacing(6);
  l->addWidget(titled(r));
  l->addWidget(control);
  return w;
}

// A letter or a digit with modifiers, as settings.json keeps it; nullopt for anything else.
std::optional<Hotkey> hotkey_of(const QKeySequence &k) {
  if (k.isEmpty()) return std::nullopt;
  auto c = k[0];
  auto key = c.key();
  bool letter = key >= Qt::Key_A && key <= Qt::Key_Z, digit = key >= Qt::Key_0 && key <= Qt::Key_9;
  auto m = c.keyboardModifiers();
  if (!(letter || digit) || !(m & (Qt::AltModifier | Qt::ControlModifier | Qt::MetaModifier))) return std::nullopt;
  Hotkey h;
  h.key = std::string(1, static_cast<char>(key));
  h.option = m & Qt::AltModifier, h.control = m & Qt::ControlModifier, h.shift = m & Qt::ShiftModifier, h.command = m & Qt::MetaModifier;
  return h;
}

QKeySequence sequence(const Hotkey &h) {
  QKeyCombination c(Qt::KeyboardModifiers((h.option ? Qt::AltModifier : Qt::NoModifier) | (h.control ? Qt::ControlModifier : Qt::NoModifier) |
                                          (h.shift ? Qt::ShiftModifier : Qt::NoModifier) | (h.command ? Qt::MetaModifier : Qt::NoModifier)),
                    static_cast<Qt::Key>(h.key.empty() ? 0 : h.key[0]));
  return QKeySequence(c);
}



}  // namespace

// Windows Settings' layout: the sections in a list on the left, the open one's page on the right.
SettingsWindow::SettingsWindow(CoreClient &client) : sh_(client) {
  setWindowTitle(t("settings_more"));
  auto *l = new QHBoxLayout(this);
  l->setContentsMargins(0, 0, 0, 0);
  l->setSpacing(0);
  auto *side = new QWidget;
  auto *sl = new QVBoxLayout(side);
  sl->setContentsMargins(8, 12, 4, 12);
  nav_ = new QListWidget;
  nav_->setObjectName("nav");
  nav_->setFixedWidth(200);
  nav_->setIconSize(QSize(16, 16));
  auto names = settings_sections();
  const fluent::Glyph glyphs[] = {fluent::Glyph::settings, fluent::Glyph::globe, fluent::Glyph::card, fluent::Glyph::robot};
  for (int i = 0; i < 4; ++i) nav_->addItem(new QListWidgetItem(fluent::icon(glyphs[i]), q(names[static_cast<int>(shown_sections[i])])));
  nav_->setCurrentRow(0);
  sl->addWidget(nav_);
  l->addWidget(side);
  auto *right = new QWidget;
  page_ = new QVBoxLayout(right);
  page_->setContentsMargins(20, 16, 24, 16);
  page_->setSpacing(12);
  l->addWidget(right, 1);
  connect(nav_, &QListWidget::currentRowChanged, this, [this](int i) {
    if (i < 0) return;
    section_ = shown_sections[i];
    draw();
  });
  resize(860, 640);
  setMinimumSize(680, 480);
  draw();
}

void SettingsWindow::select(SettingsSection section, int tab) {
  tabs_[section] = tab;
  section_ = section;
  for (int i = 0; i < 4; ++i)
    if (shown_sections[i] == section) nav_->setCurrentRow(i);
  draw();
}

void SettingsWindow::refresh() { draw(); }

// A theme change: drawn again (labels keep the colours they were made with).
void SettingsWindow::changeEvent(QEvent *e) {
  QWidget::changeEvent(e);
  if (e->type() != QEvent::ApplicationPaletteChange) return;
  if (packs_) static_cast<PacksPane *>(packs_)->draw();
  if (dicts_) static_cast<DictsPane *>(dicts_)->draw();
  draw();
}

void SettingsWindow::hotkey_errors(const std::string &lookup, const std::string &input) {
  if (sh_.lookup_error == lookup && sh_.input_error == input) return;
  sh_.lookup_error = lookup, sh_.input_error = input;
  if (isVisible()) draw();
}

void SettingsWindow::after(const std::string &error) {
  error_ = error;
  emit changed();
  auto names = settings_sections();
  for (int i = 0; i < 4; ++i) nav_->item(i)->setText(q(names[static_cast<int>(shown_sections[i])]));
  setWindowTitle(t("settings_more"));
  draw();
}

// Drawn again once the event that changed it is over: a toggle or a button of the page must not delete itself in its
// own signal.
void SettingsWindow::draw() {
  if (pending_) return;
  pending_ = true;
  QMetaObject::invokeMethod(this, [this] {
    pending_ = false;
    draw_now();
  }, Qt::QueuedConnection);
}

void SettingsWindow::draw_now() {
  // The same pane drawn again keeps its place (a toggle, a download's progress).
  auto *old = findChild<QScrollArea *>("rows");
  int scrolled = old && section_ == drawn_section_ && tabs_[section_] == drawn_tab_ ? old->verticalScrollBar()->value() : 0;
  drawn_section_ = section_, drawn_tab_ = tabs_[section_];
  for (auto *pane : {packs_, dicts_})
    if (pane) pane->hide(), pane->setParent(nullptr);
  ui::clear(page_);
  auto page = app::settings_page(sh_.client.data(), section_, tabs_[section_], sh_.state());
  auto *target = page_;
  auto *title = ui::text(page.sections[static_cast<int>(section_)], 8, false, true);
  page_->addWidget(title);
  // A section's sub-tabs: text tabs over its page.
  if (!page.tabs.empty()) {
    QStringList names;
    for (auto &x : page.tabs) names << q(x);
    auto *tabs = new fluent::TabBar(names, page.tab);
    connect(tabs, &fluent::TabBar::picked, this, [this](int i) {
      tabs_[section_] = i;
      QMetaObject::invokeMethod(this, [this] { draw(); }, Qt::QueuedConnection);
    });
    page_->addWidget(tabs);
  }
  if (page.pane == "packs") {
    if (!packs_) {
      auto *p = new PacksPane(sh_.client);
      connect(p, &PacksPane::changed, this, [this] { emit changed(); });
      packs_ = p;
    }
    target->addWidget(packs_, 1);
    packs_->show();
    return;
  }
  if (page.pane == "other") {
    if (!dicts_) dicts_ = new DictsPane(sh_.client);
    static_cast<DictsPane *>(dicts_)->draw();
    target->addWidget(dicts_, 1);
    dicts_->show();
    return;
  }
  // Each row in its card; a heading is a group's title over the cards under it (Windows Settings').
  auto *rows = new QWidget;
  auto *rl = new QVBoxLayout(rows);
  rl->setContentsMargins(0, 4, 8, 8);
  rl->setSpacing(4);
  for (auto &r : page.rows) {
    if (r.kind == SettingRow::Kind::heading) {
      rl->addSpacing(14);
      rl->addWidget(ui::text(r.title, 0, false, true));
      if (!r.about.empty()) rl->addWidget(ui::text(r.about, -1, true));
      rl->addSpacing(2);
      continue;
    }
    rl->addWidget(fluent::card(row(r)));
  }
  if (!error_.empty()) {
    auto *e = ui::text(error_, -1);
    e->setStyleSheet("color: #d0342c");
    rl->addWidget(e);
  }
  rl->addStretch();
  auto *scroll = ui::scroll(rows);
  scroll->setObjectName("rows");
  target->addWidget(scroll, 1);
  ui::keep_scroll(scroll, scrolled);
}

QWidget *SettingsWindow::row(const SettingRow &r) {
  using K = SettingRow::Kind;
  auto dir = sh_.client.data();
  auto key = r.key;
  switch (r.kind) {
    case K::heading: {
      auto *w = new QWidget;
      auto *l = new QVBoxLayout(w);
      l->setContentsMargins(0, 0, 0, 0);
      l->setSpacing(1);
      l->addWidget(ui::text(r.title, 0, false, true));
      if (!r.about.empty()) l->addWidget(ui::text(r.about, -1.5, true));
      return w;
    }
    case K::toggle: {
      auto *box = new fluent::Toggle;
      box->setChecked(r.on);
      box->setEnabled(r.enabled);
      connect(box, &fluent::Toggle::toggled, this, [this, key, dir](bool on) {
        if (key == "open_at_login") platform::set_login(on), on = platform::login();
        after(app::toggle(dir, key, on));
      });
      if (r.edit.empty()) return beside(r, box);
      auto *w = new QWidget;
      auto *l = new QHBoxLayout(w);
      l->setContentsMargins(0, 0, 0, 0);
      auto *pen = new QToolButton;
      pen->setText("✎");
      pen->setAutoRaise(true);
      pen->setToolTip(t("edit_btn"));
      connect(pen, &QToolButton::clicked, this, [this, r] { press(r); });
      l->addWidget(pen);
      l->addWidget(box);
      return beside(r, w);
    }
    case K::choice: {
      auto *c = new QComboBox;
      for (auto &x : r.choices) c->addItem(q(x));
      c->setCurrentIndex(r.selected);
      c->setEnabled(r.enabled);
      connect(c, &QComboBox::activated, this, [this, key, dir](int i) { after(app::choose(dir, key, i, sh_.state())); });
      return beside(r, c);
    }
    case K::hotkey: {
      auto s = app::settings(dir);
      bool input = key == "input";
      auto *w = new QWidget;
      auto *l = new QVBoxLayout(w);
      l->setContentsMargins(0, 0, 0, 0);
      auto *e = new QKeySequenceEdit(sequence(input ? s.input : s.lookup));
      e->setMaximumSequenceLength(1);
      e->setFixedWidth(160);
      connect(e, &QKeySequenceEdit::editingFinished, this, [this, e, input, dir] {
        auto h = hotkey_of(e->keySequence());
        if (!h) return draw();
        auto s = app::settings(dir);
        // The other hotkey's keys: kept as they were, the row says why.
        if (*h == (input ? s.lookup : s.input)) return (input ? sh_.input_error : sh_.lookup_error) = t("hk_conflict").toStdString(), draw();
        (input ? s.input : s.lookup) = *h;
        after(app::save(dir, s));
      });
      l->addWidget(e);
      auto err = input ? sh_.input_error : sh_.lookup_error;
      if (!err.empty()) {
        auto *x = ui::text(err, -1.5);
        x->setStyleSheet("color: #d0342c");
        l->addWidget(x);
      }
      return beside(r, w);
    }
    case K::apps: {
      // Programs where the hotkey does nothing ("chrome.exe"): a list, a field to add, ✕ to remove.
      auto *w = new QWidget;
      auto *l = new QVBoxLayout(w);
      l->setContentsMargins(0, 0, 0, 0);
      auto s = app::settings(dir);
      for (auto &id : s.ignore_apps) {
        auto *row = new QHBoxLayout;
        row->addWidget(ui::text(id), 1);
        auto *x = new QToolButton;
        x->setText("✕");
        x->setAutoRaise(true);
        connect(x, &QToolButton::clicked, this, [this, id, dir] {
          auto s = app::settings(dir);
          std::erase(s.ignore_apps, id);
          after(app::save(dir, s));
        });
        row->addWidget(x);
        l->addLayout(row);
      }
      auto *add = new QLineEdit;
      add->setPlaceholderText("app.exe");
      connect(add, &QLineEdit::returnPressed, this, [this, add, dir] {
        auto id = add->text().trimmed().toLower().toStdString();
        if (id.empty()) return;
        auto s = app::settings(dir);
        if (std::find(s.ignore_apps.begin(), s.ignore_apps.end(), id) == s.ignore_apps.end()) s.ignore_apps.push_back(id);
        after(app::save(dir, s));
      });
      l->addWidget(add);
      return under(r, w);
    }
    case K::button: {
      auto *b = new QPushButton(q(r.value));
      b->setEnabled(r.enabled);
      connect(b, &QPushButton::clicked, this, [this, r] {
        if (r.go_tab >= 0) select(r.go_section, r.go_tab);
        else press(r);
      });
      return beside(r, b);
    }
    case K::status: {
      auto *s = ui::text(r.on ? "✓ " + r.value : r.value, 0, !r.on);
      if (r.on) s->setStyleSheet("color: #2e9d48");
      return beside(r, s);
    }
    case K::link: {
      auto url = q(r.value);
      auto *l = ui::link(url, [url] { QDesktopServices::openUrl(QUrl(url)); });
      return beside(r, l);
    }
    case K::text:
    case K::model: {
      auto *w = new QWidget;
      auto *l = new QHBoxLayout(w);
      l->setContentsMargins(0, 0, 0, 0);
      auto *e = new QLineEdit(q(r.value));
      e->setPlaceholderText(q(r.placeholder));
      connect(e, &QLineEdit::editingFinished, this, [this, e, key, dir, was = r.value] {
        if (e->text().toStdString() != was) after(app::set_text(dir, key, e->text().toStdString()));
      });
      l->addWidget(e, 1);
      if (r.kind == K::model && r.on) {
        auto *choose = new QPushButton(t("model_choose"));
        connect(choose, &QPushButton::clicked, this, [this, key, dir, choose] {
          // The provider's list over the network: off the window's thread.
          choose->setEnabled(false);
          auto k = platform::secret(app::ai_provider(dir));
          sh_.client.run<app::AiModels>(sh_.client.ai, [dir, k] { return app::ai_models(dir, k); }, [this, key, dir](app::AiModels m) {
            if (!m.error.empty()) return after(m.error);
            QStringList names;
            for (auto &x : app::model_titles(m.list)) names << q(x);
            bool ok = false;
            auto picked = QInputDialog::getItem(this, t("model_choose"), {}, names, 0, false, &ok);
            if (ok) after(app::set_text(dir, key, m.list[names.indexOf(picked)].id));
            else draw();
          }, this);
        });
        l->addWidget(choose);
      }
      return under(r, w);
    }
    case K::secret: {
      // The API key: typed (hidden), Save puts it into the OS's store; it is never shown again.
      auto *w = new QWidget;
      auto *l = new QHBoxLayout(w);
      l->setContentsMargins(0, 0, 0, 0);
      auto *e = new QLineEdit;
      e->setEchoMode(QLineEdit::Password);
      e->setPlaceholderText(t("key_t"));
      auto *save = new QPushButton(t("save_key"));
      auto store = [this, e, dir] {
        if (e->text().isEmpty()) return;
        platform::set_secret(app::ai_provider(dir), e->text().toStdString());
        e->clear();
        sh_.test.clear();
        draw();
      };
      connect(e, &QLineEdit::returnPressed, this, store);
      connect(save, &QPushButton::clicked, this, store);
      l->addWidget(e, 1);
      l->addWidget(save);
      if (r.on) {
        auto *remove = new QPushButton(t("remove"));
        connect(remove, &QPushButton::clicked, this, [this, dir] {
          platform::set_secret(app::ai_provider(dir), "");
          draw();
        });
        l->addWidget(remove);
      }
      return under(r, w);
    }
    case K::method: {
      // A translation method (InkPeek's MethodPane): a radio button, what it does, needs, why not here; its download.
      auto *w = new QWidget;
      auto *l = new QVBoxLayout(w);
      l->setContentsMargins(0, 0, 0, 0);
      l->setSpacing(2);
      auto *radio = new QRadioButton(q(r.title));
      radio->setAutoExclusive(false);
      radio->setChecked(r.on);
      radio->setEnabled(r.enabled);
      auto selected = r.selected;
      connect(radio, &QRadioButton::clicked, this, [this, key, dir, selected] { after(app::choose(dir, key, selected, sh_.state())); });
      l->addWidget(radio);
      auto *col = new QVBoxLayout;
      col->setContentsMargins(22, 0, 0, 0);
      col->setSpacing(2);
      col->addWidget(ui::text(r.about, -1, true));
      if (!r.detail.empty()) col->addWidget(ui::text((r.enabled ? "" : "⚠ ") + r.detail, -1, true));
      if (r.progress >= 0) {
        auto *p = new QProgressBar;
        p->setRange(0, 1000);
        p->setValue(static_cast<int>(r.progress * 1000));
        p->setTextVisible(false);
        col->addWidget(p);
      } else if (!r.button.empty()) {
        auto *b = new QPushButton(q(r.button));
        auto method = r.value;
        auto ids = r.ids;
        connect(b, &QPushButton::clicked, this, [this, method, ids] { download(method, ids); });
        col->addWidget(b, 0, Qt::AlignLeft);
      }
      l->addLayout(col);
      return w;
    }
  }
  return titled(r);
}

// A button's action, or ✎ of a prompt.
void SettingsWindow::press(const SettingRow &r) {
  auto dir = sh_.client.data();
  if (!r.edit.empty()) {
    auto e = app::prompt_editor(dir, r.edit);
    QDialog d(this);
    d.setWindowTitle(q(e.title));
    auto *l = new QVBoxLayout(&d);
    if (!e.note.empty()) l->addWidget(ui::text(e.note, -1, true));
    std::vector<QPlainTextEdit *> fields;
    for (auto &f : e.fields) {
      l->addWidget(ui::text(f.label, -1, false, true));
      auto *x = new QPlainTextEdit(q(f.text));
      x->setMinimumHeight(90);
      l->addWidget(x);
      fields.push_back(x);
    }
    auto *buttons = new QDialogButtonBox;
    auto *reset = buttons->addButton(q(e.reset), QDialogButtonBox::ResetRole);
    buttons->addButton(q(e.cancel), QDialogButtonBox::RejectRole);
    buttons->addButton(q(e.save), QDialogButtonBox::AcceptRole);
    l->addWidget(buttons);
    bool cleared = false;
    connect(reset, &QPushButton::clicked, &d, [&] { cleared = true, d.accept(); });
    connect(buttons, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    d.resize(560, 480);
    if (d.exec() != QDialog::Accepted) return;
    std::vector<std::string> ids, texts;
    for (size_t i = 0; i < e.fields.size(); ++i)
      ids.push_back(e.fields[i].id), texts.push_back(cleared ? "" : fields[i]->toPlainText().toStdString());
    return after(app::set_prompts(dir, ids, texts));
  }
  if (r.key == "setup") return emit openFirstRun();
  if (r.key == "ai_reset") return after(app::reset_prompts(dir));
  if (r.key == "updates" && !sh_.checking) {
    sh_.checking = true;
    draw();
    sh_.client.run<UpdateCheck>(sh_.client.ai, [] { return app::check_updates(version()); }, [this](UpdateCheck u) {
      sh_.update = u;
      sh_.checking = false;
      draw();
    }, this);
  }
  if (r.key == "ai_test" && !sh_.testing) {
    sh_.testing = true;
    draw();
    auto k = platform::secret(app::ai_provider(dir));
    sh_.client.run<std::string>(sh_.client.ai, [dir, k] { return app::ai_test(dir, k); }, [this](std::string s) {
      sh_.test = s;
      sh_.testing = false;
      draw();
    }, this);
  }
}

// Languages → Translation: a method's models, progress on its card.
void SettingsWindow::download(const std::string &method, const std::vector<std::string> &ids) {
  if (!sh_.downloading.empty() || ids.empty()) return;
  sh_.downloading = method, sh_.progress = 0, sh_.download_error.clear();
  app::Download dl;
  auto *timer = new QTimer(this);
  connect(timer, &QTimer::timeout, this, [this, dl] {
    auto total = dl.get_total();
    sh_.progress = total > 0 ? static_cast<double>(dl.get_done()) / total : 0;
    draw();
  });
  timer->start(500);
  auto dir = sh_.client.data();
  sh_.client.run<Status>(sh_.client.ai, [dir, ids, dl] { return app::install(dir, ids, dl); }, [this, timer](Status s) {
    timer->deleteLater();
    sh_.downloading.clear();
    sh_.download_error = s.cancelled ? "" : s.error;
    sh_.client.reload();
    draw();
  }, this);
}

AboutWindow::AboutWindow(CoreClient &client) : sh_(client) {
  setWindowTitle(t("menu_about"));
  auto *l = new QVBoxLayout(this);
  tabs_ = new QTabWidget;
  for (auto &x : about_tabs()) {
    auto *page = new QWidget;
    new QVBoxLayout(page);
    tabs_->addTab(page, q(x));
  }
  l->addWidget(tabs_);
  connect(tabs_, &QTabWidget::currentChanged, this, &AboutWindow::refresh);
  setFixedWidth(480);
  refresh();
}

void AboutWindow::refresh() {
  auto *body = tabs_->currentWidget()->layout();
  ui::clear(body);
  auto *rows = new QWidget;
  auto *rl = new QVBoxLayout(rows);
  rl->setSpacing(10);
  for (auto &r : about_rows(sh_.state(), tabs_->currentIndex())) {
    using K = SettingRow::Kind;
    if (r.kind == K::heading) {
      rl->addWidget(ui::text(r.title, 0, false, true));
      if (!r.about.empty()) rl->addWidget(ui::text(r.about, -1.5, true));
    } else if (r.kind == K::link) {
      auto url = q(r.value);
      rl->addWidget(ui::link(q(r.title), [url] { QDesktopServices::openUrl(QUrl(url)); }), 0, Qt::AlignLeft);
    } else if (r.kind == K::button) {
      auto *b = new QPushButton(q(r.value));
      b->setEnabled(r.enabled && !sh_.checking);
      connect(b, &QPushButton::clicked, this, [this] {
        sh_.checking = true;
        refresh();
        sh_.client.run<UpdateCheck>(sh_.client.ai, [] { return app::check_updates(version()); }, [this](UpdateCheck u) {
          sh_.update = u;
          sh_.checking = false;
          refresh();
        }, this);
      });
      auto *w = new QHBoxLayout;
      w->addWidget(titled(r), 1);
      w->addWidget(b);
      rl->addLayout(w);
    } else {
      auto *w = new QHBoxLayout;
      w->addWidget(ui::text(r.title), 1);
      w->addWidget(ui::text(r.value, 0, true));
      rl->addLayout(w);
      if (!r.about.empty()) rl->addWidget(ui::text(r.about, -1.5, true));
    }
  }
  rl->addStretch();
  body->addWidget(rows);
  adjustSize();
}

}  // namespace lexipeek::qt
