#include "card.hpp"
#include <QApplication>
#include <QClipboard>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QTextLayout>
#include <QLineEdit>
#include <QMenu>
#include <QRegularExpression>
#include <QScreen>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>
#include "fluent.hpp"
#include "ui.hpp"

namespace lexipeek::qt {

CardModel::CardModel(CoreClient &client) : c_(client), session_(app::CardSession::make(client.engine)) {
  poll_.setInterval(40);
  connect(&poll_, &QTimer::timeout, this, [this] {
    if (session_.poll()) relook();
    if (session_.notes_changed()) emit notes_bus().changed();
    if (!session_.busy()) poll_.stop();
  });
  // Notes may have removed (or another card saved) this card's word: its ★ again.
  connect(&notes_bus(), &NotesBus::changed, this, [this] {
    session_.recheck_saved();
    if (session_.poll()) relook();
  });
}

void CardModel::relook() {
  nav = session_.nav();
  look_ = session_.view();
  emit changed();
}

void CardModel::acted() {
  if (session_.poll()) relook();
  if (session_.notes_changed()) emit notes_bus().changed();
  if (session_.busy() && !poll_.isActive()) poll_.start();
}

void CardModel::reset() {
  pinned = false;
  unread.clear();
  session_.reset();
  acted();
}

void CardModel::lookup(const std::string &text, const std::string &lang, const std::string &answer) {
  session_.set_key(key());
  session_.look(q(text).trimmed().toStdString(), source, lang, answer);
  acted();
}
void CardModel::open(const std::string &word) { session_.set_key(key()), session_.open(word), acted(); }
void CardModel::relookup(const std::string &lang, const std::string &answer) { session_.set_key(key()), session_.relookup(lang, answer), acted(); }
void CardModel::back() { session_.set_key(key()), session_.back(), acted(); }
void CardModel::open_section(int entry, const std::string &section) { session_.open_section(entry, section), acted(); }
void CardModel::turn(int by) { session_.turn(by), acted(); }
void CardModel::open_entry(int entry) { session_.open_entry(entry), acted(); }
void CardModel::select(CardTab tab) { session_.set_key(key()), session_.select(tab), acted(); }
void CardModel::next_tab(int by) { session_.set_key(key()), session_.next_tab(by), acted(); }
void CardModel::full_entries() { session_.full_entries(), acted(); }

void CardModel::choose(Method m) {
  auto s = app::settings(c_.data());
  app::set_method(s, app::method_id(m));
  app::save(c_.data(), s);
  c_.configure();
  use(m);
}

void CardModel::use(Method m) { session_.set_key(key()), session_.use(m), acted(); }
void CardModel::choose_ai() { session_.set_key(key()), session_.choose_ai(), acted(); }
void CardModel::toggle_saved() { session_.toggle_saved(), acted(); }

void CardModel::cancel() { session_.cancel(), acted(); }

void CardModel::toggle_pin() {
  pinned = !pinned;
  relook();
}

namespace {

// An AI answer's light Markdown (**bold**, *italic*) as Qt's rich text, its lines kept.
QString markdown(const std::string &s) {
  auto h = q(s).toHtmlEscaped();
  h.replace(QRegularExpression("\\*\\*(.+?)\\*\\*"), "<b>\\1</b>");
  h.replace(QRegularExpression("\\*(.+?)\\*"), "<i>\\1</i>");
  return h.replace("\n", "<br>");
}

// Rich text as a link in the text's own colour (Qt's rich text knows no palette() in its CSS).
QString plain_link(const QString &html) {
  return "<a href='#' style='text-decoration: none; color: " + QApplication::palette().color(QPalette::WindowText).name() + "'>" + html + "</a>";
}

// The text in at most lines lines of width, the last one ending with "…" when it is cut.
QString cut(const QString &text, const QFont &f, int width, int lines) {
  QFontMetrics m(f);
  QTextLayout layout(text, f);
  layout.beginLayout();
  QString out;
  for (int n = 0; n < lines; ++n) {
    auto line = layout.createLine();
    if (!line.isValid()) break;
    line.setLineWidth(width);
    if (n == lines - 1 && line.textStart() + line.textLength() < text.size())
      out += m.elidedText(text.mid(line.textStart()), Qt::ElideRight, width);
    else
      out += text.mid(line.textStart(), line.textLength());
  }
  layout.endLayout();
  return out.trimmed() == text.trimmed() ? text : out;
}

QLabel *label(const QString &text, const QFont &f, bool rich = false, bool select = true) {
  auto *l = new QLabel(text);
  l->setFont(f);
  l->setWordWrap(true);
  l->setTextFormat(rich ? Qt::RichText : Qt::PlainText);
  if (select) l->setTextInteractionFlags(Qt::TextSelectableByMouse);
  return l;
}


// The card's own look: a link in the link colour, as wide as its text.
QLabel *link(const QString &text, const QFont &f, std::function<void()> click, const QColor &color = {}) {
  auto c = color.isValid() ? color : QApplication::palette().color(QPalette::Link);
  auto *l = new QLabel("<a href='#' style='text-decoration: none; color: " + c.name() + "'>" + text.toHtmlEscaped() + "</a>");
  l->setFont(f);
  l->setTextFormat(Qt::RichText);
  l->setCursor(Qt::PointingHandCursor);
  QObject::connect(l, &QLabel::linkActivated, [click = std::move(click)] { click(); });
  return l;
}

// A menu button of the bottom row (the method, the badge): its text and ▾, as the command bar's buttons.
QToolButton *menu_button(const QString &text, const QFont &f) {
  auto *b = new QToolButton;
  b->setText(text);
  b->setFont(f);
  b->setProperty("command", true);
  b->setCursor(Qt::PointingHandCursor);
  b->setPopupMode(QToolButton::InstantPopup);
  b->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);  // never cut to "d…": the stretch gives way
  return b;
}

// An icon button of the command bar: the system's glyph (in a colour: ☆ saved, the pin on), its tooltip.
QToolButton *command(fluent::Glyph g, const QString &tip, std::function<void()> click, QColor color = {}) {
  auto *b = new QToolButton;
  b->setIcon(fluent::icon(g, color));
  b->setIconSize(QSize(16, 16));
  b->setToolTip(tip);
  b->setProperty("command", true);
  b->setCursor(Qt::PointingHandCursor);
  QObject::connect(b, &QToolButton::clicked, std::move(click));
  return b;
}

}  // namespace

