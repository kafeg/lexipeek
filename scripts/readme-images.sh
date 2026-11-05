#!/bin/sh
# The README's images in docs/img/: the app's own snapshots (invisible, as snapshots.sh), the interface in English,
# German → English, Franz Kafka's "Die Verwandlung" (public domain), notes from a fixture. Light look, 800 px at most.
# Usage: scripts/readme-images.sh [out-dir]   (after `make app`; packs dict-de-en and model-de-en in the default data dir
# or LEXIPEEK_DATA: only the packs are linked, settings, notes and the sync folder are the run's own)
set -eu
cd "$(dirname "$0")/.."
APP=${APP:-$PWD/mac/build.noindex/Build/Products/Release/LexiPeek.app}
OUT=${1:-docs/img}
DATA=${LEXIPEEK_DATA:-$HOME/Library/Application Support/LexiPeek}
[ -x "$APP/Contents/MacOS/LexiPeek" ] || { echo "no $APP: make app" >&2; exit 1; }
for p in dict-de-en model-de-en; do
  grep -q "\"$p\"" "$DATA/installed.json" 2>/dev/null || { echo "no pack $p in $DATA" >&2; exit 1; }
done
mkdir -p "$OUT"
TMP=$(mktemp -d)
# A fake AnkiConnect: Settings → Sync → Anki says it is connected.
python3 -c 'import http.server, json
class H(http.server.BaseHTTPRequestHandler):
    def do_POST(self):
        r = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        b = json.dumps({"result": {"version": 6, "deckNames": ["Default", "Peek::Words"], "modelNames": ["Basic"]}.get(r["action"]), "error": None}).encode()
        self.send_response(200); self.send_header("Content-Length", str(len(b))); self.end_headers(); self.wfile.write(b)
    def log_message(self, *a): pass
http.server.HTTPServer(("127.0.0.1", 18766), H).serve_forever()' &
ANKI_PID=$!
trap 'rm -rf "$TMP"; kill $ANKI_PID 2>/dev/null' EXIT
. scripts/snapshot-cases.sh
mkdir "$TMP/data" "$TMP/shots"
link_packs "$DATA" "$TMP/data"
# Settings of its own, never the person's: English answers and interface, a fixture sync folder, Anki on.
cat >"$TMP/data/settings.json" <<EOF
{"answer": "en", "ui_lang": "en", "first_run": true, "sync": "$TMP/sync",
 "anki": {"on": true, "url": "http://127.0.0.1:18766"}}
EOF
# Another device's notes: an InkPeek reader on the same story.
mkdir -p "$TMP/sync/inkpeek-log/boox-7hq2mzta"
echo '{"device":"boox-7hq2mzta","app":"inkpeek","name":"Boox Go 6","created":1790553600000}' >"$TMP/sync/inkpeek-log/boox-7hq2mzta/device.json"
cat >"$TMP/sync/inkpeek-log/boox-7hq2mzta/2026-10.jsonl" <<'LOG'
{"t":[1793145600000,0],"d":"boox-7hq2mzta","op":"saved.put","text":"Rücken","kind":"word","lang":"en","meaning":"der Rücken — back"}
{"t":[1793145660000,0],"d":"boox-7hq2mzta","op":"saved.put","text":"Bettdecke","kind":"word","lang":"en","meaning":"die Bettdecke — blanket, duvet"}
LOG

KAFKA="Als Gregor Samsa eines Morgens aus unruhigen Träumen erwachte, fand er sich in seinem Bett zu einem ungeheueren Ungeziefer verwandelt."
enc() { python3 -c 'import sys, urllib.parse; print(urllib.parse.quote(sys.argv[1], safe="=&?"))' "$1"; }
wait_for() {  # file, seconds; stops when the app has quit
  i=0
  while [ ! -e "$1" ] && [ $i -lt $(($2 * 10)) ]; do
    kill -0 "$pid" 2>/dev/null || { echo "the app quit:" >&2; cat "$TMP/shots/app.log" >&2; exit 1; }
    sleep 0.1
    i=$((i + 1))
  done
  [ -e "$1" ]
}

LEXIPEEK_SNAPSHOT="$TMP/shots" LEXIPEEK_SNAPSHOT_APPEARANCE=light LEXIPEEK_DATA="$TMP/data" \
  "$APP/Contents/MacOS/LexiPeek" >"$TMP/shots/app.log" 2>&1 &
pid=$!
wait_for "$TMP/shots/settings.done" 60 || { echo "settings timed out" >&2; exit 1; }
fail=0
while IFS='|' read -r name url; do
  echo "lexipeek://$(enc "$url&snap=$name")" >"$TMP/shots/request.tmp" && mv "$TMP/shots/request.tmp" "$TMP/shots/request"
  wait_for "$TMP/shots/$name.done" 90 && echo "$name" || { echo "$name: timed out" >&2; fail=1; }
done <<EOF
card-word|lookup?text=Ungeziefer&full=1&save=1
card-passage|lookup?text=$KAFKA&save=1
card-gloss|lookup?text=$KAFKA&tab=dictionary
notes|window?page=words
EOF
kill "$pid" 2>/dev/null || true
wait "$pid" 2>/dev/null || true

# The shots are 2×: 800 px wide at most; PNGs as small as they go.
for n in card-word card-passage card-gloss notes settings-languages-packs settings-sync-anki; do
  [ -e "$TMP/shots/$n.png" ] || { echo "no $n.png" >&2; fail=1; continue; }
  w=$(sips -g pixelWidth "$TMP/shots/$n.png" | awk '/pixelWidth/ {print $2}')
  sips --resampleWidth $((w < 800 ? w : 800)) "$TMP/shots/$n.png" --out "$OUT/$n.png" >/dev/null
  ! command -v optipng >/dev/null || optipng -quiet -o5 "$OUT/$n.png"
  echo "$OUT/$n.png"
done
exit $fail
