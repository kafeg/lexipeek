#include "settings_window.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
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
#include <QTabBar>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <thread>
#include "packs_pane.hpp"
#include "platform.hpp"

namespace lexipeek::qt {

std::string version() { return LEXIPEEK_VERSION; }

SettingsState SettingsShell::state() const {
  SettingsState s;
  auto data = client.data();
  s.trusted = true;  // Windows reads the selection without a permission
  s.has_pair = app::has_pair(data);
  s.login = platform::login();
  s.version = version();
  s.answers = app::answers(data);
  s.input = platform::hotkey_label(app::settings(data).input);
  s.checking = checking;
  s.update = update.text, s.update_version = update.version, s.update_url = update.url;
  s.ai_key = !platform::secret(app::ai_provider(data)).empty();
  s.ai_testing = testing;
  s.ai_test = test;
  s.device = client.device;
  s.downloading = downloading;
  s.progress = progress;
  s.download_error = download_error;
  return s;
}

namespace {

QLabel *text_label(const std::string &s, int delta = 0, bool grey = false, bool bold = false) {
  auto *l = new QLabel(q(s));
  l->setWordWrap(true);
  auto f = l->font();
  f.setPointSizeF(f.pointSizeF() + delta);
  f.setBold(bold);
  l->setFont(f);
  if (grey) {
    auto p = l->palette();
    p.setColor(QPalette::WindowText, p.color(QPalette::PlaceholderText));
    l->setPalette(p);
  }
  return l;
}

// A row's title and its one-line description.
QWidget *titled(const SettingRow &r) {
  auto *w = new QWidget;
  auto *l = new QVBoxLayout(w);
  l->setContentsMargins(0, 0, 0, 0);
  l->setSpacing(1);
  l->addWidget(text_label(r.title));
  if (!r.about.empty()) l->addWidget(text_label(r.about, -2, true));
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

// A method's card (InkPeek's MethodPane): picked by a click.
class MethodCard : public QFrame {
 public:
  std::function<void()> pick;
  void mousePressEvent(QMouseEvent *) override {
    if (pick) pick();
  }
};

void clear(QLayout *l) {
  while (auto *item = l->takeAt(0)) {
    if (item->layout()) clear(item->layout());
    delete item->widget();
    delete item;
  }
}

}  // namespace

SettingsWindow::SettingsWindow(CoreClient &client) : sh_(client) {
  setWindowTitle(t("settings_more"));
  auto *l = new QVBoxLayout(this);
  l->setContentsMargins(0, 0, 0, 0);
  l->setSpacing(0);
  sections_ = new QTabBar;
  sections_->setExpanding(false);
  sections_->setDrawBase(true);
  auto names = settings_sections();
  for (auto s : shown_sections) sections_->addTab(q(names[static_cast<int>(s)]));
  auto *bar = new QHBoxLayout;
  bar->setContentsMargins(12, 10, 12, 0);
  bar->addStretch();
  bar->addWidget(sections_);
  bar->addStretch();
  l->addLayout(bar);
  body_ = new QVBoxLayout;
  l->addLayout(body_, 1);
  connect(sections_, &QTabBar::currentChanged, this, [this](int i) {
    section_ = shown_sections[i];
    draw();
  });
  setFixedSize(600, 580);
  draw();
}

void SettingsWindow::select(SettingsSection section, int tab) {
  tabs_[section] = tab;
  section_ = section;
  for (int i = 0; i < 4; ++i)
    if (shown_sections[i] == section) sections_->setCurrentIndex(i);
  draw();
}

void SettingsWindow::refresh() { draw(); }

void SettingsWindow::hotkey_errors(const std::string &lookup, const std::string &input) {
  if (sh_.lookup_error == lookup && sh_.input_error == input) return;
  sh_.lookup_error = lookup, sh_.input_error = input;
  if (isVisible()) draw();
}

void SettingsWindow::after(const std::string &error) {
  error_ = error;
  emit changed();
  auto names = settings_sections();
  for (int i = 0; i < 4; ++i) sections_->setTabText(i, q(names[static_cast<int>(shown_sections[i])]));
  setWindowTitle(t("settings_more"));
  draw();
}

void SettingsWindow::draw() {
  if (packs_) body_->removeWidget(packs_), packs_->hide();
  clear(body_);
  auto page = app::settings_page(sh_.client.data(), section_, tabs_[section_], sh_.state());
  if (!page.tabs.empty()) {
    auto *tabs = new QTabBar;
    for (auto &x : page.tabs) tabs->addTab(q(x));
    tabs->setCurrentIndex(page.tab);
    connect(tabs, &QTabBar::currentChanged, this, [this](int i) {
      tabs_[section_] = i;
      QMetaObject::invokeMethod(this, [this] { draw(); }, Qt::QueuedConnection);
    });
    auto *row = new QHBoxLayout;
    row->setContentsMargins(12, 10, 12, 4);
    row->addStretch();
    row->addWidget(tabs);
    row->addStretch();
    body_->addLayout(row);
  }
  if (page.pane == "packs") {
    if (!packs_) {
      auto *p = new PacksPane(sh_.client);
      connect(p, &PacksPane::changed, this, [this] { emit changed(); });
      packs_ = p;
    }
    body_->addWidget(packs_, 1);
    packs_->show();
    return;
  }
  if (page.pane == "other") {
    body_->addWidget(text_label("…", 0, true), 1, Qt::AlignCenter);
    return;
  }
  auto *rows = new QWidget;
  auto *rl = new QVBoxLayout(rows);
  rl->setContentsMargins(20, 12, 20, 16);
  rl->setSpacing(12);
  for (auto &r : page.rows) {
    if (r.kind == SettingRow::Kind::button && r.key == "access") continue;  // macOS only
    if (r.kind == SettingRow::Kind::status && r.key == "access") continue;
    if (r.kind == SettingRow::Kind::heading) rl->addSpacing(6);
    rl->addWidget(row(r));
  }
  if (!error_.empty()) {
    auto *e = text_label(error_, -1);
    e->setStyleSheet("color: #d0342c");
    rl->addWidget(e);
  }
  rl->addStretch();
  auto *scroll = new QScrollArea;
  scroll->setWidget(rows);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  body_->addWidget(scroll, 1);
}

QWidget *SettingsWindow::row(const SettingRow &r) {
  using K = SettingRow::Kind;
  auto data = sh_.client.data();
  auto key = r.key;
  switch (r.kind) {
    case K::heading: {
      auto *w = new QWidget;
      auto *l = new QVBoxLayout(w);
      l->setContentsMargins(0, 0, 0, 0);
      l->setSpacing(1);
      l->addWidget(text_label(r.title, 0, false, true));
      if (!r.about.empty()) l->addWidget(text_label(r.about, -2, true));
      return w;
    }
    case K::toggle: {
      auto *box = new QCheckBox;
      box->setChecked(r.on);
      box->setEnabled(r.enabled);
      connect(box, &QCheckBox::toggled, this, [this, key, data](bool on) {
        if (key == "open_at_login") platform::set_login(on), on = platform::login();
        after(app::toggle(data, key, on));
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
      connect(c, &QComboBox::activated, this, [this, key, data](int i) { after(app::choose(data, key, i, sh_.state())); });
      return beside(r, c);
    }
    case K::hotkey: {
      auto s = app::settings(data);
      bool input = key == "input";
      auto *w = new QWidget;
      auto *l = new QVBoxLayout(w);
      l->setContentsMargins(0, 0, 0, 0);
      auto *e = new QKeySequenceEdit(sequence(input ? s.input : s.lookup));
      e->setMaximumSequenceLength(1);
      e->setFixedWidth(160);
      connect(e, &QKeySequenceEdit::editingFinished, this, [this, e, input, data] {
        auto h = hotkey_of(e->keySequence());
        if (!h) return draw();
        auto s = app::settings(data);
        (input ? s.input : s.lookup) = *h;
        after(app::save(data, s));
      });
      l->addWidget(e);
      auto err = input ? sh_.input_error : sh_.lookup_error;
      if (!err.empty()) {
        auto *x = text_label(err, -2);
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
      auto s = app::settings(data);
      for (auto &id : s.ignore_apps) {
        auto *row = new QHBoxLayout;
        row->addWidget(text_label(id), 1);
        auto *x = new QToolButton;
        x->setText("✕");
        x->setAutoRaise(true);
        connect(x, &QToolButton::clicked, this, [this, id, data] {
          auto s = app::settings(data);
          std::erase(s.ignore_apps, id);
          after(app::save(data, s));
        });
        row->addWidget(x);
        l->addLayout(row);
      }
      auto *add = new QLineEdit;
      add->setPlaceholderText("app.exe");
      connect(add, &QLineEdit::returnPressed, this, [this, add, data] {
        auto id = add->text().trimmed().toLower().toStdString();
        if (id.empty()) return;
        auto s = app::settings(data);
        if (std::find(s.ignore_apps.begin(), s.ignore_apps.end(), id) == s.ignore_apps.end()) s.ignore_apps.push_back(id);
        after(app::save(data, s));
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
      auto *s = text_label(r.on ? "✓ " + r.value : r.value, 0, !r.on);
      if (r.on) s->setStyleSheet("color: #2e9d48");
      return beside(r, s);
    }
    case K::link: {
      auto *b = new QToolButton;
      b->setText("↗");
      b->setAutoRaise(true);
      b->setToolTip(q(r.value));
      auto url = q(r.value);
      connect(b, &QToolButton::clicked, this, [url] { QDesktopServices::openUrl(QUrl(url)); });
      return beside(r, b);
    }
    case K::text:
    case K::model: {
      auto *w = new QWidget;
      auto *l = new QHBoxLayout(w);
      l->setContentsMargins(0, 0, 0, 0);
      auto *e = new QLineEdit(q(r.value));
      e->setPlaceholderText(q(r.placeholder));
      connect(e, &QLineEdit::editingFinished, this, [this, e, key, data, was = r.value] {
        if (e->text().toStdString() != was) after(app::set_text(data, key, e->text().toStdString()));
      });
      l->addWidget(e, 1);
      if (r.kind == K::model && r.on) {
        auto *choose = new QPushButton(t("model_choose"));
        connect(choose, &QPushButton::clicked, this, [this, key, data] {
          auto k = platform::secret(app::ai_provider(data));
          auto m = app::ai_models(data, k);
          if (!m.error.empty()) return after(m.error);
          QStringList names;
          for (auto &x : app::model_titles(m.list)) names << q(x);
          bool ok = false;
          auto picked = QInputDialog::getItem(this, t("model_choose"), {}, names, 0, false, &ok);
          if (ok) after(app::set_text(data, key, m.list[names.indexOf(picked)].id));
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
      auto store = [this, e, data] {
        if (e->text().isEmpty()) return;
        platform::set_secret(app::ai_provider(data), e->text().toStdString());
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
        connect(remove, &QPushButton::clicked, this, [this, data] {
          platform::set_secret(app::ai_provider(data), "");
          draw();
        });
        l->addWidget(remove);
      }
      return under(r, w);
    }
    case K::method: {
      auto *card = new MethodCard;
      card->setFrameShape(QFrame::StyledPanel);
      card->setCursor(r.enabled ? Qt::PointingHandCursor : Qt::ArrowCursor);
      auto selected = r.selected;
      if (r.enabled) card->pick = [this, key, data, selected] { after(app::choose(data, key, selected, sh_.state())); };
      auto *l = new QHBoxLayout(card);
      auto *dot = text_label(r.on ? "◉" : "○", 2);
      l->addWidget(dot, 0, Qt::AlignTop);
      auto *col = new QVBoxLayout;
      col->setSpacing(3);
      col->addWidget(text_label(r.title, 0, false, r.on));
      col->addWidget(text_label(r.about, -2, true));
      if (!r.detail.empty()) col->addWidget(text_label((r.enabled ? "" : "⚠ ") + r.detail, -2, true));
      if (r.progress >= 0) {
        auto *p = new QProgressBar;
        p->setRange(0, 1000);
        p->setValue(static_cast<int>(r.progress * 1000));
        p->setTextVisible(false);
        p->setFixedHeight(6);
        col->addWidget(p);
      } else if (!r.button.empty()) {
        auto *b = new QPushButton(q(r.button));
        auto method = r.value;
        auto ids = r.ids;
        connect(b, &QPushButton::clicked, this, [this, method, ids] { download(method, ids); });
        col->addWidget(b, 0, Qt::AlignLeft);
      }
      l->addLayout(col, 1);
      if (!r.enabled) card->setEnabled(false);
      return card;
    }
  }
  return titled(r);
}

// A button's action, or ✎ of a prompt.
void SettingsWindow::press(const SettingRow &r) {
  auto data = sh_.client.data();
  if (!r.edit.empty()) {
    auto e = app::prompt_editor(data, r.edit);
    QDialog d(this);
    d.setWindowTitle(q(e.title));
    auto *l = new QVBoxLayout(&d);
    if (!e.note.empty()) l->addWidget(text_label(e.note, -1, true));
    std::vector<QPlainTextEdit *> fields;
    for (auto &f : e.fields) {
      l->addWidget(text_label(f.label, -1, false, true));
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
    return after(app::set_prompts(data, ids, texts));
  }
  if (r.key == "setup") return emit openFirstRun();
  if (r.key == "ai_reset") return after(app::reset_prompts(data));
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
    auto k = platform::secret(app::ai_provider(data));
    sh_.client.run<std::string>(sh_.client.ai, [data, k] { return app::ai_test(data, k); }, [this](std::string s) {
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
  auto data = sh_.client.data();
  sh_.client.run<Status>(sh_.client.ai, [data, ids, dl] { return app::install(data, ids, dl); }, [this, timer](Status s) {
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
  tabs_ = new QTabBar;
  for (auto &x : about_tabs()) tabs_->addTab(q(x));
  auto *row = new QHBoxLayout;
  row->addStretch();
  row->addWidget(tabs_);
  row->addStretch();
  l->addLayout(row);
  body_ = new QVBoxLayout;
  l->addLayout(body_, 1);
  connect(tabs_, &QTabBar::currentChanged, this, &AboutWindow::refresh);
  setFixedWidth(460);
  refresh();
}

void AboutWindow::refresh() {
  clear(body_);
  auto *rows = new QWidget;
  auto *rl = new QVBoxLayout(rows);
  rl->setSpacing(10);
  for (auto &r : about_rows(sh_.state(), tabs_->currentIndex())) {
    using K = SettingRow::Kind;
    if (r.kind == K::heading) {
      rl->addWidget(text_label(r.title, 0, false, true));
      if (!r.about.empty()) rl->addWidget(text_label(r.about, -2, true));
    } else if (r.kind == K::link) {
      auto *b = new QToolButton;
      b->setText(q(r.title) + " ↗");
      b->setAutoRaise(true);
      b->setStyleSheet("QToolButton { color: palette(link); border: none; padding: 0; }");
      auto url = q(r.value);
      connect(b, &QToolButton::clicked, this, [url] { QDesktopServices::openUrl(QUrl(url)); });
      rl->addWidget(b, 0, Qt::AlignLeft);
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
      w->addWidget(text_label(r.title), 1);
      w->addWidget(text_label(r.value, 0, true));
      rl->addLayout(w);
      if (!r.about.empty()) rl->addWidget(text_label(r.about, -2, true));
    }
  }
  rl->addStretch();
  body_->addWidget(rows, 1);
  adjustSize();
}

}  // namespace lexipeek::qt