CardWidget::CardWidget(CardModel &model, bool embedded, QWidget *parent) : QWidget(parent), m_(model), embedded_(embedded) {
  box_ = new QVBoxLayout(this);
  box_->setContentsMargins(0, 0, 0, 0);
  box_->setSpacing(0);
  // Drawn again once the event that changed it is over: a click or Return on a widget of the card must not delete it
  // inside its own signal.
  connect(&m_, &CardModel::changed, this, [this] {
    if (pending_) return;
    pending_ = true;
    QMetaObject::invokeMethod(this, [this] {
      pending_ = false;
      rebuild();
    }, Qt::QueuedConnection);
  });
  rebuild();
}

QFont CardWidget::font(int delta, bool bold) const {
  QFont f = QApplication::font();
  if (v_.font == "serif") f.setFamilies({"Georgia", "Times New Roman", "serif"});
  if (v_.font == "mono") f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
  f.setPointSizeF((v_.text > 0 ? v_.text : 13) + delta);
  f.setBold(bold);
  return f;
}

void CardWidget::rebuild() {
  v_ = m_.look();
  if (auto place = m_.nav.place(); place != place_) place_ = place, whole_ = false;
  ui::clear(box_);
  if (!embedded_) setFixedWidth(v_.width > 0 ? v_.width : 400);
  auto *top = new QWidget;
  auto *tl = new QVBoxLayout(top);
  tl->setContentsMargins(14, 14, 14, 8);
  tl->setSpacing(8);
  box_->addWidget(top);
  if (!m_.unread.empty()) {
    auto *u = label(t("card_unread", m_.unread), font(-1), false, false);
    ui::grey(u);
    tl->addWidget(u);
  }
  if (m_.input) tl->addWidget(field());
  if (!m_.has_card()) {
    if (m_.looking()) tl->addWidget(label("…", font(), false, false));
    if (!embedded_) adjustSize();
    return;
  }
  if (!(embedded_ && v_.style == HeaderStyle::passage)) tl->addWidget(header(v_));
  if (v_.tabs) tl->addWidget(tabs(v_));
  // Wrapped labels: the height their lines need at the card's width (a layout's size hint does not know it).
  // (The size hint wraps them narrower: taller, a gap under the title.)
  if (!embedded_) top->setFixedHeight(tl->hasHeightForWidth() ? tl->totalHeightForWidth(width()) : tl->sizeHint().height());

  auto *body = new QWidget;
  auto *bl = new QVBoxLayout(body);
  bl->setContentsMargins(14, 0, 24, 8);
  bl->setSpacing(8);
  for (auto &r : v_.rows) bl->addWidget(row(r, v_));
  if (!v_.fix.empty()) bl->addWidget(link(q(v_.fix), font(-2), [this] { emit openPane(static_cast<int>(v_.fix_section), v_.fix_tab); }));
  if (embedded_) bl->addWidget(bar(v_));  // in the window the bar follows the text
  bl->addStretch();
  auto *scroll = new QScrollArea;
  scroll->setObjectName("body");
  scroll->setWidget(body);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  if (!embedded_) {
    auto *screen = QApplication::primaryScreen();
    int limit = screen ? screen->availableGeometry().height() * (v_.height > 0 ? v_.height : 45) / 100 : 500;
    body->setFixedWidth(width());
    scroll->setFixedHeight(std::min(bl->totalHeightForWidth(width()), limit));
  }
  box_->addWidget(scroll, 1);
  if (!embedded_) box_->addWidget(bar(v_));
  if (!embedded_) adjustSize();
}

