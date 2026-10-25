#!/bin/sh
# Downloads pack components by id from InkPeek's `packs` release into a data dir, checks sha256, unpacks (M0: by hand; M1: the pack manager).
# Usage: scripts/fetch-models.sh <data-dir> <id>...   e.g. langid model-de-en model-en-ru opus-deu_eng_fra_por_spa-sla llm-hy-mt2-1.8b-q4
set -eu
BASE=https://github.com/kafeg/inkpeek/releases/download/packs
dir=$1; shift
mkdir -p "$dir"
curl -fsSL -o "$dir/packs.json" "$BASE/packs.json"
sha() { if command -v sha256sum >/dev/null; then sha256sum "$1" | cut -d' ' -f1; else shasum -a 256 "$1" | cut -d' ' -f1; fi; }
for id in "$@"; do
  meta=$(python3 -I -c "import json,sys; c=[c for c in json.load(open(sys.argv[1]))['components'] if c['id']==sys.argv[2]][0]; print(c['kind'], c.get('from') or '-', c.get('to') or '-', c['file']['name'], c['file']['sha256'])" "$dir/packs.json" "$id")
  set -- $meta; kind=$1
  name=$4; want=$5
  case $kind in model) dest="$dir/models/$2$3" ;; langid) dest="$dir/langid.ftz" ;; llm) dest="$dir/llm/$id.gguf" ;; *) dest="$dir/$kind/$id" ;; esac
  if [ -e "$dest" ]; then echo "$id: there"; continue; fi
  echo "$id: $name"
  curl -fL --retry 3 -C - -o "$dir/$name.part" "$BASE/$name"
  [ "$(sha "$dir/$name.part")" = "$want" ] || { echo "$id: sha256 mismatch" >&2; exit 1; }
  case $kind in
    langid) mv "$dir/$name.part" "$dest" ;;
    llm) mkdir -p "$dir/llm"; mv "$dir/$name.part" "$dest" ;;
    *) mkdir -p "$dest.tmp"; rm -rf "$dest.tmp"/*; zstd -dc "$dir/$name.part" | tar -x -C "$dest.tmp"
       find "$dest.tmp" -type f -mindepth 2 -exec mv {} "$dest.tmp/" \; ; find "$dest.tmp" -mindepth 1 -type d -empty -delete
       rm "$dir/$name.part"; mv "$dest.tmp" "$dest" ;;
  esac
done
