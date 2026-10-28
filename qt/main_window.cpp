#include "main_window.hpp"
#include <QDate>
#include <QEvent>
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
#include <QHeaderView>
#include <QShortcut>
#include <QTreeWidget>
#include <QVBoxLayout>
#include "fluent.hpp"
#include "ui.hpp"

namespace lexipeek::qt {

namespace {

QString downloads(const QString &name) {
  return QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) + "/" + name;
}


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
  // Windows Settings' layout: the pages in a list on the left; Settings and About at its foot.
  side_ = new QListWidget;
  side_->setObjectName("nav");
  side_->setIconSize(QSize(16, 16));
  for (int i = 0; i < 5; ++i) side_->addItem("");
  sl->addWidget(side_, 1);
  memory_ = ui::text(QString(), -1.5, true);
  memory_->setContentsMargins(12, 0, 0, 4);
  sl->addWidget(memory_);
  foot_ = new QListWidget;
  foot_->setObjectName("nav");
  foot_->setIconSize(QSize(16, 16));
  foot_->addItem("");
  foot_->addItem("");
  foot_->setFixedHeight(84);
  connect(foot_, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
    foot_->clearSelection();
    if (foot_->row(item) == 0) emit openSettings(-1, 0);
    else emit openAbout();
  });
  sl->addWidget(foot_);
  row->addWidget(side);

  pages_ = new QStackedWidget;
  pages_->addWidget(lookup_page());
  pages_->addWidget(notes_page());
  help_ = new QWidget;
  auto *hs = ui::scroll(help_);
  pages_->addWidget(hs);
  auto *col = new QVBoxLayout;
  col->setContentsMargins(0, 0, 0, 0);
  banner_ = new QVBoxLayout;
  banner_->setContentsMargins(16, 12, 16, 0);
  col->addLayout(banner_);
  col->addWidget(pages_, 1);
  row->addLayout(col, 1);

  connect(side_, &QListWidget::currentRowChanged, this, [this](int r) {
    if (r == lookup) pages_->setCurrentIndex(0);
    else if (r == help) draw_help(), pages_->setCurrentIndex(2);
    else if (r >= 0) tab_ = static_cast<NotesTab>(r - words), page_ = 0, draw_notes(), pages_->setCurrentIndex(1);
  });
  auto *timer = new QTimer(this);
  connect(timer, &QTimer::timeout, this, &MainWindow::refresh);
  timer->start(15000);
  resize(900, 600);
  side_->setCurrentRow(0);
  icons();
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
  hint_ = ui::text(QString(), 0, true);
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

void MainWindow::changeEvent(QEvent *e) {
  QWidget::changeEvent(e);
  if (e->type() != QEvent::ApplicationPaletteChange) return;
  if (pages_->currentIndex() == 1) draw_notes();
  if (pages_->currentIndex() == 2) draw_help();
}

void MainWindow::look(const QString &text) {
  if (text.trimmed().isEmpty()) return;
  query_->setText(text);
  side_->setCurrentRow(lookup);
  card.reset();
  card.source = source;
  card.lookup(text.toStdString());
}

void MainWindow::icons() {
  const fluent::Glyph glyphs[] = {fluent::Glyph::search, fluent::Glyph::star, fluent::Glyph::comment, fluent::Glyph::history, fluent::Glyph::help};
  for (int i = 0; i < 5; ++i) side_->item(i)->setIcon(fluent::icon(glyphs[i]));
  foot_->item(0)->setIcon(fluent::icon(fluent::Glyph::settings));
  foot_->item(1)->setIcon(fluent::icon(fluent::Glyph::info));
}

void MainWindow::refresh() {
  foot_->item(0)->setText(t("settings_more"));
  foot_->item(1)->setText(t("menu_about"));
  hint_->setText(t("win_hint", lookup_key, input_key));
  auto v = c_.engine.notes(tab_, page_);
  for (int i = 0; i < 3 && i < static_cast<int>(v.tabs.size()); ++i) side_->item(words + i)->setText(q(v.tabs[i]));
  side_->item(lookup)->setText(t("win_lookup"));
  side_->item(help)->setText(t("help_t"));
  auto m = c_.engine.memory();
  memory_->setText(q(m.text));
  ui::clear(banner_);
  if (!app::has_pair(c_.data())) banner_->addWidget(fluent::banner(t("nopack_t"), t("nopack_d"), t("setup_btn"), [this] { emit openFirstRun(); }));
  if (pages_->currentIndex() == 1) draw_notes();
}