// As InkPeek's title: a word's or a phrase's two lines, a passage's one, cut with "…"; a click on a cut one shows it whole
// (scrolling, half the card's limit at most) with ⌃ to fold it again.
QWidget *CardWidget::header(const CardView &v) {
  auto *w = new QWidget;
  auto *l = new QVBoxLayout(w);
  l->setContentsMargins(0, 0, 0, 0);
  l->setSpacing(2);
  auto f = v.style == HeaderStyle::word ? font(4, true) : v.style == HeaderStyle::phrase ? font(0, true) : font(-1);
  int width = (embedded_ ? std::max(this->width(), 300) : (v.width > 0 ? v.width : 400)) - 28;
  int lines = v.style == HeaderStyle::passage ? 1 : 2;
  auto full = q(v.title);
  auto shown = cut(full, f, width, lines);
  bool is_cut = shown != full;
  if (whole_ && is_cut) {
    auto *all = label(full, f);
    if (v.style == HeaderStyle::passage) ui::grey(all);
    auto *scroll = new QScrollArea;
    scroll->setWidget(all);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *screen = QApplication::primaryScreen();
    int limit = screen ? screen->availableGeometry().height() * (v.height > 0 ? v.height : 45) / 200 : 250;
    all->setFixedWidth(width);
    scroll->setFixedHeight(std::min(all->heightForWidth(width) + 4, limit));
    l->addWidget(scroll);
    auto *fold = link("⌃", font(0), [this] { show_whole(false); });
    fold->setToolTip(t("card_collapse"));
    l->addWidget(fold, 0, Qt::AlignHCenter);
  } else {
    auto *title = label(shown, f, false, false);
    title->setToolTip(is_cut ? t("card_whole") : q(v.tooltip));
    if (v.style == HeaderStyle::passage) ui::grey(title);
    if (is_cut) {
      title->setCursor(Qt::PointingHandCursor);
      title->installEventFilter(this);
      title->setObjectName("cut-title");
    }
    l->addWidget(title);
  }
  if (!v.subtitle.empty()) {
    auto *s = label(q(v.subtitle), font(-2), false, false);
    ui::grey(s);
    l->addWidget(s);
  }
  return w;
}

