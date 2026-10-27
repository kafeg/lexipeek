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

# name | url (snap= names the shot; tab=dictionary opens that tab; full=1 the full entry; save=1 ☆ once it is final;
# open=<word> clicks that word in the card; section=<key> opens the first entry's section;
# window?page=…: the LexiPeek window, notes after the cards that fill them; look= looks up there)
cases() {
  cat <<EOF
word-hausern|lookup?text=Häusern
word-haus-full|lookup?text=Haus&full=1
word-entry-one|lookup?text=Haus&entry=1
section-page-2|lookup?text=machen&full=1&section=rel&page=1
gloss-back|lookup?text=Ich mache die Tür zu&tab=dictionary&open=Tür&back=1
word-answer-en|lookup?text=Häusern&answer=en
word-machen-sections|lookup?text=machen&full=1
section-related|lookup?text=machen&full=1&section=rel
section-forms|lookup?text=machen&full=1&section=forms
gloss-open|lookup?text=Ich mache die Tür zu&tab=dictionary&open=Tür
gloss-open-full|lookup?text=Am nächsten Morgen stand Anna früh auf&tab=dictionary&open=aufstehen&full=1
word-ru|lookup?text=дом
phrase-translate|lookup?text=Ich mache die Tür zu
phrase-dictionary|lookup?text=Ich mache die Tür zu&tab=dictionary
passage-translate|lookup?text=Der Hund schläft im Garten. Die Kinder spielen draußen, bis es dunkel wird.
passage-dictionary|lookup?text=Der Hund schläft im Garten. Die Kinder spielen draußen, bis es dunkel wird.&tab=dictionary
long-translate|lookup?text=$LONG
long-dictionary|lookup?text=$LONG&tab=dictionary
long-whole|lookup?text=$LONG&whole=1
passage-english|lookup?text=Der Hund schläft im Garten. Die Kinder spielen draußen, bis es dunkel wird.&method=fast
passage-whole|lookup?text=Der Hund schläft im Garten. Die Kinder spielen draußen, bis es dunkel wird.&whole=1
passage-ru|lookup?text=Вчера мы долго гуляли по парку. Погода была тёплой, и мы не хотели возвращаться домой.
french|lookup?text=Le chat dort sur le canapé depuis ce matin.
unknown-symbols|lookup?text=+++ 42 ### 17 *** 9
unknown-words|lookup?text=Blorvik tanquo meseral fintu dravok.
input|input
selection-failed|unread
word-saved|lookup?text=Häusern&save=1
word-baume-saved|lookup?text=Bäume&tab=dictionary&save=1
article-saved|lookup?text=das Haus
phrase-saved|lookup?text=Ich mache die Tür zu&save=1
passage-saved|lookup?text=Der Hund schläft im Garten. Die Kinder spielen draußen, bis es dunkel wird.&save=1
notes-words|window?page=words
notes-phrases|window?page=phrases
notes-history|window?page=history
window-help|window?page=help
window-empty|window?page=lookup
window-lookup|window?page=lookup&look=Häusern
window-passage|window?page=lookup&look=Der Hund schläft im Garten. Die Kinder spielen draußen, bis es dunkel wird.
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

# The first-run window on an empty data dir: name | step | a file of the data dir copied in before it.
firstrun_cases() {
  cat <<EOF
firstrun-access|access|
firstrun-list|list|
firstrun-list-error|list-error|
firstrun-pair|pair|packs.json
firstrun-download|download|
firstrun-error|error|
firstrun-done|done|installed.json
firstrun-done-later|done-later|
EOF
}

# AI on (Claude; no key, no network: LEXIPEEK_AI_REPLIES answers from files), a third instance with its own data dir:
# name | url | the answers (word, phrase) or "error" (the provider says 401). method=ai: AI translation picked in the card;
# tab=analysis|context: that AI tab clicked.
ai_cases() {
  cat <<EOF
ai-word-translate|lookup?text=Häusern&method=ai&tab=translate|word
ai-word-analysis|lookup?text=Häusern&tab=analysis|word
ai-word-context|lookup?text=Häusern&tab=context|word
ai-phrase-translate|lookup?text=Ich mache die Tür zu&method=ai|phrase
ai-phrase-analysis|lookup?text=Ich mache die Tür zu&tab=analysis|phrase
ai-phrase-context|lookup?text=Ich mache die Tür zu&tab=context|phrase
ai-error|lookup?text=Bäume&tab=analysis|error
EOF
}

# The fake provider's answers (as a model would give them), per kind of text.
ai_fixtures() {
  mkdir -p "$1/word" "$1/phrase"
  cat >"$1/word/quick.json" <<'EOF'
{"base":"Haus","article":"das","plural":"die Häuser","pos":"сущ., ср. р.","forms":"","irregular":false,"form_of":"дательный падеж, мн. ч.","meanings":["дом","здание","семья, род"],"translation":""}
EOF
  cat >"$1/word/analysis.json" <<'EOF'
{"grammar":"**Häusern** — дательный падеж множественного числа от **das Haus**.\nВо множественном числе: **die Häuser**, в дательном добавляется **-n**: *in den Häusern*.","words":["**das Haus** — дом","**das Gebäude** — здание","**das Heim** — домашний очаг"]}
EOF
  cat >"$1/word/context.json" <<'EOF'
{"explanation":"Форма встречается после предлогов с дательным падежом: *in*, *aus*, *von*. Нейтральное слово, любой стиль.","examples":[{"text":"In den alten **Häusern** wohnen viele Familien.","translation":"В старых домах живёт много семей."},{"text":"Aus den **Häusern** kam Musik.","translation":"Из домов доносилась музыка."},{"text":"Zwischen den **Häusern** ist ein kleiner Park.","translation":"Между домами есть маленький парк."}]}
EOF
  cat >"$1/phrase/quick.json" <<'EOF'
{"base":"","article":"","plural":"","pos":"","forms":"","irregular":false,"form_of":"","meanings":[],"translation":"Я закрываю дверь."}
EOF
  cat >"$1/phrase/analysis.json" <<'EOF'
{"grammar":"**mache … zu** — отделяемый глагол **zumachen**: приставка **zu** уходит в конец предложения.\n**die Tür** — винительный падеж (прямое дополнение), артикль *die* не меняется.","words":["**zumachen** — закрывать","**die Tür** — дверь"]}
EOF
  cat >"$1/phrase/context.json" <<'EOF'
{"explanation":"Обычная разговорная фраза; **zumachen** проще и разговорнее, чем *schließen*.","examples":[{"text":"Kannst du bitte das Fenster **zumachen**?","translation":"Можешь, пожалуйста, закрыть окно?"},{"text":"Ich **mache** den Laden um sechs **zu**.","translation":"Я закрываю магазин в шесть."},{"text":"**Mach** die Augen **zu**!","translation":"Закрой глаза!"}]}
EOF
}

# The settings window's shots, written by the app at launch: every section × tab (settings-<section>[-<tab>]); AI's
# and Card → Opening also with AI on (-on, from the AI instance).
settings_shots() {
  echo settings-languages-packs settings-languages-translation settings-languages-translation-empty \
    settings-languages-other-dictionaries settings-card-opening settings-card-opening-on settings-card-article \
    settings-card-appearance settings-ai-connection settings-ai-connection-on settings-ai-modules settings-ai-modules-on \
    settings-general-keys settings-general-system about
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
