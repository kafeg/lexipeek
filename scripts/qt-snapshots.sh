#!/bin/sh
# The Qt shell's snapshots (cards, the window, Settings, About, first run), light and dark, + index.html; nothing on the
# screen (Qt's minimal platform). Usage: [UI_LANG=de] scripts/qt-snapshots.sh [out-dir]   (after building lexipeek-qt;
# QT_APP: its path, else build-qt/qt/lexipeek-qt; packs from the default data dir or LEXIPEEK_DATA)
set -eu
cd "$(dirname "$0")/.."
APP=${QT_APP:-$PWD/build-qt/qt/lexipeek-qt}
UI_LANG=${UI_LANG:-}
OUT=${1:-build-qt/snapshots${UI_LANG:+-$UI_LANG}}
case "$(uname)" in
  Darwin) DEF="$HOME/Library/Application Support/LexiPeek" ;;
  MINGW* | MSYS* | CYGWIN*) DEF="$APPDATA/LexiPeek" ;;
  *) DEF="${XDG_DATA_HOME:-$HOME/.local/share}/LexiPeek" ;;
esac
DATA=${LEXIPEEK_DATA:-$DEF}
[ -x "$APP" ] || [ -x "$APP.exe" ] || { echo "no $APP: build lexipeek-qt" >&2; exit 1; }
[ -d "$DATA" ] || { echo "no data dir $DATA" >&2; exit 1; }
rm -rf "$OUT"
mkdir -p "$OUT"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

# The packs of the data dir, an empty history, the run's app language.
settings() {
  python3 -c 'import json, sys
try: s = json.load(open(sys.argv[1]))
except (OSError, ValueError): s = {}
if sys.argv[3]: s["ui_lang"] = sys.argv[3]
json.dump(s, open(sys.argv[2], "w"))' "$1" "$2" "$UI_LANG"
}
mkdir "$TMP/data"
for f in "$DATA"/*; do
  case "${f##*/}" in log | device | lexipeek.db* | settings.json | backup) ;; *) ln -s "$f" "$TMP/data/" ;; esac
done
settings "$DATA/settings.json" "$TMP/data/settings.json"
"$APP" --data "$TMP/data" --snapshots "$OUT/all" 2>/dev/null || fail=$?

# The first run on a data dir with only the packs list: its pair step.
mkdir "$TMP/empty"
[ ! -e "$DATA/packs.json" ] || cp "$DATA/packs.json" "$TMP/empty/"
settings /dev/null "$TMP/empty/settings.json"
"$APP" --data "$TMP/empty" --snapshots "$TMP/first" 2>/dev/null || true
for f in "$TMP"/first/firstrun*.png; do cp "$f" "$OUT/all/$(basename "$f" | sed 's/firstrun/firstrun-pair/')"; done

{
  echo '<!doctype html><meta charset="utf-8"><title>LexiPeek Qt snapshots</title>'
  echo '<style>body{font:13px system-ui;background:#888;margin:16px}h2{margin:24px 0 6px}'
  echo '.row{display:flex;gap:16px;align-items:flex-start}img{zoom:.75}</style>'
  for f in "$OUT"/all/*.png; do
    n=$(basename "$f" .png)
    case "$n" in *-dark) continue ;; esac
    echo "<h2>$n</h2><div class=row><img src=\"all/$n.png\"><img src=\"all/$n-dark.png\"></div>"
  done
} >"$OUT/index.html"
echo "$OUT/index.html"
exit "${fail:-0}"
