#include "platform.hpp"
#include <QGuiApplication>
#include <QScreen>
#include <QWidget>

namespace lexipeek::qt::platform {

std::optional<Selected> selection() {
  auto s = os::selection();
  if (!s) return std::nullopt;
  Selected out{s->text, {}};
  if (!s->bounds.empty()) out.bounds = QRect(s->bounds.x, s->bounds.y, s->bounds.w, s->bounds.h);
  return out;
}

// The OS gives physical pixels; Qt's windows go by logical ones on the screen there.
QRect logical(const QRect &px) {
#ifdef _WIN32
  if (px.isEmpty()) return px;
  for (auto *s : QGuiApplication::screens()) {
    auto g = s->geometry();
    auto r = s->devicePixelRatio();
    QRect phys(g.topLeft(), g.size() * r);
    if (phys.contains(px.center())) {
      auto p = g.topLeft() + (px.topLeft() - g.topLeft()) / r;
      return {p, px.size() / r};
    }
  }
#endif
  return px;
}

bool hand_over(const QStringList &args) {
  std::vector<std::string> a;
  for (auto &x : args) a.push_back(x.toStdString());
  return os::hand_over(a);
}

void receive(std::function<void(const QStringList &)> got) {
  os::receive([got](const std::vector<std::string> &args) {
    QStringList a;
    for (auto &x : args) a << QString::fromStdString(x);
    got(a);
  });
}

void activate(QWidget *w) {
#ifdef _WIN32
  os::activate(reinterpret_cast<void *>(w->winId()));
#endif
  w->raise();
  w->activateWindow();
}

void round([[maybe_unused]] QWidget *w) {
#ifdef _WIN32
  os::round(reinterpret_cast<void *>(w->winId()));
#endif
}

QImage glyph_mask(const QString &family, char16_t code, int px) {
  auto mask = os::glyph_mask(family.toStdString(), code, px);
  if (mask.empty()) return {};
  QImage out(px, px, QImage::Format_Alpha8);
  for (int y = 0; y < px; ++y) std::copy_n(mask.data() + y * px, px, out.scanLine(y));
  return out;
}

}  // namespace lexipeek::qt::platform
