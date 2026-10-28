// `lexipeek-qt --snapshots <dir>`: the Mac app's snapshot cases (scripts/snapshot-cases.sh) drawn by the Qt shell, light
// and dark, never on the screen. Its data dir is the run's own (a look-up writes history, ☆ saves).
#pragma once
#include <QApplication>
#include "core_client.hpp"

namespace lexipeek::qt {

struct SnapshotOptions {
  QString out;
  QString cases;          // name|url[|answers] lines: lookup?…, input, unread, window?page=…, firstrun?step=…
  QString fixtures;       // AI answers per kind (<dir>/<answers>/*.json) copied into LEXIPEEK_AI_REPLIES before a case
  QString firstrun_from;  // the data dir whose packs.json / installed.json a first-run step copies in
  bool settings = true;   // every Settings section × tab and About
  QString suffix;         // added to the settings shots' names ("-on": AI on)
};

// The count of shots that were not final in time.
int snapshots(QApplication &app, CoreClient &client, const SnapshotOptions &o);

}  // namespace lexipeek::qt
