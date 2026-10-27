#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QLineEdit>
#include <QLocale>
#include <QStyle>
#include <QStyleFactory>
#include <QTimer>
#include <QVBoxLayout>
#include <cstdlib>
#include "card.hpp"

using namespace lexipeek;
using namespace lexipeek::qt;

namespace {

QPalette dark() {
  QPalette p;
  p.setColor(QPalette::Window, QColor(0x2b, 0x2b, 0x2b));
  p.setColor(QPalette::WindowText, QColor(0xe8, 0xe8, 0xe8));
  p.setColor(QPalette::Base, QColor(0x1e, 0x1e, 0x1e));
  p.setColor(QPalette::AlternateBase, QColor(0x33, 0x33, 0x33));
  p.setColor(QPalette::Text, QColor(0xe8, 0xe8, 0xe8));
  p.setColor(QPalette::Button, QColor(0x3a, 0x3a, 0x3a));
  p.setColor(QPalette::ButtonText, QColor(0xe8, 0xe8, 0xe8));
  p.setColor(QPalette::PlaceholderText, QColor(0x9a, 0x9a, 0x9a));
  p.setColor(QPalette::Highlight, QColor(0x3d, 0x8b, 0xfd));
  p.setColor(QPalette::Link, QColor(0x6c, 0xa8, 0xff));
  p.setColor(QPalette::Mid, QColor(0x50, 0x50, 0x50));
  return p;
}

struct Case {
  const char *name, *text;
  CardTab tab;
};

// The cards `--snapshots <dir>` draws (Qt's minimal platform: nothing on the screen), light and dark.
const Case cases[] = {
    {"word", "Haus", CardTab::dictionary},
    {"word-translate", "Haus", CardTab::translate},
    {"phrase", "Ich mache die Tür zu", CardTab::translate},
    {"phrase-words", "Ich mache die Tür zu", CardTab::dictionary},
    {"passage", "Das Haus steht am Ende der Straße. Dort wohnt meine Großmutter seit vielen Jahren.", CardTab::translate},
    {"unknown", "Qwxzt", CardTab::translate},
};

int snapshots(QApplication &app, CoreClient &client, const QString &dir) {
  QDir().mkpath(dir);
  app.setQuitOnLastWindowClosed(false);
  QApplication::setStyle(QStyleFactory::create("Fusion"));
  auto light = QApplication::style()->standardPalette();
  int i = 0, failed = 0;
  bool is_dark = false;
  auto model = new CardModel(client);
  model->source = Source::cli;
  CardWidget *card = nullptr;
  QElapsedTimer since;
  auto next = [&] {
    if (i == std::size(cases) * 2) return app.quit();
    is_dark = i >= static_cast<int>(std::size(cases));
    QApplication::setPalette(is_dark ? dark() : light);
    delete card;
    card = new CardWidget(*model);
    card->setAttribute(Qt::WA_DontShowOnScreen);
    card->show();
    model->reset();
    model->lookup(cases[i % std::size(cases)].text);
    since.start();
  };
  auto *poll = new QTimer(&app);
  QObject::connect(poll, &QTimer::timeout, [&] {
    auto &c = cases[i % std::size(cases)];
    bool done = model->has_card() && !model->busy();
    if (!done && since.elapsed() < 60000) return;
    // A tab picked: the shot on the next tick (a layout shows its new widgets by a queued call).
    if (done && model->look().tab != c.tab) return model->select(c.tab);
    if (!done) ++failed;
    card->adjustSize();
    auto name = QString(c.name) + (is_dark ? "-dark" : "") + ".png";
    card->grab().save(dir + "/" + name);
    ++i;
    next();
  });
  next();
  poll->start(100);
  app.exec();
  return failed;
}

// The LexiPeek window for now: a text field and the card under it.
QWidget *window(CoreClient &client) {
  auto *w = new QWidget;
  w->setWindowTitle("LexiPeek");
  auto *box = new QVBoxLayout(w);
  auto *input = new QLineEdit;
  input->setPlaceholderText(t("card_input_hint"));
  auto *model = new CardModel(client);
  model->setParent(w);
  auto *card = new CardWidget(*model, true);
  box->addWidget(input);
  box->addWidget(card, 1);
  QObject::connect(input, &QLineEdit::returnPressed, [input, model] { model->lookup(input->text().toStdString()); });
  w->resize(520, 640);
  return w;
}

}  // namespace

int main(int argc, char **argv) {
  for (int i = 1; i < argc; ++i)
    if (std::string(argv[i]) == "--snapshots") qputenv("QT_QPA_PLATFORM", "minimal:enable_fonts");
  QApplication app(argc, argv);
  auto args = app.arguments();
  std::string data;
  if (auto i = args.indexOf("--data"); i > 0 && i + 1 < args.size()) data = args[i + 1].toStdString();
  else if (auto *env = std::getenv("LEXIPEEK_DATA")) data = env;
  app::set_language(data, QLocale::system().name().left(2).toStdString());
  CoreClient client(data);
  if (auto i = args.indexOf("--snapshots"); i > 0 && i + 1 < args.size()) return snapshots(app, client, args[i + 1]);
  auto *w = window(client);
  w->show();
  return app.exec();
}
