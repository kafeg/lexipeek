#include "main_window.hpp"
#include <QDate>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace lexipeek::qt {

namespace {

QLabel *grey_label(const QString &s, int delta = 0) {
  auto *l = new QLabel(s);
  l->setWordWrap(true);
  auto f = l->font();
  f.setPointSizeF(f.pointSizeF() + delta);
  l->setFont(f);
  auto p = l->palette();
  p.setColor(QPalette::WindowText, p.color(QPalette::PlaceholderText));
  l->setPalette(p);
  return l;
}

void clear(QLayout *l) {
  while (auto *item = l->takeAt(0)) {
    if (item->layout()) clear(item->layout());
    delete item->widget();
    delete item;
  }
}

QString downloads(const QString &name) {
  return QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) + "/" + name;
}

QString today() { return QDate::currentDate().toString("yyyy-MM-dd"); }

}  // namespace

MainWindow::MainWindow(CoreClient &client) : card(client), c_(client) {
  setWindowTitle("LexiPeek");
  auto *row = new QHBoxLayout(this);
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(0);

  auto *side = new QWidget;
  side->setFixedWidth(210);
  auto *sl = new QVBoxLayout(side);
  sl->setContentsMargins(8, 8, 8, 12);
  side_ = new QListWidget;
  side_->setFrameShape(QFrame::NoFrame);
  side_->setStyleSheet("QListWidget { background: transparent; } QListWidget::item { padding: 6px; }");
  for (int i = 0; i < 5; ++i) side_->addItem("");
  sl->addWidget(side_, 1);
  memory_ = grey_label("", -2);
  sl->addWidget(memory_);
  auto *buttons = new QHBoxLayout;
  auto *settings = new QToolButton;
  settings->setText(t("settings_more"));
  settings->setAutoRaise(true);
  connect(settings, &QToolButton::clicked, this, [this] { emit openSettings(-1, 0); });
  auto *about = new QToolButton;
  about->setText("ⓘ");
  about->setToolTip(t("menu_about"));
  about->setAutoRaise(true);
  connect(about, &QToolButton::clicked, this, [this] { emit openAbout(); });
  buttons->addWidget(settings);
  buttons->addStretch();
  buttons->addWidget(about);
  sl->addLayout(buttons);
  row->addWidget(side);

  auto *line = new QFrame;
  line->setFrameShape(QFrame::VLine);
  line->setFrameShadow(QFrame::Plain);
  line->setStyleSheet("color: palette(mid);");
  row->addWidget(line);

  pages_ = new QStackedWidget;
  pages_->addWidget(lookup_page());
  pages_->addWidget(notes_page());
  help_ = new QWidget;
  auto *hs = new QScrollArea;
  hs->setWidget(help_);
  hs->setWidgetResizable(true);
  hs->setFrameShape(QFrame::NoFrame);
  pages_->addWidget(hs);
  row->addWidget(pages_, 1);

  connect(side_, &QListWidget::currentRowChanged, this, [this](int r) {
    if (r == lookup) pages_->setCurrentIndex(0);
    else if (r == help) draw_help(), pages_->setCurrentIndex(2);
    else if (r >= 0) tab_ = static_cast<NotesTab>(r - words), page_ = 0, draw_notes(), pages_->setCurrentIndex(1);
  });
  auto *timer = new QTimer(this);
  connect(timer, &QTimer::timeout, this, &MainWindow::refresh);
  timer->start(15000);
  resize(860, 560);
  side_->setCurrentRow(0);
  refresh();
}

