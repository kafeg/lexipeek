#!/bin/sh
# THIRD_PARTY_NOTICES.txt: the licenses of everything the release zips hold — the engines (submodules), vcpkg's ports of
# each app's build (share/<port>/copyright) and, for Windows, the NuGet packages that ship (their own license files).
# Usage: scripts/gen-notices.sh --mac <vcpkg share dir> --win <build-win dir> [-- <name>=<file>...]
#   e.g. --mac build/vcpkg_installed/arm64-osx/share --win build-win (its vcpkg_installed/x64-windows-static/share and
#   packages/, as scripts\build-win.cmd leaves them); <name>=<file> adds or replaces one component's text.
set -eu
cd "$(dirname "$0")/.."
OUT=THIRD_PARTY_NOTICES.txt
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
# Build tools, duplicates and what only links the system's DLL (vulkan-loader): not in the apps.
SKIP="doc doctest man pkgconfig vcpkg-cmake vcpkg-cmake-config vcpkg-cmake-get-vars vcpkg-make nlohmann_json unofficial-hash-library unofficial-sqlite3 unofficial-utf8proc vulkan-loader spirv-headers shaderc"
add() { [ -f "$2" ] && [ ! -f "$TMP/$1" ] && cp "$2" "$TMP/$1" || true; }
only() { echo "$2" > "$TMP/$1.in"; }  # the component is in that zip only
share() {
  for d in "$1"/*/; do
    p=$(basename "$d")
    case " $SKIP " in *" $p "*) continue ;; esac
    case "$p" in Qt6* | qtbase) continue ;; esac  # the Qt shell is not in a release
    add "$p" "$d/copyright"
    seen="$seen $2:$p"
  done
}
seen=""
while [ $# -gt 0 ]; do
  case "$1" in
    --mac) share "$2" macOS; shift 2 ;;
    --win)
      share "$2/vcpkg_installed/x64-windows-static/share" Windows
      add windows-app-sdk "$(ls "$2"/packages/Microsoft.WindowsAppSDK.Base.*/license.txt | head -1)"
      add cppwinrt "$(ls "$2"/packages/Microsoft.Windows.CppWinRT.*/LICENSE | head -1)"
      seen="$seen Windows:windows-app-sdk Windows:cppwinrt"
      shift 2 ;;
    --) shift; for a in "$@"; do rm -f "$TMP/${a%%=*}"; add "${a%%=*}" "${a#*=}"; done; break ;;
    *) echo "usage: $0 --mac <share> --win <build-win dir> [-- name=file...]" >&2; exit 2 ;;
  esac
done
# A port only one build has is that zip's.
for p in $(ls "$TMP"); do
  case "$seen " in *" macOS:$p "*) m=1 ;; *) m=0 ;; esac
  case "$seen " in *" Windows:$p "*) w=1 ;; *) w=0 ;; esac
  if [ $m = 1 ] && [ $w = 0 ]; then only "$p" macOS; elif [ $w = 1 ] && [ $m = 0 ]; then only "$p" Windows; fi
done
M=engines/bergamot/3rd_party/marian-dev/src/3rd_party
add bergamot-translator engines/bergamot/LICENSE
add marian engines/bergamot/3rd_party/marian-dev/LICENSE.md
add ssplit-cpp engines/bergamot/3rd_party/ssplit-cpp/LICENSE.md
for p in intgemm ruy sentencepiece yaml-cpp spdlog CLI cnpy pathie-cpp phf zstr simd_utils mio faiss; do add "$p" "$M/$p/LICENSE"; done
add fasttext engines/fasttext/LICENSE
add llama.cpp engines/llama/LICENSE
add pcre2 engines/pcre2/COPYING
add qwentts.cpp engines/qwentts/LICENSE
add ggml-qwentts engines/qwentts/ggml/LICENSE
add voices core/src/speech/voices/SOURCES.md
# Marian: ruy and simd_utils on ARM (the Mac), intgemm on x86-64 (Windows).
only ruy macOS; only simd_utils macOS; only intgemm Windows
{
  echo "LexiPeek — third-party components"
  echo
  echo "LexiPeek (MIT, see LICENSE) bundles the components below; each keeps its own license, in full after the list."
  echo "LexiPeek-macOS.zip: LexiPeek.app — the core with the engines and its libraries, Read aloud's libqwen.dylib."
  echo "LexiPeek-Windows.zip: the LexiPeek folder — LexiPeek.exe (the same core and engines, statically linked, with"
  echo "OpenBLAS and CLAPACK), Read aloud's qwen.dll, the Windows App SDK's runtime DLLs and resources (self-contained,"
  echo "redistributed under the Microsoft Software License Terms of the Windows App SDK: \"windows-app-sdk\" below) and"
  echo "C++/WinRT's code compiled in (MIT). A component marked [macOS] or [Windows] is in that zip only; the rest in both."
  echo "The language packs (dictionaries, translation models) are downloaded separately; their licenses are in the app's"
  echo "About → Licenses. The sentence breaking table is generated from Unicode's data (Unicode License v3). Read aloud's"
  echo "voice model (Qwen3-TTS Base 1.7B, Apache-2.0) is downloaded separately too; the voices in the app (\"voices\") are"
  echo "latents of clips under their sources' licences."
  echo
  echo "Components:"
  names=$(ls "$TMP" | grep -v '\.in$' | sort -f)
  for f in $names; do echo "  - $f$([ -f "$TMP/$f.in" ] && echo " [$(cat "$TMP/$f.in")]")"; done
  for f in $names; do
    echo
    echo "================================================================================"
    echo "$f"
    echo "================================================================================"
    cat "$TMP/$f"
  done
} > "$OUT"
echo "$OUT: $(ls "$TMP" | grep -vc '\.in$') components"
