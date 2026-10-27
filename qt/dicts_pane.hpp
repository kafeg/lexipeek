// Settings → Languages → Other dictionaries (mac OtherDictionariesPane): the core's rows (packs and own ones in the
// card's order), on / off, order, folders read in place, import (also by a drop), the catalog of public sources.
#pragma once
#include <QDialog>
#include <QWidget>
#include "core_client.hpp"

class QLineEdit;
class QVBoxLayout;
class QTimer;

namespace lexipeek::qt {

class DictsPane : public QWidget {
  Q_OBJECT
 public:
  explicit DictsPane(CoreClient &client);
  void draw();

 protected:
  void dragEnterEvent(QDragEnterEvent *e) override;
  void dropEvent(QDropEvent *e) override;

 private:
  void import(const std::vector<std::string> &files);
  void changed(bool reload);
  CoreClient &c_;
  QVBoxLayout *body_;
  std::string message_;
  bool busy_ = false;
};

// The catalog: the switched-on sources' lists (fetched on opening), a filter, one download at a time with progress.
class CatalogDialog : public QDialog {
  Q_OBJECT
 public:
  explicit CatalogDialog(CoreClient &client, QWidget *parent);

 private:
  void fetch();
  void draw();
  void get(int item, const std::string &title);
  CoreClient &c_;
  CatalogList list_;
  bool loading_ = false;
  std::string filter_, downloading_, message_, failed_;
  double progress_ = 0;
  app::Download dl_;
  QTimer *poll_;
  QLineEdit *filter_edit_;
  QVBoxLayout *body_;
};

}  // namespace lexipeek::qt
