#!/bin/sh
# The Qt shell's snapshots of the Mac app's cases (scripts/snapshot-cases.sh), light and dark, + index.html; nothing on the
# screen. Usage: [UI_LANG=de] scripts/qt-snapshots.sh [out-dir]   (after building lexipeek-qt; QT_APP: its path, else
# build-qt/qt/lexipeek-qt; packs from the default data dir or LEXIPEEK_DATA)
set -eu
cd "$(dirname "$0")/.."
APP=${QT_APP:-$PWD/build-qt/qt/lexipeek-qt}
UI_LANG=${UI_LANG:-}
OUT=${1:-build-qt/snapshots${UI_LANG:+-$UI_LANG}}
case "$(uname)" in
  Darwin) DEF="$HOME/Library/Application Support/LexiPeek" ;;
  MINGW* | MSYS* | CYGWIN*) DEF="$LOCALAPPDATA/LexiPeek" ;;
  *) DEF="${XDG_DATA_HOME:-$HOME/.local/share}/LexiPeek" ;;
esac
DATA=${LEXIPEEK_DATA:-$DEF}
[ -x "$APP" ] || [ -x "$APP.exe" ] || { echo "no $APP: build lexipeek-qt" >&2; exit 1; }
[ -d "$DATA" ] || { echo "no data dir $DATA" >&2; exit 1; }
rm -rf "$OUT"
mkdir -p "$OUT/all"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
. scripts/snapshot-cases.sh

# A data dir with the packs of DATA and an empty store; extra: a Python line changing the settings s.
data() {
  mkdir "$1"
  for f in "$DATA"/*; do
    case "${f##*/}" in log | device | lexipeek.db* | settings.json | backup) ;; *) ln -s "$f" "$1/" ;; esac
  done
  python3 -c 'import json, sys
try: s = json.load(open(sys.argv[1]))
except (OSError, ValueError): s = {}
s["mt_english"] = True
if sys.argv[3]: s["ui_lang"] = sys.argv[3]
exec(sys.argv[4])
json.dump(s, open(sys.argv[2], "w"))' "$DATA/settings.json" "$1/settings.json" "$UI_LANG" "${2:-pass}"
}
fail=0
run() { "$APP" "$@" 2>/dev/null || fail=1; }

# The packs: cards, the window, Settings, About.
data "$TMP/data"
cases >"$TMP/cases"
run --data "$TMP/data" --snapshots "$OUT/all" --cases "$TMP/cases"

# An empty data dir: the first run's steps (Windows has no Accessibility step) and Translation without a pair.
mkdir "$TMP/empty"
echo "{\"ui_lang\": \"$UI_LANG\"}" >"$TMP/empty/settings.json"
firstrun_cases | grep -v -e '^firstrun-access|' -e '^firstrun-done-later|' | sed 's/^\([^|]*\)|\([^|]*\)|.*/\1|firstrun?step=\2/' >"$TMP/firstrun"
run --data "$TMP/empty" --snapshots "$TMP/empty-out" --cases "$TMP/firstrun" --firstrun-from "$DATA" --suffix -empty
mv "$TMP"/empty-out/firstrun-*.png "$OUT/all/"
mv "$TMP"/empty-out/settings-languages-translation-empty*.png "$OUT/all/"

# AI on (Claude; no key, no network: answers from files): its cards and its settings (-on).
data "$TMP/aidata" 's["ai"] = {"provider": "anthropic"}'
ai_fixtures "$TMP/fixtures"
mkdir "$TMP/ai"
ai_cases >"$TMP/ai_cases"
LEXIPEEK_AI_REPLIES="$TMP/ai" run --data "$TMP/aidata" --snapshots "$TMP/ai-out" --cases "$TMP/ai_cases" --ai-fixtures "$TMP/fixtures" --suffix -on
mv "$TMP"/ai-out/ai-*.png "$OUT/all/"
for s in settings-ai-connection settings-ai-modules settings-card-opening; do mv "$TMP"/ai-out/"$s"-on*.png "$OUT/all/" 2>/dev/null || true; done

# One page, the Mac script's order, light and dark side by side.
{
  echo '<!doctype html><meta charset="utf-8"><title>LexiPeek Qt snapshots</title>'
  echo '<style>body{font:13px system-ui;background:#888;margin:16px}h2{margin:24px 0 6px}'
  echo '.row{display:flex;gap:16px;align-items:flex-start}img{zoom:.75}</style>'
  for n in $(cut -d'|' -f1 "$TMP/cases" "$TMP/firstrun" "$TMP/ai_cases") $(settings_shots); do
    for s in "$n" "$n-end"; do
      [ -e "$OUT/all/$s.png" ] || continue
      echo "<h2>$s</h2><div class=row><img src=\"all/$s.png\"><img src=\"all/$s-dark.png\"></div>"
    done
  done
} >"$OUT/index.html"
echo "$OUT/index.html ($(ls "$OUT/all" | wc -l | tr -d ' ') pictures)"
exit $fail