void CardWidget::changeEvent(QEvent *e) {
  QWidget::changeEvent(e);
  if (e->type() == QEvent::ApplicationPaletteChange) rebuild();
}

void CardWidget::show_whole(bool on) {
  whole_ = on;
  QMetaObject::invokeMethod(this, [this] { rebuild(); }, Qt::QueuedConnection);  // ⌃ is a widget of the card
}

bool CardWidget::eventFilter(QObject *o, QEvent *e) {
  // ↑↓ in the field: through the recent look-ups.
  if (e->type() == QEvent::KeyPress && recents_ && qobject_cast<QLineEdit *>(o)) {
    int key = static_cast<QKeyEvent *>(e)->key(), n = recents_->count();
    if ((key == Qt::Key_Down || key == Qt::Key_Up) && n > 0) {
      int at = recents_->currentRow() + (key == Qt::Key_Down ? 1 : -1);
      recents_->setCurrentRow(std::clamp(at, -1, n - 1));
      return true;
    }
  }
  if (e->type() == QEvent::MouseButtonRelease && o->objectName() == "cut-title") {
    QMetaObject::invokeMethod(this, [this] { show_whole(true); }, Qt::QueuedConnection);
    return true;
  }
  return QWidget::eventFilter(o, e);
}

// The card's tabs (the open one bold), a smaller size when they would not fit.
QWidget *CardWidget::tabs(const CardView &v) {
  auto *w = new QWidget;
  auto *l = new QHBoxLayout(w);
  l->setContentsMargins(0, 0, 0, 0);
  l->setSpacing(12);
  // The tabs in one row: a smaller size when they would not fit the card.
  int room = (embedded_ ? std::max(width(), 300) : (v.width > 0 ? v.width : 400)) - 28, size = -1;
  for (; size > -4; --size) {
    int need = 12 * static_cast<int>(v.tab_items.size() - 1);
    for (auto &t : v.tab_items) need += QFontMetrics(font(size, true)).horizontalAdvance(q(t.title));
    if (need <= room) break;
  }
  for (auto &t : v.tab_items) {
    auto tab = t.tab;
    auto *b = new fluent::Tab(q(t.title), t.tab == v.tab);
    b->setFont(font(size, t.tab == v.tab));
    connect(b, &fluent::Tab::clicked, this, [this, tab] { m_.select(tab); });
    l->addWidget(b);
  }
  l->addStretch();
  return w;
}

