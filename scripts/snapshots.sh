#!/bin/sh
# The app's own snapshots of a fixed set of cards, of Notes and of every settings section × tab, light and dark, + index.html.
# Usage: [UI_LANG=de] scripts/snapshots.sh [out-dir]   (after `make app`; packs from the default data dir or LEXIPEEK_DATA;
# UI_LANG: the app's language for the run, one of en ru de fr es it pt pl; out-dir then build/snapshots-<lang>)
set -eu
cd "$(dirname "$0")/.."
APP=${APP:-$PWD/mac/build/Build/Products/Release/LexiPeek.app}
UI_LANG=${UI_LANG:-}
OUT=${1:-build/snapshots${UI_LANG:+-$UI_LANG}}
DATA=${LEXIPEEK_DATA:-$HOME/Library/Application Support/LexiPeek}
[ -x "$APP/Contents/MacOS/LexiPeek" ] || { echo "no $APP: make app" >&2; exit 1; }
[ -d "$DATA" ] || { echo "no data dir $DATA" >&2; exit 1; }
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)

# The packs of the data dir, but an empty history: the shots do not depend on it and do not add to it.
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
mkdir "$TMP/data"
for f in "$DATA"/*; do
  case "${f##*/}" in log | device | lexipeek.db* | settings.json) ;; *) ln -s "$f" "$TMP/data/" ;; esac
done
# The settings as they are, with Mozilla's English in between on (shown only by a fast translation through English) and
# the run's app language.
python3 -c 'import json, sys
try: s = json.load(open(sys.argv[1]))
except OSError: s = {}
s["mt_english"] = True
if sys.argv[3]: s["ui_lang"] = sys.argv[3]
json.dump(s, open(sys.argv[2], "w"))' "$DATA/settings.json" "$TMP/data/settings.json" "$UI_LANG"

. scripts/snapshot-cases.sh

# Percent-encodes everything but unreserved characters and the query's own = and &.
enc() { python3 -c 'import sys, urllib.parse; print(urllib.parse.quote(sys.argv[1], safe="=&?"))' "$1"; }

wait_for() {  # file, seconds; stops when the app has quit (another `pkill -x LexiPeek`)
  i=0
  while [ ! -e "$1" ] && [ $i -lt $(($2 * 10)) ]; do
    kill -0 "$pid" 2>/dev/null || { echo "the app quit, see $dir/app.log" >&2; exit 1; }
    sleep 0.1
    i=$((i + 1))
  done
  [ -e "$1" ]
}

cases >"$TMP/cases"
firstrun_cases >"$TMP/firstrun"
ai_cases >"$TMP/ai_cases"
ai_fixtures "$TMP/fixtures"
# The AI instance's data dir: the same packs, AI on with Claude's default model.
mkdir "$TMP/aidata"
for f in "$TMP/data"/*; do
  case "${f##*/}" in settings.json) ;; *) ln -s "$(readlink "$f")" "$TMP/aidata/" ;; esac
