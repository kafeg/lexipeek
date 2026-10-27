// The first-run window (mac FirstRun): the first pair and its download, then the hotkey; the core's first_run_view
// says which step it is and what it shows.
#pragma once
#include <QWidget>
#include "core_client.hpp"

class QTimer;
class QVBoxLayout;

namespace lexipeek::qt {

class FirstRunWindow : public QWidget {
  Q_OBJECT
 public:
  explicit FirstRunWindow(CoreClient &client);
  FirstRunState state;
  void refresh();
  void start();  // the packs list fetched when there is none

 signals:
  void finished();  // Done or closed: it opens by itself again only while no pair is installed

 protected:
  void closeEvent(QCloseEvent *e) override;

 private:
  void primary();
  void other();
  void fetch();
  void install();
  CoreClient &c_;
  FirstRunView look_;
  QVBoxLayout *box_;
  app::Download dl_;
  QTimer *poll_;
};

}  // namespace lexipeek::qt
