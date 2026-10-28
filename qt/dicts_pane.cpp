#include "dicts_pane.hpp"
#include <QCheckBox>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include "ui.hpp"

namespace lexipeek::qt {

namespace {

QScrollArea *scrolled(QWidget *inner) {
  auto *s = new QScrollArea;
  s->setWidget(inner);
  s->setWidgetResizable(true);
  s->setFrameShape(QFrame::NoFrame);
  return s;
}

QToolButton *small(const QString &text, const QString &tip, bool enabled, std::function<void()> click) {
  auto *b = new QToolButton;
  b->setText(text);
  b->setToolTip(tip);
  b->setAutoRaise(true);
  b->setEnabled(enabled);
  QObject::connect(b, &QToolButton::clicked, std::move(click));
  return b;
}

}  // namespace

DictsPane::DictsPane(CoreClient &client) : c_(client) {
  setAcceptDrops(true);
  auto *inner = new QWidget;
  body_ = new QVBoxLayout(inner);
  body_->setContentsMargins(20, 12, 20, 16);
  body_->setSpacing(8);
  auto *l = new QVBoxLayout(this);
  l->setContentsMargins(0, 0, 0, 0);
  l->addWidget(scrolled(inner));
  draw();
}

void DictsPane::draw() {
  auto *area = findChild<QScrollArea *>();
  int scrolled = area->verticalScrollBar()->value();
  ui::clear(body_);
  auto dir = c_.data();
  auto v = app::dictionaries(dir);
  auto *buttons = new QHBoxLayout;
  auto *folder = new QPushButton(t("add_folder_more"));
  connect(folder, &QPushButton::clicked, this, [this, dir] {
    auto d = QFileDialog::getExistingDirectory(this, t("folder_panel"));
    if (d.isEmpty()) return;
    message_ = app::add_folder(dir, d.toStdString());
    changed(true);
  });
  auto *import = new QPushButton(t("import_more"));
  connect(import, &QPushButton::clicked, this, [this] {
    auto files = QFileDialog::getOpenFileNames(this, t("import_"));
    std::vector<std::string> paths;
    for (auto &f : files) paths.push_back(f.toStdString());
    if (!paths.empty()) DictsPane::import(paths);
  });
  auto *catalog = new QPushButton(t("catalog_more"));
  connect(catalog, &QPushButton::clicked, this, [this] {
    CatalogDialog d(c_, this);
    d.exec();
    changed(true);
  });
  for (auto *b : {folder, import, catalog}) b->setEnabled(!busy_), buttons->addWidget(b);
  buttons->addStretch();
  body_->addLayout(buttons);
  if (!message_.empty()) {
    auto *m = ui::text(message_, -1, true);
    m->setTextInteractionFlags(Qt::TextSelectableByMouse);
    body_->addWidget(m);
  }
  body_->addWidget(ui::text(v.import_note, -1.5, true));
  body_->addSpacing(6);
  body_->addWidget(ui::text(t("lang_own_tab"), 0, false, true));
  bool own = false;
  for (size_t i = 0; i < v.rows.size(); ++i) {
    auto &r = v.rows[i];
    own = own || !r.pack;
    auto *row = new QHBoxLayout;
    if (r.pack) {
      auto *box = ui::text(QString("▣"), 0, true);
      box->setFixedWidth(24);
      row->addWidget(box);
    } else {
      auto *on = new QCheckBox;
      on->setChecked(r.on);
      on->setFixedWidth(24);
      auto id = r.id;
      connect(on, &QCheckBox::toggled, this, [this, dir, id](bool b) {
        app::set_dict_on(dir, id, b);
        changed(false);
      });
      row->addWidget(on);
    }
    auto *col = new QVBoxLayout;
    col->setSpacing(1);
    auto *title = ui::text(r.title);
    auto *detail = ui::text(r.detail, -1.5, true);
    title->setEnabled(r.on), detail->setEnabled(r.on);
    col->addWidget(title);
    col->addWidget(detail);
    row->addLayout(col, 1);
    auto id = r.id;
    row->addWidget(small("▲", t("up"), i > 0, [this, dir, id] {
      app::move_dict(dir, id, -1);
      changed(false);
    }));
    row->addWidget(small("▼", t("down"), i + 1 < v.rows.size(), [this, dir, id] {
      app::move_dict(dir, id, 1);
      changed(false);
    }));
    if (r.removable) {
      auto title_text = r.title;
      row->addWidget(small("✕", t("delete"), true, [this, dir, id, title_text] {
        QMessageBox box(QMessageBox::Warning, t("delete_q", title_text), t("delete_d"), QMessageBox::NoButton, this);
        auto *del = box.addButton(t("delete"), QMessageBox::DestructiveRole);
        box.addButton(t("cancel"), QMessageBox::RejectRole);
        box.exec();
        if (box.clickedButton() != del) return;
        message_ = app::remove_dict(dir, id);
        changed(true);
      }));
    }
    body_->addLayout(row);
  }
  if (!own) body_->addWidget(ui::text(v.empty, 0, true));
  body_->addWidget(ui::text(v.note, -1.5, true));
  if (!v.folders.empty()) {
    body_->addSpacing(6);
    body_->addWidget(ui::text(t("folders_t"), 0, false, true));
    for (auto &f : v.folders) {
      auto *row = new QHBoxLayout;
      auto *col = new QVBoxLayout;
      col->setSpacing(1);
      col->addWidget(ui::text(f.title));
      col->addWidget(ui::text(f.detail, -1.5, true));
      row->addLayout(col, 1);
      auto *remove = new QPushButton(t("remove"));
      auto path = f.path;
      connect(remove, &QPushButton::clicked, this, [this, dir, path] {
        app::remove_folder(dir, path);
        changed(true);
      });
      row->addWidget(remove);
      body_->addLayout(row);
    }
  }
  body_->addStretch();
  ui::keep_scroll(area, scrolled);
}

void DictsPane::import(const std::vector<std::string> &files) {
  busy_ = true;
  message_ = t("importing").toStdString();
  draw();
  auto dir = c_.data();
  c_.run<std::string>(c_.ai, [dir, files] { return app::imported_text(app::import_dicts(dir, files)); }, [this](std::string text) {
    busy_ = false;
    message_ = text;
    changed(true);
  }, this);
}

// A new dictionary on disk: the library looks again (and indexes it); else only the settings changed.
void DictsPane::changed(bool reload) {
  c_.configure();
  if (reload) c_.reload();
  draw();
}

void DictsPane::dragEnterEvent(QDragEnterEvent *e) {
  if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}

void DictsPane::dropEvent(QDropEvent *e) {
  std::vector<std::string> files;
  for (auto &u : e->mimeData()->urls())
    if (u.isLocalFile()) files.push_back(u.toLocalFile().toStdString());
  if (!files.empty()) import(files);
}

CatalogDialog::CatalogDialog(CoreClient &client, QWidget *parent) : QDialog(parent), c_(client) {
  setWindowTitle(t("lang_own"));
  resize(520, 520);
  auto *l = new QVBoxLayout(this);
  filter_edit_ = new QLineEdit;
  connect(filter_edit_, &QLineEdit::textChanged, this, [this](const QString &s) {
    filter_ = s.toStdString();
    draw();
  });
  body_ = new QVBoxLayout;
  auto *inner = new QWidget;
  auto *il = new QVBoxLayout(inner);
  il->setContentsMargins(0, 0, 0, 0);
  il->addLayout(body_);
  il->addStretch();
  auto *sources = new QHBoxLayout;
  sources->setObjectName("sources");
  l->addLayout(sources);
  l->addWidget(filter_edit_);
  l->addWidget(scrolled(inner), 1);
  auto *done = new QPushButton(t("done"));
  done->setDefault(true);
  connect(done, &QPushButton::clicked, this, &QDialog::accept);
  auto *bottom = new QHBoxLayout;
  bottom->addStretch();
  bottom->addWidget(done);
  l->addLayout(bottom);
  poll_ = new QTimer(this);
  connect(poll_, &QTimer::timeout, this, [this] {
    auto total = dl_.get_total();
    progress_ = total > 0 ? static_cast<double>(dl_.get_done()) / total : 0;
    draw();
  });
  fetch();
}

void CatalogDialog::fetch() {
  loading_ = true;
  draw();
  auto dir = c_.data();
  c_.run<CatalogList>(c_.ai, [dir] { return app::catalog(dir); }, [this](CatalogList l) {
    list_ = l;
    failed_.clear();
    for (auto &f : l.failed) failed_ += (failed_.empty() ? "" : "\n") + f;
    loading_ = false;
    draw();
  }, this);
}

void CatalogDialog::draw() {
  auto dir = c_.data();
  auto v = app::catalog_view(dir, list_, filter_);
  filter_edit_->setPlaceholderText(q(v.filter_hint));
  auto *sources = findChild<QHBoxLayout *>("sources");
  ui::clear(sources);
  for (auto &s : v.sources) {
    auto *b = new QCheckBox(q(s.title));
    b->setChecked(s.on);
    b->setToolTip(q(s.about));
    auto id = s.id;
    connect(b, &QCheckBox::toggled, this, [this, dir, id](bool on) {
      app::set_source_on(dir, id, on);
      QMetaObject::invokeMethod(this, [this] { fetch(); }, Qt::QueuedConnection);
    });
    sources->addWidget(b);
  }
  sources->addStretch();
  ui::clear(body_);
  body_->addWidget(ui::text(v.legend, -1.5, true));
  if (!downloading_.empty()) {
    auto *row = new QHBoxLayout;
    row->addWidget(ui::text(downloading_));
    auto *p = new QProgressBar;
    p->setTextVisible(false);
    p->setRange(0, 1000);
    p->setValue(static_cast<int>(progress_ * 1000));
    row->addWidget(p, 1);
    auto *cancel = new QPushButton(t("cancel"));
    connect(cancel, &QPushButton::clicked, this, [this] { dl_.cancel(); });
    row->addWidget(cancel);
    body_->addLayout(row);
  }
  for (auto *s : {&message_, &failed_, &v.empty})
    if (!s->empty()) body_->addWidget(ui::text(*s, -1, true));
  if (loading_) body_->addWidget(ui::text(t("loading_lists"), 0, true));
  for (auto &r : v.rows) {
    auto *row = new QHBoxLayout;
    auto *col = new QVBoxLayout;
    col->setSpacing(1);
    col->addWidget(ui::text(r.title));
    col->addWidget(ui::text(r.detail, -1.5, true));
    row->addLayout(col, 1);
    auto *get = new QPushButton(t("download"));
    get->setEnabled(downloading_.empty());
    auto item = r.item;
    auto title = r.title, about = r.about;
    connect(get, &QPushButton::clicked, this, [this, item, title, about] {
      if (QMessageBox::question(this, q(title), q(about)) == QMessageBox::Yes) CatalogDialog::get(item, title);
    });
    row->addWidget(get);
    body_->addLayout(row);
  }
  setWindowTitle(t("lang_own") + (v.info.empty() ? "" : "  ·  " + q(v.info)));
}

void CatalogDialog::get(int item, const std::string &title) {
  if (!downloading_.empty()) return;
  downloading_ = title;
  progress_ = 0;
  message_.clear();
  dl_ = app::Download();
  auto dl = dl_;
  auto dir = c_.data();
  auto list = list_;
  poll_->start(300);
  c_.run<std::string>(c_.ai, [dir, list, item, dl] { return app::imported_text(app::download_dict(dir, list, item, dl)); },
                      [this](std::string text) {
                        poll_->stop();
                        downloading_.clear();
                        message_ = text;
                        draw();
                      },
                      this);
  draw();
}

}  // namespace lexipeek::qt