QWidget *CardWidget::row(const CardRow &r, const CardView &) {
  using S = CardRow::Style;
  switch (r.style) {
    case S::entry: {
      auto *w = new QWidget;
      auto *l = new QVBoxLayout(w);
      l->setContentsMargins(0, 0, 0, 0);
      l->setSpacing(2);
      if (!r.label.empty()) {
        auto *d = label(q(r.label), font(-2), false, false);
        ui::grey(d);
        l->addWidget(d);
      }
      auto body = r.html.empty() ? q(r.text).toHtmlEscaped() : q(r.html);
      if (r.open == "entry") {
        // A dictionary's short line (its headword and meaning): a click opens its full entry alone.
        auto entry = r.entry;
        auto *b = label(plain_link(q(r.text).toHtmlEscaped()), font(), true, false);
        b->setCursor(Qt::PointingHandCursor);
        b->setToolTip(t("card_entry_tip"));
        connect(b, &QLabel::linkActivated, this, [this, entry] { m_.open_entry(entry); });
        l->addWidget(b);
      } else {
        l->addWidget(label(body, font(), true));
      }
      return w;
    }
    case S::gloss: {
      auto html = "<b>" + q(r.label).toHtmlEscaped() + "</b>" + (r.text.empty() ? "" : " <span style='color: gray'>" + q(r.text).toHtmlEscaped() + "</span>");
      auto *l = label(html, font(), true, false);
      if (!r.link.empty()) {
        auto word = r.link;
        l->setCursor(Qt::PointingHandCursor);
        l->setToolTip(t("card_open_word", word));
        connect(l, &QLabel::linkActivated, this, [this, word] { m_.open(word); });
        l->setText(plain_link(html));
      }
      return l;
    }
    case S::heading: return label(q(r.text), font(0, true));
    case S::sense: return label("<span style='color: gray'>" + q(r.label) + "</span>&nbsp;&nbsp;" + q(r.text).toHtmlEscaped(), font(), true);
    case S::example: {
      auto *l = label(q(r.text), font(-1));
      ui::grey(l);
      l->setContentsMargins(24, 0, 0, 0);
      return l;
    }
    case S::section: {
      auto entry = r.entry;
      auto key = r.open;
      auto html = q(r.label).toHtmlEscaped() + "<br><span style='color: gray; font-size: small'>" + q(r.text).toHtmlEscaped() + "</span>";
      auto *l = label(plain_link(html), font(), true, false);
      l->setCursor(Qt::PointingHandCursor);
      connect(l, &QLabel::linkActivated, this, [this, entry, key] { m_.open_section(entry, key); });
      return l;
    }
    case S::text: return label(q(r.text), font(1));
    case S::markdown: return label(markdown(r.text), font(), true);
    case S::english: {
      auto *l = label(q(r.text), font(-1));
      ui::grey(l);
      return l;
    }
    case S::error: {
      auto *l = label(q(r.text), font());
      l->setStyleSheet("color: #d0342c");
      return l;
    }
    default: {
      auto *l = label(q(r.text), font(), false, false);
      ui::grey(l);
      return l;
    }
  }
}