QWidget *MainWindow::lookup_page() {
  auto *w = new QWidget;
  auto *l = new QVBoxLayout(w);
  l->setContentsMargins(0, 0, 0, 0);
  l->setSpacing(0);
  query_ = new QLineEdit;
  query_->setPlaceholderText(t("card_input_hint"));
  query_->setMinimumHeight(30);
  auto *top = new QWidget;
  auto *tl = new QVBoxLayout(top);
  tl->setContentsMargins(14, 14, 14, 14);
  tl->addWidget(query_);
  l->addWidget(top);
  auto *cw = new CardWidget(card, true);
  connect(cw, &CardWidget::openPane, this, [this](int s, int tab) { emit openSettings(s, tab); });
  auto *body = new QWidget;
  auto *bl = new QVBoxLayout(body);
  bl->setContentsMargins(0, 0, 0, 0);
  hint_ = grey_label("");
  hint_->setAlignment(Qt::AlignCenter);
  hint_->setMaximumWidth(420);
  bl->addWidget(cw, 1);
  bl->addWidget(hint_, 1, Qt::AlignCenter);
  l->addWidget(body, 1);
  auto toggle = [this, cw] {
    bool shown = card.has_card() || card.looking();
    cw->setVisible(shown);
    hint_->setVisible(!shown);
    hint_->setText(t("win_hint", lookup_key, input_key));
  };
  connect(&card, &CardModel::changed, this, toggle);
  toggle();
  connect(query_, &QLineEdit::returnPressed, this, [this] { look(query_->text()); });
  return w;
}

QWidget *MainWindow::notes_page() {
  notes_ = new QWidget;
  new QVBoxLayout(notes_);
  return notes_;
}

void MainWindow::show_page(int page) { side_->setCurrentRow(page); }

void MainWindow::look(const QString &text) {
  if (text.trimmed().isEmpty()) return;
  query_->setText(text);
  side_->setCurrentRow(lookup);
  card.reset();
  card.source = source;
  card.lookup(text.toStdString());
}

void MainWindow::refresh() {
  auto v = c_.engine.notes(tab_, page_);
  for (int i = 0; i < 3 && i < static_cast<int>(v.tabs.size()); ++i) side_->item(words + i)->setText(q(v.tabs[i]));
  side_->item(lookup)->setText(t("win_lookup"));
  side_->item(help)->setText(t("help_t"));
  auto m = c_.engine.memory();
  memory_->setText(q(m.held ? m.line + " · " + m.detail : m.line));
  if (pages_->currentIndex() == 1) draw_notes();
}

