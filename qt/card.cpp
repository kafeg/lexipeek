#include "card.hpp"
#include <QApplication>
#include <QClipboard>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QRegularExpression>
#include <QScreen>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

namespace lexipeek::qt {

CardModel::CardModel(CoreClient &client) : c_(client), lane_(client) {}

void CardModel::relook() {
  look_ = nav.has_card() ? card_view(nav.card(), nav.state) : CardView{};
  emit changed();
}

void CardModel::reset() {
  request_ = 0;
  nav.reset();
  relook();
}

void CardModel::lookup(const std::string &text) { run(nav.look(text, source, false, "", "")); }
void CardModel::open(const std::string &word) { run(nav.open(word)); }
void CardModel::relookup(const std::string &lang, const std::string &answer) { run(nav.relookup(lang, answer)); }
void CardModel::back() { run(nav.back()); }
void CardModel::open_section(int entry, const std::string &section) { run(nav.open_section(entry, section, 0)); }
void CardModel::turn(int by) { run(nav.turn(by)); }

void CardModel::open_entry(int entry) {
  nav.open_entry(entry);
  relook();
}

void CardModel::select(CardTab tab) {
  nav.select(tab);
  ask_ai();
  relook();
}

void CardModel::next_tab(int by) {
  nav.next_tab(by);
  ask_ai();
  relook();
}

void CardModel::toggle_entry() {
  nav.toggle_entry();
  relook();
}

void CardModel::choose(Method m) {
  auto s = app::settings(c_.data());
  app::set_method(s, app::method_id(m));
  app::save(c_.data(), s);
  c_.configure();
  nav.pick_method(m);
  translate(m);
}

void CardModel::choose_ai() {
  nav.pick_ai();
  ask_ai();
  relook();
}

void CardModel::toggle_saved() {
  if (!nav.has_card() || !look_.can_save) return;
  nav.state.saved = c_.engine.toggle_saved(nav.card(), shown_translation(nav.state));
  relook();
}

void CardModel::run(const CardStep &step) {
  switch (step.kind) {
    case CardStep::Kind::lookup:
      request_ = lane_.lookup(step.text, step.source, step.lang, step.answer, [this](int id, Card card) {
        if (id != request_) return;
        nav.arrived(card);
        if (nav.state.has_method) translate(nav.state.method);
        ask_ai();
        relook();
      });
      relook();
      break;
    case CardStep::Kind::restore:
      request_ = lane_.restore(nav.card());
      if (step.translate) translate(nav.state.method);
      ask_ai();
      relook();
      break;
    case CardStep::Kind::section: {
      auto id = request_;
      auto e = c_.engine;
      auto card = nav.card();
      c_.run<SectionPage>(
          c_.lookups, [e, card, step] { return e.section(card, step.entry, step.key, step.page); },
          [this, id](SectionPage p) {
            if (id != request_) return;
            nav.section_arrived(p);
            relook();
          });
      break;
    }
    default:
      relook();
  }
}

void CardModel::translate(Method m) {
  if (!request_) return;
  nav.translating(m);
  auto id = request_;
  lane_.translate(id, m, [this, id, m](Translation t) {
    if (id != request_) return;
    nav.translated(m, t);
    relook();
  });
}

// The AI answers the card shows now (the core says which: only on the person's action), the open one first.
void CardModel::ask_ai() {
  if (!nav.has_card() || !request_) return;
  auto asks = card_asks(nav.card(), nav.state);
  if (asks.empty()) return;
  auto id = request_;
  auto k = key();
  for (auto a : asks) {
    AiState asking;
    asking.asking = true;
    nav.state = with_answer(nav.state, a, asking);
    lane_.ask(id, nav.card(), a, k, [this, id, a](AiState st) {
      if (id != request_) return;
      nav.state = with_answer(nav.state, a, st);
      relook();
    });
  }
}

namespace {

// An AI answer's light Markdown (**bold**, *italic*) as Qt's rich text, its lines kept.
QString markdown(const std::string &s) {
  auto h = q(s).toHtmlEscaped();
  h.replace(QRegularExpression("\\*\\*(.+?)\\*\\*"), "<b>\\1</b>");
  h.replace(QRegularExpression("\\*(.+?)\\*"), "<i>\\1</i>");
  return h.replace("\n", "<br>");
}

QLabel *label(const QString &text, const QFont &f, bool rich = false, bool select = true) {
  auto *l = new QLabel(text);
  l->setFont(f);
  l->setWordWrap(true);
  l->setTextFormat(rich ? Qt::RichText : Qt::PlainText);
  if (select) l->setTextInteractionFlags(Qt::TextSelectableByMouse);
  return l;
}

void grey(QWidget *w) {
  auto p = w->palette();
  p.setColor(QPalette::WindowText, p.color(QPalette::PlaceholderText));
  w->setPalette(p);
}

QToolButton *link(const QString &text, const QFont &f, std::function<void()> click) {
  auto *b = new QToolButton;
  b->setText(text);
  b->setFont(f);
  b->setAutoRaise(true);
  b->setCursor(Qt::PointingHandCursor);
  b->setStyleSheet("QToolButton { color: palette(link); border: none; padding: 0; }");
  QObject::connect(b, &QToolButton::clicked, click);
  return b;
}

}  // namespace

CardWidget::CardWidget(CardModel &model, bool embedded, QWidget *parent) : QWidget(parent), m_(model), embedded_(embedded) {
  box_ = new QVBoxLayout(this);
  box_->setContentsMargins(0, 0, 0, 0);
  box_->setSpacing(0);
  connect(&m_, &CardModel::changed, this, &CardWidget::rebuild);
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
  while (auto *item = box_->takeAt(0)) {
    delete item->widget();
    delete item;
  }
  if (!embedded_) setFixedWidth(v_.width > 0 ? v_.width : 400);
  if (!m_.has_card()) {
    if (m_.looking()) box_->addWidget(label("…", font(), false, false));
    return;
  }
  auto *top = new QWidget;
  auto *tl = new QVBoxLayout(top);
  tl->setContentsMargins(14, 14, 14, 8);
  tl->setSpacing(8);
  tl->addWidget(header(v_));
  if (v_.tabs) tl->addWidget(tabs(v_));
  box_->addWidget(top);

  auto *body = new QWidget;
  auto *bl = new QVBoxLayout(body);
  bl->setContentsMargins(14, 0, 24, 8);
  bl->setSpacing(8);
  for (auto &r : v_.rows) bl->addWidget(row(r, v_));
  if (!v_.fix.empty()) bl->addWidget(link(q(v_.fix), font(-2), [this] { emit openPane(static_cast<int>(SettingsSection::ai), 0); }));
  bl->addStretch();
  auto *scroll = new QScrollArea;
  scroll->setWidget(body);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  if (!embedded_) {
    auto *screen = QApplication::primaryScreen();
    int limit = screen ? screen->availableGeometry().height() * (v_.height > 0 ? v_.height : 45) / 100 : 500;
    body->setFixedWidth(width());
    body->adjustSize();
    scroll->setFixedHeight(std::min(body->sizeHint().height(), limit));
  }
  box_->addWidget(scroll, 1);
  box_->addWidget(bar(v_));
  if (!embedded_) adjustSize();
}

QWidget *CardWidget::header(const CardView &v) {
  auto *w = new QWidget;
  auto *l = new QVBoxLayout(w);
  l->setContentsMargins(0, 0, 0, 0);
  l->setSpacing(2);
  auto f = v.style == HeaderStyle::word ? font(4, true) : v.style == HeaderStyle::phrase ? font(0, true) : font(-1);
  auto *title = label(q(v.title), f, false, false);
  title->setToolTip(q(v.tooltip));
  // A passage's header: one grey line, cut at the card's width (the whole text in the tooltip).
  if (v.style == HeaderStyle::passage) {
    grey(title);
    title->setWordWrap(false);
    title->setText(QFontMetrics(f).elidedText(q(v.title), Qt::ElideRight, (v.width > 0 ? v.width : 400) - 28));
  }
  l->addWidget(title);
  if (!v.subtitle.empty()) {
    auto *s = label(q(v.subtitle), font(-2), false, false);
    grey(s);
    l->addWidget(s);
  }
  return w;
}

// Text tabs with a line under the open one (the pop-up card's).
QWidget *CardWidget::tabs(const CardView &v) {
  auto *w = new QWidget;
  auto *l = new QHBoxLayout(w);
  l->setContentsMargins(0, 0, 0, 0);
  l->setSpacing(14);
  for (auto &t : v.tab_items) {
    bool on = t.tab == v.tab;
    auto *b = new QToolButton;
    b->setText(q(t.title));
    b->setFont(font(-1, on));
    b->setAutoRaise(true);
    b->setCursor(Qt::PointingHandCursor);
    b->setStyleSheet(on ? "QToolButton { border: none; border-bottom: 2px solid palette(highlight); padding: 0 0 4px 0; }"
                        : "QToolButton { border: none; color: palette(placeholder-text); padding: 0 0 4px 0; }");
    auto tab = t.tab;
    connect(b, &QToolButton::clicked, this, [this, tab] { m_.select(tab); });
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
        grey(d);
        l->addWidget(d);
      }
      auto body = r.html.empty() ? q(r.text).toHtmlEscaped() : q(r.html);
      if (r.open == "entry") {
        auto entry = r.entry;
        auto *b = label("<a href='#' style='text-decoration: none; color: palette(text)'>" + body + "</a>", font(), true, false);
        b->setCursor(Qt::PointingHandCursor);
        b->setToolTip(t("card_full"));
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
        l->setText("<a href='#' style='text-decoration: none; color: palette(text)'>" + html + "</a>");
      }
      return l;
    }
    case S::heading: return label(q(r.text), font(0, true));
    case S::sense: return label("<span style='color: gray'>" + q(r.label) + "</span>&nbsp;&nbsp;" + q(r.text).toHtmlEscaped(), font(), true);
    case S::example: {
      auto *l = label(q(r.text), font(-1));
      grey(l);
      l->setContentsMargins(24, 0, 0, 0);
      return l;
    }
    case S::section: {
      auto entry = r.entry;
      auto key = r.open;
      auto html = q(r.label).toHtmlEscaped() + "<br><span style='color: gray; font-size: small'>" + q(r.text).toHtmlEscaped() + "</span>";
      auto *l = label("<a href='#' style='text-decoration: none; color: palette(text)'>" + html + "</a>", font(), true, false);
      l->setCursor(Qt::PointingHandCursor);
      connect(l, &QLabel::linkActivated, this, [this, entry, key] { m_.open_section(entry, key); });
      return l;
    }
    case S::text: return label(q(r.text), font(1));
    case S::markdown: return label(markdown(r.text), font(), true);
    case S::english: {
      auto *l = label(q(r.text), font(-1));
      grey(l);
      return l;
    }
    case S::error: {
      auto *l = label(q(r.text), font());
      l->setStyleSheet("color: #d0342c");
      return l;
    }
    default: {
      auto *l = label(q(r.text), font(), false, false);
      grey(l);
      return l;
    }
  }
}