// The bottom row (InkPeek's): ←, the method, a pager; copy, the badge, ☆.
QWidget *CardWidget::bar(const CardView &v) {
  auto *w = new QWidget;
  w->setObjectName("bar");
  if (embedded_) w->setObjectName("inline-bar");  // the flyout's bar is styled by fluent's sheet
  auto *l = new QHBoxLayout(w);
  l->setContentsMargins(embedded_ ? 0 : 10, 4, embedded_ ? 0 : 8, 4);
  l->setSpacing(4);
  auto f = font(-2);
  // ← with where it goes as its tooltip.
  if (!v.back.empty()) l->addWidget(command(fluent::Glyph::back, q(v.back), [this] { m_.back(); }));
  if (v.menu) {
    auto *b = menu_button(q(v.menu_title) + " ▾", f);
    b->setToolTip(q(v.menu_help));
    auto *menu = new QMenu(b);
    for (auto &m : v.methods) {
      auto *a = menu->addAction(q(m.title));
      a->setEnabled(m.enabled);
      auto method = m.method;
      bool ai = m.ai;
      connect(a, &QAction::triggered, this, [this, method, ai] { ai ? m_.choose_ai() : m_.choose(method); });
    }
    b->setMenu(menu);
    l->addWidget(b);
  }
  if (!v.pager.empty()) {
    auto *prev = command(fluent::Glyph::left, {}, [this] { m_.turn(-1); });
    prev->setEnabled(v.prev);
    l->addWidget(prev);
    auto *pager = label(q(v.pager), f, false, false);
    pager->setWordWrap(false);
    ui::grey(pager);
    l->addWidget(pager);
    auto *next = command(fluent::Glyph::right, {}, [this] { m_.turn(1); });
    next->setEnabled(v.next);
    l->addWidget(next);
  }
  l->addStretch();
  // A pager's row has no room for copy (and a page of words has nothing to copy).
  if (!v.copy.empty() && v.pager.empty()) {
    auto text = q(v.copy);
    l->addWidget(command(fluent::Glyph::copy, t("card_copy"), [text] { QApplication::clipboard()->setText(text); }));
  }
  if (!v.badge.empty() || !v.books.empty() || !v.answers.empty()) {
    auto *b = menu_button(q(v.badge) + (v.books.empty() && v.answers.empty() ? "" : " ▾"), f);
    if (!v.books.empty() || !v.answers.empty()) {
      auto *menu = new QMenu(b);
      menu->addSection(t("card_text_in"));
      for (auto &x : v.books) {
        auto *a = menu->addAction(q(x.title));
        a->setCheckable(true), a->setChecked(x.on);
        auto code = x.code;
        connect(a, &QAction::triggered, this, [this, code] { m_.relookup(code, ""); });
      }
      menu->addSection(t("card_into"));
      for (auto &x : v.answers) {
        auto *a = menu->addAction(q(x.title));
        a->setCheckable(true), a->setChecked(x.on);
        auto code = x.code;
        connect(a, &QAction::triggered, this, [this, code] { m_.relookup("", code); });
      }
      b->setMenu(menu);
    }
    l->addWidget(b);
  }
  if (v.can_save) {
    l->addWidget(command(v.saved ? fluent::Glyph::star_filled : fluent::Glyph::star, q(v.star_help), [this] { m_.toggle_saved(); },
                         v.saved ? fluent::accent() : QColor()));
  }
  if (!embedded_) {
    l->addWidget(command(m_.pinned ? fluent::Glyph::pinned : fluent::Glyph::pin, t("card_pin"), [this] { m_.toggle_pin(); },
                         m_.pinned ? fluent::accent() : QColor()));
  }
  return w;
}

// The card's text field (typing): Return looks it up.
// The text field; on an empty card the recent look-ups under it (a click, or ↑↓ and Return, opens one).
QWidget *CardWidget::field() {
  auto *w = new QWidget;
  auto *l = new QVBoxLayout(w);
  l->setContentsMargins(0, 0, 0, 0);
  l->setSpacing(6);
  auto *e = new QLineEdit(typed_);
  e->setPlaceholderText(t("card_input_hint"));
  e->setFont(font());
  connect(e, &QLineEdit::textChanged, this, [this](const QString &s) { typed_ = s; });
  connect(e, &QLineEdit::returnPressed, this, [this] {
    auto *r = recents_ && typed_.trimmed().isEmpty() ? recents_->currentItem() : nullptr;
    emit submitted(r ? r->data(Qt::UserRole).toString() : typed_);
  });
  l->addWidget(e);
  recents_ = nullptr;
  if (!m_.has_card() && !m_.looking()) {
    auto seen = m_.recents();
    if (!seen.empty()) {
      auto *list = new QListWidget;
      list->setObjectName("recents");
      list->setFrameShape(QFrame::NoFrame);
      list->setFocusPolicy(Qt::NoFocus);
      list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
      list->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
      list->setFont(font(-1));
      for (auto &s : seen) {
        auto *item = new QListWidgetItem(q(app::menu_line(s.meaning.empty() ? s.text : s.text + " — " + s.meaning)));
        item->setData(Qt::UserRole, q(s.text));
        list->addItem(item);
      }
      list->setFixedHeight(list->sizeHintForRow(0) * list->count() + 4);
      connect(list, &QListWidget::itemClicked, this, [this](QListWidgetItem *i) { emit submitted(i->data(Qt::UserRole).toString()); });
      l->addWidget(list);
      recents_ = list;
      e->installEventFilter(this);
    }
  }
  if (!m_.has_card()) QMetaObject::invokeMethod(e, [e] { e->setFocus(); }, Qt::QueuedConnection);
  return w;
}

}  // namespace lexipeek::qt
