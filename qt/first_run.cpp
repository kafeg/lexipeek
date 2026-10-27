#include "first_run.hpp"
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include "ui.hpp"

namespace lexipeek::qt {

FirstRunWindow::FirstRunWindow(CoreClient &client) : c_(client) {
  setWindowTitle(t("fr_window"));
  setFixedWidth(480);
  box_ = new QVBoxLayout(this);
  box_->setContentsMargins(20, 20, 20, 20);
  box_->setSpacing(12);
  state.trusted = true;
  state.system = ui_language();
  poll_ = new QTimer(this);
  connect(poll_, &QTimer::timeout, this, [this] {
    state.done = dl_.get_done(), state.total = dl_.get_total();
    refresh();
  });
  refresh();
}

void FirstRunWindow::refresh() {
  look_ = app::first_run(c_.data(), state);
  ui::clear(box_);
  auto *head = new QHBoxLayout;
  auto *icon = new QLabel;
  icon->setPixmap(QPixmap(":/icons/icon_64.png").scaled(48, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation));
  head->addWidget(icon);
  auto *titles = new QVBoxLayout;
  titles->setSpacing(2);
  titles->addWidget(ui::text(t("fr_step", std::to_string(look_.number), std::to_string(look_.steps)).toStdString(), -2, true));
  titles->addWidget(ui::text(look_.title, 4, false, true));
  head->addLayout(titles, 1);
  box_->addLayout(head);
  if (!look_.text.empty()) box_->addWidget(ui::text(look_.text));
  if (look_.step == FirstStep::pair) {
    auto *row = new QHBoxLayout;
    row->addWidget(ui::text(look_.answer_label));
    auto *answers = new QComboBox;
    for (auto &a : look_.answers) answers->addItem(q(a.title), q(a.code));
    answers->setCurrentIndex(answers->findData(q(look_.answer)));
    connect(answers, &QComboBox::activated, this, [this, answers](int i) {
      state.answer = answers->itemData(i).toString().toStdString();
      state.book.clear();
      QMetaObject::invokeMethod(this, [this] { refresh(); }, Qt::QueuedConnection);
    });
    row->addWidget(answers);
    row->addStretch();
    box_->addLayout(row);
    box_->addWidget(ui::text(look_.book_label));
    auto *books = new QListWidget;
    books->setFixedHeight(150);
    for (auto &b : look_.books) {
      auto *item = new QListWidgetItem;
      item->setData(Qt::UserRole, q(b.code));
      books->addItem(item);
      // The language on the left, its level and size on the right.
      auto *line = new QWidget;
      auto *rl = new QHBoxLayout(line);
      rl->setContentsMargins(6, 0, 6, 0);
      rl->addWidget(new QLabel(q(b.title)), 1);
      auto *d = new QLabel(q(b.detail));
      ui::grey(d);
      rl->addWidget(d);
      item->setSizeHint(QSize(0, 24));
      books->setItemWidget(item, line);
      if (b.code == look_.book) books->setCurrentItem(item);
    }
    connect(books, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
      state.book = item->data(Qt::UserRole).toString().toStdString();
      QMetaObject::invokeMethod(this, [this] { refresh(); }, Qt::QueuedConnection);
    });
    box_->addWidget(books);
  }
  if ((look_.step == FirstStep::download || look_.step == FirstStep::list) && look_.progress) {
    auto *p = new QProgressBar;
    p->setTextVisible(false);
    p->setRange(0, look_.fraction >= 0 ? 1000 : 0);
    p->setValue(static_cast<int>(std::max(0.0, look_.fraction) * 1000));
    box_->addWidget(p);
    if (!look_.progress_text.empty()) box_->addWidget(ui::text(look_.progress_text, -2, true));
  }
  if (!look_.error.empty()) box_->addWidget(ui::text("⚠ " + look_.error, -1));
  if (!look_.note.empty()) box_->addWidget(ui::text(look_.note, -2, true));
  box_->addStretch();
  auto *buttons = new QHBoxLayout;
  buttons->addStretch();
  if (!look_.secondary.empty()) {
    auto *b = new QPushButton(q(look_.secondary));
    connect(b, &QPushButton::clicked, this, &FirstRunWindow::other);
    buttons->addWidget(b);
  }
  if (!look_.button.empty()) {
    auto *b = new QPushButton(q(look_.button));
    b->setDefault(true);
    b->setEnabled(look_.button_enabled);
    connect(b, &QPushButton::clicked, this, &FirstRunWindow::primary);
    buttons->addWidget(b);
  }
  box_->addLayout(buttons);
  // Wrapped labels: the height their lines need at this width.
  setFixedHeight(std::max(440, box_->totalHeightForWidth(480)));
}

void FirstRunWindow::start() {
  if (look_.step == FirstStep::list || look_.step == FirstStep::pair) fetch();
}

void FirstRunWindow::primary() {
  switch (look_.step) {
    case FirstStep::list: return fetch();
    case FirstStep::pair: return install();
    case FirstStep::download:
      if (state.downloading) return dl_.cancel();
      return install();
    default: close();
  }
}

void FirstRunWindow::other() {
  if (look_.step == FirstStep::download) state.error.clear(), refresh();
}

void FirstRunWindow::closeEvent(QCloseEvent *e) {
  dl_.cancel();
  auto s = app::settings(c_.data());
  s.first_run = true;
  app::save(c_.data(), s);
  emit finished();
  QWidget::closeEvent(e);
}

// The release's list: fresh, or kept from the last time.
void FirstRunWindow::fetch() {
  if (state.fetching) return;
  state.fetching = true;
  refresh();
  auto dir = c_.data();
  c_.run<std::string>(c_.ai, [dir] { return app::refresh(dir); }, [this](std::string e) {
    state.list_error = e;
    state.fetching = false;
    refresh();
  }, this);
}

// The picked pair: its answer language for the next cards, then what it still needs, with progress.
void FirstRunWindow::install() {
  if (state.downloading || look_.book.empty()) return;
  auto dir = c_.data();
  auto answer = look_.answer, book = look_.book;
  auto s = app::settings(dir);
  s.answer = answer;
  app::save(dir, s);
  c_.configure();
  state.answer = answer, state.book = book;
  auto ids = app::missing_ids(dir, book, answer);
  state.error.clear();
  state.done = state.total = 0;
  state.downloading = true;
  dl_ = app::Download();
  auto dl = dl_;
  poll_->start(300);
  c_.run<Status>(c_.ai, [dir, ids, dl] { return app::install(dir, ids, dl); }, [this](Status s) {
    poll_->stop();
    state.error = s.cancelled ? "" : s.error;
    state.downloading = false;
    c_.reload();
    refresh();
  }, this);
  refresh();
}

}  // namespace lexipeek::qt