// The bottom row (InkPeek's): back, the full entry, the method, a section's pager; copy, the badge, ☆.
QWidget *CardWidget::bar(const CardView &v) {
  auto *w = new QWidget;
  w->setObjectName("bar");
  if (!embedded_) w->setStyleSheet("#bar { border-top: 1px solid palette(mid); background: palette(alternate-base); }");
  auto *l = new QHBoxLayout(w);
  l->setContentsMargins(embedded_ ? 0 : 14, 6, embedded_ ? 0 : 14, 6);
  l->setSpacing(12);
  auto f = font(-2);
  if (!v.back.empty()) l->addWidget(link(q(v.back), f, [this] { m_.back(); }));
  if (!v.entry_toggle.empty()) l->addWidget(link(q(v.entry_toggle), f, [this] { m_.toggle_entry(); }));
  if (v.menu) {
    auto *b = new QToolButton;
    b->setText(q(v.menu_title) + " ▾");
    b->setFont(f);
    b->setAutoRaise(true);
    b->setToolTip(q(v.menu_help));
    b->setPopupMode(QToolButton::InstantPopup);
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
    l->addWidget(link("◀", f, [this] { m_.turn(-1); }));
    l->addWidget(label(q(v.pager), f, false, false));
    l->addWidget(link("▶", f, [this] { m_.turn(1); }));
  }
  l->addStretch();
  if (!v.copy.empty()) {
    auto text = q(v.copy);
    auto *c = link("⧉", font(0), [text] { QApplication::clipboard()->setText(text); });
    c->setToolTip(t("card_copy"));
    l->addWidget(c);
  }
  if (!v.badge.empty() || !v.books.empty() || !v.answers.empty()) {
    auto *b = new QToolButton;
    b->setText(q(v.badge) + (v.books.empty() && v.answers.empty() ? "" : " ▾"));
    b->setFont(f);
    b->setAutoRaise(true);
    if (!v.books.empty() || !v.answers.empty()) {
      b->setPopupMode(QToolButton::InstantPopup);
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
    auto *s = link(v.saved ? "★" : "☆", font(1), [this] { m_.toggle_saved(); });
    s->setToolTip(q(v.star_help));
    l->addWidget(s);
  }
  return w;
}

}  // namespace lexipeek::qt
