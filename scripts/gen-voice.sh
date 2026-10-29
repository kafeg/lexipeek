#!/bin/sh
# One of the app's voices (spec M9): a clip of 5–12 s (WAV; 24 kHz mono best) and its exact text → its speaker embedding
# and codec codes (qwentts.cpp's qt_extract_voice_ref, as qwen-codec --talker) in core/src/speech/voices/<id>.{spk,rvq,txt}.
# Usage: scripts/gen-voice.sh <data dir with tts/> <id: de-female> <clip.wav> "<what it says>"   (after make build;
# the model in <data>/tts: lexipeek-cli --data <data> tts download). Its source and licence: voices/SOURCES.md.
set -eu
cd "$(dirname "$0")/.."
[ $# -eq 4 ] || { sed -n 2,5p "$0" >&2; exit 2; }
data=$1 id=$2 wav=$3 said=$4
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
mkdir "$tmp/tts"
for f in "$data"/tts/*.gguf; do ln -s "$(cd "$(dirname "$f")" && pwd)/${f##*/}" "$tmp/tts/"; done
build/cli/lexipeek-cli --data "$tmp" voices add "$wav" --lang "${id%%-*}" "$said" >/dev/null
out=core/src/speech/voices
for ext in spk rvq; do cp "$tmp"/voices/*.$ext "$out/$id.$ext"; done
printf '%s\n' "$said" >"$out/$id.txt"
echo "$out/$id: $(wc -c <"$out/$id.spk") + $(wc -c <"$out/$id.rvq") bytes"
