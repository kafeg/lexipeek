#!/bin/sh
# THIRD_PARTY_NOTICES.txt: the licenses of everything the apps bundle — the engines (submodules) and vcpkg's ports of
# the given builds (their share/<port>/copyright). Usage: scripts/gen-notices.sh <vcpkg share dir>... [-- <name>=<file>...]
# (e.g. build/vcpkg_installed/arm64-osx/share build-qt/vcpkg_installed/arm64-osx/share -- openblas=<copyright> clapack=<copyright>)
set -eu
cd "$(dirname "$0")/.."
OUT=THIRD_PARTY_NOTICES.txt
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
# Build tools and duplicates, not in the apps.
SKIP="doc doctest man pkgconfig vcpkg-cmake vcpkg-cmake-config vcpkg-cmake-get-vars vcpkg-make nlohmann_json unofficial-hash-library unofficial-sqlite3 unofficial-utf8proc"
add() { [ -f "$2" ] && [ ! -f "$TMP/$1" ] && cp "$2" "$TMP/$1" || true; }
extra=0
for a in "$@"; do
  if [ "$a" = "--" ]; then extra=1; continue; fi
  if [ $extra = 1 ]; then add "${a%%=*}" "${a#*=}"; continue; fi
  for d in "$a"/*/; do
    p=$(basename "$d")
    case " $SKIP " in *" $p "*) continue ;; esac
    case "$p" in Qt6*) continue ;; esac
    add "$p" "$d/copyright"
  done
done
M=engines/bergamot/3rd_party/marian-dev/src/3rd_party
add bergamot-translator engines/bergamot/LICENSE
add marian engines/bergamot/3rd_party/marian-dev/LICENSE.md
add ssplit-cpp engines/bergamot/3rd_party/ssplit-cpp/LICENSE.md
for p in intgemm ruy sentencepiece yaml-cpp spdlog CLI cnpy pathie-cpp phf zstr simd_utils mio faiss; do add "$p" "$M/$p/LICENSE"; done
add fasttext engines/fasttext/LICENSE
add llama.cpp engines/llama/LICENSE
add pcre2 engines/pcre2/COPYING
{
  echo "LexiPeek — third-party components"
  echo
  echo "LexiPeek (MIT, see LICENSE) bundles the components below; each keeps its own license, in full after the list."
  echo "The language packs (dictionaries, translation models) are downloaded separately; their licenses are in the app's"
  echo "About → Licenses. The sentence breaking table is generated from Unicode's data (Unicode License v3)."
  echo
  echo "Qt (qtbase, the Windows app) is used under the GNU Lesser General Public License v3 and statically linked. Its"
  echo "source: https://download.qt.io/official_releases/qt/ (the version vcpkg.json's baseline pins, with vcpkg's port"
  echo "patches: https://github.com/microsoft/vcpkg/tree/master/ports/qtbase). To relink LexiPeek with a modified Qt,"
  echo "build it from this repository's source at the release's tag (README → Build) with your Qt."
  echo
  echo "Components:"
  for f in $(ls "$TMP" | sort -f); do echo "  - $f"; done
  for f in $(ls "$TMP" | sort -f); do
    echo
    echo "================================================================================"
    echo "$f"
    echo "================================================================================"
    cat "$TMP/$f"
  done
} > "$OUT"
echo "$OUT: $(ls "$TMP" | wc -l | tr -d ' ') components"