// The open list's page: a click opens the card, ✕ removes; TSV, the pager, "Clear history"; the backup under it.
void MainWindow::draw_notes() {
  auto said = said_ ? said_->text() : QString();
  auto *l = static_cast<QVBoxLayout *>(notes_->layout());
  ui::clear(l);
  l->setContentsMargins(14, 14, 14, 14);
  auto v = c_.engine.notes(tab_, page_);
  if (v.rows.empty()) {
    auto *empty = ui::text(q(v.empty), 0, true);
    empty->setAlignment(Qt::AlignCenter);
    l->addWidget(empty, 1);
  } else {
    // A list: the text, its meaning, the looks; Return or a double click opens the card, Delete or the button removes.
    auto *list = new QTreeWidget;
    list->setColumnCount(3);
    list->setHeaderHidden(true);
    list->setRootIsDecorated(false);
    list->setUniformRowHeights(true);
    list->setAlternatingRowColors(true);
    for (auto &r : v.rows) {
      auto *item = new QTreeWidgetItem({q(r.text), q(r.line), q(r.looks)});
      item->setToolTip(0, q(r.text));
      item->setForeground(1, palette().color(QPalette::PlaceholderText));
      item->setTextAlignment(2, Qt::AlignRight);
      list->addTopLevelItem(item);
    }
    list->header()->setStretchLastSection(false);
    list->header()->setSectionResizeMode(0, QHeaderView::Interactive);
    list->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    list->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    list->setColumnWidth(0, 220);
    connect(list, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *item) { look(item->text(0)); });
    auto remove = [this, list] {
      if (auto *item = list->currentItem()) {
        c_.engine.remove_note(tab_, item->text(0).toStdString());
        refresh();
      }
    };
    auto *del = new QShortcut(QKeySequence::Delete, list);
    connect(del, &QShortcut::activated, this, remove);
    l->addWidget(list, 1);
    auto *x = new QPushButton(tab_ == NotesTab::history ? t("forget") : t("unsave_word"));
    x->setEnabled(false);
    connect(list, &QTreeWidget::currentItemChanged, x, [x](QTreeWidgetItem *now) { x->setEnabled(now); });
    connect(x, &QPushButton::clicked, this, remove);
    auto *row = new QHBoxLayout;
    row->addStretch();
    row->addWidget(x);
    l->addLayout(row);
  }
  auto *bar = new QHBoxLayout;
  if (!v.rows.empty()) {
    auto *tsv = new QPushButton(t("tsv_btn"));
    tsv->setToolTip(t("tsv_d"));
    connect(tsv, &QPushButton::clicked, this, [this] {
      auto f = QFileDialog::getSaveFileName(this, {}, downloads(q(app::notes_file(tab_))));
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
    bar->addWidget(ui::text(q(v.pager), 0, true));
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
  line->setFrameShadow(QFrame::Sunken);
  l->addWidget(line);
  auto *backup = new QHBoxLayout;
  backup->addWidget(ui::text(t("backup_d"), -1.5, true), 1);
  auto *out = new QPushButton(t("export_btn"));
  connect(out, &QPushButton::clicked, this, [this] {
    auto f = QFileDialog::getSaveFileName(this, {}, downloads(q(app::backup_file())));
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
  said_ = ui::text(said, -1.5, true);
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
      l->addWidget(ui::text(q(i.text), 0, true));
    }
    if (!topic.go.empty()) {
      auto s = static_cast<int>(topic.section);
      auto tab = topic.tab;
      auto *go = ui::link(q(topic.go), [this, s, tab] { emit openSettings(s, tab); });
      l->addWidget(go, 0, Qt::AlignLeft);
    }
  }
  l->addStretch();
}

}  // namespace lexipeek::qt
