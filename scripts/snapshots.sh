#!/bin/sh
# The app's own snapshots of a fixed set of cards and of every settings pane, light and dark, + index.html.
# Usage: scripts/snapshots.sh [out-dir]   (after `make app`; packs from the default data dir or LEXIPEEK_DATA)
set -eu
cd "$(dirname "$0")/.."
APP=${APP:-$PWD/mac/build/Build/Products/Release/LexiPeek.app}
OUT=${1:-build/snapshots}
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
  case "${f##*/}" in log | device | lexipeek.db*) ;; *) ln -s "$f" "$TMP/data/" ;; esac
done

LONG="Am nächsten Morgen stand Anna früh auf, weil sie den ersten Zug in die Stadt nicht verpassen wollte. \
Draußen war es noch dunkel, und auf den Straßen lag eine dünne Schicht Schnee. Sie trank schnell einen Kaffee, \
zog ihren warmen Mantel an und ging zum Bahnhof. Der Zug war fast leer, nur ein alter Mann las seine Zeitung. \
Anna schaute aus dem Fenster und dachte an das Gespräch, das sie am Nachmittag führen musste. Ihr Chef hatte \
ihr gesagt, dass die Firma eine neue Abteilung eröffnen wolle und dass sie dafür die richtige Person sei. \
Sie war stolz, aber auch ein wenig nervös, denn sie hatte noch nie ein Team geleitet. In der Stadt angekommen, \
ging sie zuerst in eine kleine Bäckerei, kaufte ein Brötchen und setzte sich an einen Tisch am Fenster. \
Dort schrieb sie ihre Gedanken in ein Heft: was sie fragen wollte, was sie sich wünschte und was sie fürchtete. \
Als die Uhr zwölf schlug, wusste sie, dass sie bereit war. Das Gespräch dauerte länger als erwartet, aber am \
Ende gab ihr Chef ihr die Hand und sagte, er freue sich auf die Zusammenarbeit. Auf dem Heimweg schien die Sonne, \
der Schnee schmolz, und Anna lachte zum ersten Mal seit Wochen ohne Grund. Zu Hause rief sie ihre Schwester an \
und erzählte ihr alles, vom dunklen Morgen bis zum hellen Nachmittag."

# name | url (snap= names the shot; tab=dictionary opens that tab; full=1 the full entry)
cases() {
  cat <<EOF
word-hausern|lookup?text=Häusern
word-haus-full|lookup?text=Haus&full=1
word-ru|lookup?text=дом
phrase-translate|lookup?text=Ich mache die Tür zu
phrase-dictionary|lookup?text=Ich mache die Tür zu&tab=dictionary
passage-translate|lookup?text=Der Hund schläft im Garten. Die Kinder spielen draußen, bis es dunkel wird.
passage-dictionary|lookup?text=Der Hund schläft im Garten. Die Kinder spielen draußen, bis es dunkel wird.&tab=dictionary
long-translate|lookup?text=$LONG
long-dictionary|lookup?text=$LONG&tab=dictionary
passage-ru|lookup?text=Вчера мы долго гуляли по парку. Погода была тёплой, и мы не хотели возвращаться домой.
french|lookup?text=Le chat dort sur le canapé depuis ce matin.
unknown-symbols|lookup?text=+++ 42 ### 17 *** 9
unknown-words|lookup?text=Blorvik tanquo meseral fintu dravok.
input|input
selection-failed|unread
EOF
}

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
  rm -f "$dir"/*.done "$dir/card.png"
done

# One page with every shot in the order above, light and dark side by side (shown at 1×).
{
  echo '<!doctype html><meta charset="utf-8"><title>LexiPeek snapshots</title>'
  echo '<style>body{font:13px -apple-system;background:#888;margin:16px}h2{margin:24px 0 6px}'
  echo '.row{display:flex;gap:16px;align-items:flex-start}img{zoom:.5}</style>'
  for n in $(cut -d'|' -f1 "$TMP/cases") settings-general settings-translation settings-packs settings-permissions; do
    for s in "$n" "$n-end"; do
      [ -e "$OUT/light/$s.png" ] || continue
      echo "<h2>$s</h2><div class=row><img src=\"light/$s.png\"><img src=\"dark/$s.png\"></div>"
    done
  done
} >"$OUT/index.html"
echo "$OUT/index.html"
exit $fail
