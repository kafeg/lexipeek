// Settings → Packs (mac PacksView): the core's packs_view; downloads and removals here.
#pragma once
#include <QWidget>
#include "core_client.hpp"

class QVBoxLayout;
class QTimer;

namespace lexipeek::qt {

class PacksPane : public QWidget {
  Q_OBJECT
 public:
  explicit PacksPane(CoreClient &client);
  void draw();

 signals:
  void changed();  // packs installed or removed

 private:
  void refresh();
  void run(const PackAction &a);
  void finished(const Status &s);
  QWidget *line(const PackLine &l);
  CoreClient &c_;
  QVBoxLayout *body_;
  PacksView view_;
  std::optional<std::string> busy_;  // what the progress says ("" while the list is fetched)
  double progress_ = 0;
  std::string error_, list_error_;
  app::Download dl_;
  QTimer *poll_;
};

}  // namespace lexipeek::qt