// The open list's page: a click opens the card, ✕ removes; TSV, the pager, "Clear history"; the backup under it.
void MainWindow::draw_notes() {
  auto said = said_ ? said_->text() : QString();
  auto *l = static_cast<QVBoxLayout *>(notes_->layout());
  clear(l);
  l->setContentsMargins(14, 14, 14, 14);
  auto v = c_.engine.notes(tab_, page_);
  if (v.rows.empty()) {
    l->addWidget(grey_label(q(v.empty)), 1, Qt::AlignCenter);
  } else {
    auto *list = new QWidget;
    auto *ll = new QVBoxLayout(list);
    ll->setSpacing(2);
    for (auto &r : v.rows) {
      auto *row = new QWidget;
      auto *rl = new QHBoxLayout(row);
      rl->setContentsMargins(4, 4, 4, 4);
      auto *text = new QToolButton;
      text->setAutoRaise(true);
      text->setToolButtonStyle(Qt::ToolButtonTextOnly);
      text->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
      text->setStyleSheet("QToolButton { text-align: left; border: none; }");
      auto shown = fontMetrics().elidedText(q(r.text), Qt::ElideRight, 480);
      auto meaning = fontMetrics().elidedText(q(r.line), Qt::ElideRight, 480);
      text->setText(shown + "\n" + meaning);
      text->setToolTip(q(r.text));
      auto open = q(r.text);
      connect(text, &QToolButton::clicked, this, [this, open] { look(open); });
      rl->addWidget(text, 1);
      if (!r.looks.empty()) rl->addWidget(grey_label(q(r.looks), -2));
      auto *x = new QToolButton;
      x->setText("✕");
      x->setAutoRaise(true);
      x->setToolTip(tab_ == NotesTab::history ? t("forget") : t("unsave_word"));
      auto key = r.text;
      connect(x, &QToolButton::clicked, this, [this, key] {
        c_.engine.remove_note(tab_, key);
        refresh();
      });
      rl->addWidget(x);
      ll->addWidget(row);
    }
    ll->addStretch();
    auto *scroll = new QScrollArea;
    scroll->setWidget(list);
    scroll->setWidgetResizable(true);
    l->addWidget(scroll, 1);
  }
  auto *bar = new QHBoxLayout;
  if (!v.rows.empty()) {
    auto *tsv = new QPushButton(t("tsv_btn"));
    tsv->setToolTip(t("tsv_d"));
    connect(tsv, &QPushButton::clicked, this, [this] {
      const char *kind[] = {"words", "phrases", "history"};
      auto f = QFileDialog::getSaveFileName(this, {}, downloads(QString("lexipeek-%1-%2.tsv").arg(kind[static_cast<int>(tab_)], today())));
      if (!f.isEmpty()) said_->setText(q(c_.engine.export_tsv(tab_, f.toStdString())));
    });
    bar->addWidget(tsv);
  }
  bar->addStretch();
  if (!v.pager.empty()) {
    auto *prev = new QToolButton;
    prev->setText("◀");
    prev->setEnabled(v.page > 0);
    connect(prev, &QToolButton::clicked, this, [this] { --page_, draw_notes(); });
    auto *next = new QToolButton;
    next->setText("▶");
    next->setEnabled(v.page + 1 < v.pages);
    connect(next, &QToolButton::clicked, this, [this] { ++page_, draw_notes(); });
    bar->addWidget(prev);
    bar->addWidget(grey_label(q(v.pager)));
    bar->addWidget(next);
    bar->addStretch();
  }
  if (v.clear) {
    auto *c = new QPushButton(t("clear_history_more"));
    connect(c, &QPushButton::clicked, this, [this] {
      if (QMessageBox::question(this, t("clear_history_q"), t("notes_clear_d")) != QMessageBox::Yes) return;
      c_.engine.clear_history();
      refresh();
    });
    bar->addWidget(c);
  }
  l->addLayout(bar);
  auto *line = new QFrame;
  line->setFrameShape(QFrame::HLine);
  line->setStyleSheet("color: palette(mid);");
  l->addWidget(line);
  auto *backup = new QHBoxLayout;
  backup->addWidget(grey_label(t("backup_d"), -2), 1);
  auto *out = new QPushButton(t("export_btn"));
  connect(out, &QPushButton::clicked, this, [this] {
    auto f = QFileDialog::getSaveFileName(this, {}, downloads("lexipeek-backup-" + today() + ".db"));
    if (!f.isEmpty()) said_->setText(q(c_.engine.backup(f.toStdString())));
  });
  auto *in = new QPushButton(t("import_btn"));
  connect(in, &QPushButton::clicked, this, [this] {
    auto f = QFileDialog::getOpenFileName(this, {}, QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
    if (f.isEmpty()) return;
    said_->setText(q(c_.engine.restore(f.toStdString())));
    refresh();
  });
  backup->addWidget(out);
  backup->addWidget(in);
  l->addLayout(backup);
  said_ = grey_label(said, -2);
  said_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  l->addWidget(said_);
}

// Every topic on one page, each with the settings pane it is about.
void MainWindow::draw_help() {
  delete help_->layout();
  for (auto *c : help_->findChildren<QWidget *>(Qt::FindDirectChildrenOnly)) delete c;
  auto *l = new QVBoxLayout(help_);
  l->setContentsMargins(20, 16, 20, 16);
  l->setSpacing(6);
  for (auto &topic : help_view(lookup_key, input_key)) {
    auto *title = new QLabel(q(topic.title));
    auto f = title->font();
    f.setBold(true);
    f.setPointSizeF(f.pointSizeF() + 1);
    title->setFont(f);
    l->addSpacing(8);
    l->addWidget(title);
    for (auto &i : topic.items) {
      if (!i.title.empty()) {
        auto *it = new QLabel(q(i.title));
        auto bf = it->font();
        bf.setWeight(QFont::Medium);
        it->setFont(bf);
        l->addWidget(it);
      }
      l->addWidget(grey_label(q(i.text)));
    }
    if (!topic.go.empty()) {
      auto *go = new QToolButton;
      go->setText(q(topic.go));
      go->setAutoRaise(true);
      go->setStyleSheet("QToolButton { color: palette(link); border: none; padding: 0; }");
      auto s = static_cast<int>(topic.section);
      auto tab = topic.tab;
      connect(go, &QToolButton::clicked, this, [this, s, tab] { emit openSettings(s, tab); });
      l->addWidget(go, 0, Qt::AlignLeft);
    }
  }
  l->addStretch();
}

}  // namespace lexipeek::qt