done
python3 -c 'import json, sys
s = json.load(open(sys.argv[1]))
s["ai"] = {"provider": "anthropic"}
json.dump(s, open(sys.argv[2], "w"))' "$TMP/data/settings.json" "$TMP/aidata/settings.json"
fail=0
for look in light dark; do
  dir="$OUT/$look"
  rm -rf "$dir" "$TMP/data/log" "$TMP/data/device" "$TMP/data"/lexipeek.db* && mkdir -p "$dir"
  LEXIPEEK_SNAPSHOT="$dir" LEXIPEEK_SNAPSHOT_APPEARANCE=$look LEXIPEEK_DATA="$TMP/data" \
    "$APP/Contents/MacOS/LexiPeek" >"$dir/app.log" 2>&1 &
  pid=$!
  wait_for "$dir/settings.done" 60 || { echo "$look: settings timed out" >&2; fail=1; }
  while IFS='|' read -r name url; do
    case "$url" in *\?*) q="$url&snap=$name" ;; *) q="$url?snap=$name" ;; esac
    echo "lexipeek://$(enc "$q")" >"$dir/request.tmp" && mv "$dir/request.tmp" "$dir/request"
    wait_for "$dir/$name.done" 90 && echo "$look/$name" || { echo "$look/$name: timed out" >&2; fail=1; }
  done <"$TMP/cases"
  kill "$pid" 2>/dev/null || true
  wait "$pid" 2>/dev/null || true

  # Another instance on an empty data dir (its settings shots: the Translation pane without a pair).
  empty="$TMP/empty" && rm -rf "$empty" "$dir/empty" && mkdir -p "$empty" "$dir/empty"
  [ -z "$UI_LANG" ] || echo "{\"ui_lang\": \"$UI_LANG\"}" >"$empty/settings.json"
  LEXIPEEK_SNAPSHOT="$dir/empty" LEXIPEEK_SNAPSHOT_APPEARANCE=$look LEXIPEEK_DATA="$empty" \
    "$APP/Contents/MacOS/LexiPeek" >"$dir/empty/app.log" 2>&1 &
  pid=$!
  wait_for "$dir/empty/settings.done" 60 || { echo "$look: empty settings timed out" >&2; fail=1; }
  mv "$dir/empty/settings-languages-translation.png" "$dir/settings-languages-translation-empty.png" 2>/dev/null || true
  while IFS='|' read -r name step file; do
    [ -z "$file" ] || cp "$DATA/$file" "$empty/"
    echo "lexipeek://firstrun?step=$step&snap=$name" >"$dir/empty/request.tmp" && mv "$dir/empty/request.tmp" "$dir/empty/request"
    if wait_for "$dir/empty/$name.done" 30; then mv "$dir/empty/$name.png" "$dir/" && echo "$look/$name"; else echo "$look/$name: timed out" >&2; fail=1; fi
  done <"$TMP/firstrun"
  kill "$pid" 2>/dev/null || true
  wait "$pid" 2>/dev/null || true
  rm -rf "$dir/empty"

  # AI on: its settings shots (-on) and its cards; the answers in $TMP/ai, refilled before each case.
  ai="$dir/ai" && rm -rf "$ai" "$TMP/ai" "$TMP/aidata/log" "$TMP/aidata/device" "$TMP/aidata"/lexipeek.db* && mkdir -p "$ai" "$TMP/ai"
  LEXIPEEK_SNAPSHOT="$ai" LEXIPEEK_SNAPSHOT_APPEARANCE=$look LEXIPEEK_DATA="$TMP/aidata" LEXIPEEK_AI_REPLIES="$TMP/ai" \
    "$APP/Contents/MacOS/LexiPeek" >"$ai/app.log" 2>&1 &
  pid=$!
  wait_for "$ai/settings.done" 60 || { echo "$look: AI settings timed out" >&2; fail=1; }
  for s in settings-ai-connection settings-ai-modules settings-card-opening; do
    mv "$ai/$s.png" "$dir/$s-on.png" 2>/dev/null || true
    [ ! -e "$ai/$s-end.png" ] || mv "$ai/$s-end.png" "$dir/$s-on-end.png"
  done
  while IFS='|' read -r name url answers; do
    rm -f "$TMP/ai"/*
    if [ "$answers" = error ]; then echo 401 >"$TMP/ai/status"; else cp "$TMP/fixtures/$answers"/*.json "$TMP/ai/"; fi
    echo "lexipeek://$(enc "$url&snap=$name")" >"$ai/request.tmp" && mv "$ai/request.tmp" "$ai/request"
    if wait_for "$ai/$name.done" 60; then
      mv "$ai/$name.png" "$dir/" && { [ ! -e "$ai/$name-end.png" ] || mv "$ai/$name-end.png" "$dir/"; } && echo "$look/$name"
    else
      echo "$look/$name: timed out" >&2
      fail=1
    fi
  done <"$TMP/ai_cases"
  kill "$pid" 2>/dev/null || true
  wait "$pid" 2>/dev/null || true
  rm -rf "$ai"
  rm -f "$dir"/*.done "$dir/card.png"
done

# One page with every shot in the order above, light and dark side by side (shown at 1×).
{
  echo '<!doctype html><meta charset="utf-8"><title>LexiPeek snapshots</title>'
  echo '<style>body{font:13px -apple-system;background:#888;margin:16px}h2{margin:24px 0 6px}'
  echo '.row{display:flex;gap:16px;align-items:flex-start}img{zoom:.5}</style>'
  for n in $(cut -d'|' -f1 "$TMP/cases" "$TMP/firstrun" "$TMP/ai_cases") $(settings_shots); do
    for s in "$n" "$n-end"; do
      [ -e "$OUT/light/$s.png" ] || continue
      echo "<h2>$s</h2><div class=row><img src=\"light/$s.png\"><img src=\"dark/$s.png\"></div>"
    done
  done
} >"$OUT/index.html"
echo "$OUT/index.html"
exit $fail
