#!/bin/sh
# THIRD_PARTY_NOTICES.txt: the licenses of everything the release zips hold — the engines (submodules), vcpkg's ports of
# each app's build (share/<port>/copyright) and, for Windows, the NuGet packages that ship (their own license files).
# Usage: scripts/gen-notices.sh --mac <vcpkg share dir> --win <build-win dir> --linux <vcpkg share dir> --xcb <doc dir>
#   [-- <name>=<file>...], e.g. --mac build/vcpkg_installed/arm64-osx/share --win build-win (its
#   vcpkg_installed/x64-windows-static/share and packages/, as scripts\build-win.cmd leaves them), --linux
#   build-linux/vcpkg_installed/<triplet>/share (scripts/build-linux.sh: Qt is in that one), --xcb a Debian/Ubuntu
#   /usr/share/doc (the copyright of each xcb library build-linux.sh puts in lib/); <name>=<file> adds or replaces one
#   component's text.
set -eu
cd "$(dirname "$0")/.."
OUT=THIRD_PARTY_NOTICES.txt
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
# Build tools, duplicates, headers only (the GL registries) and what only links the system's library (vulkan-loader,
# Qt's dbus): not in the apps.
SKIP="doc doctest man pkgconfig gperf egl-registry opengl-registry vcpkg-tool-meson dbus vcpkg-cmake vcpkg-cmake-config vcpkg-cmake-get-vars vcpkg-make nlohmann_json unofficial-hash-library unofficial-sqlite3 unofficial-utf8proc vulkan-loader spirv-headers shaderc"
add() { [ -f "$2" ] && [ ! -f "$TMP/$1" ] && cp "$2" "$TMP/$1" || true; }
only() { echo "$2" > "$TMP/$1.in"; }  # the component is in that zip only
share() {
  for d in "$1"/*/; do
    p=$(basename "$d")
    case " $SKIP " in *" $p "*) continue ;; esac
    case "$p" in Qt6*) continue ;; qtbase) [ "$2" = Linux ] || continue ;; esac  # the Qt shell ships on Linux only
    add "$p" "$d/copyright"
    seen="$seen $2:$p"
  done
}
seen=""
builds=""
while [ $# -gt 0 ]; do
  case "$1" in
    --mac) share "$2" macOS; builds="$builds macOS"; shift 2 ;;
    --linux) share "$2" Linux; builds="$builds Linux"; shift 2 ;;
    --xcb)
      for x in libxcb:libxcb1 xcb-util:libxcb-util1 xcb-util-cursor:libxcb-cursor0 xcb-util-image:libxcb-image0 \
        xcb-util-keysyms:libxcb-keysyms1 xcb-util-renderutil:libxcb-render-util0 xcb-util-wm:libxcb-icccm4 \
        libxkbcommon-x11:libxkbcommon-x11-0; do
        [ -f "$2/${x#*:}/copyright" ] || { echo "no $2/${x#*:}/copyright" >&2; exit 2; }
        add "${x%%:*}" "$2/${x#*:}/copyright"; only "${x%%:*}" Linux
      done
      shift 2 ;;
    --win)
      share "$2/vcpkg_installed/x64-windows-static/share" Windows
      add windows-app-sdk "$(ls "$2"/packages/Microsoft.WindowsAppSDK.Base.*/license.txt | head -1)"
      add cppwinrt "$(ls "$2"/packages/Microsoft.Windows.CppWinRT.*/LICENSE | head -1)"
      seen="$seen Windows:windows-app-sdk Windows:cppwinrt"
      builds="$builds Windows"
      shift 2 ;;
    --) shift; for a in "$@"; do rm -f "$TMP/${a%%=*}"; add "${a%%=*}" "${a#*=}"; done; break ;;
    *) echo "usage: $0 --mac <share> --win <build-win dir> --linux <share> --xcb <doc dir> [-- name=file...]" >&2; exit 2 ;;
  esac
done
# A port some builds lack is the others'.
for p in $(ls "$TMP"); do
  in=""
  for b in $builds; do case "$seen " in *" $b:$p "*) in="$in, $b" ;; esac; done
  [ -n "$in" ] && [ "$(echo "$in" | tr -cd , | wc -c)" -lt "$(echo $builds | wc -w)" ] && only "$p" "${in#, }"
done
linux() { case " $builds " in *" Linux "*) return 0 ;; esac; return 1; }
M=engines/bergamot/3rd_party/marian-dev/src/3rd_party
add bergamot-translator engines/bergamot/LICENSE
add marian engines/bergamot/3rd_party/marian-dev/LICENSE.md
add ssplit-cpp engines/bergamot/3rd_party/ssplit-cpp/LICENSE.md
for p in intgemm ruy sentencepiece yaml-cpp spdlog CLI cnpy pathie-cpp phf zstr simd_utils mio faiss; do add "$p" "$M/$p/LICENSE"; done
add fasttext engines/fasttext/LICENSE
add llama.cpp engines/llama/LICENSE
add pcre2 engines/pcre2/COPYING
rm -f "$TMP/pcre2.in"  # vcpkg's (Qt's) PCRE2 on Linux, the engine's in every build
add qwentts.cpp engines/qwentts/LICENSE
add ggml-qwentts engines/qwentts/ggml/LICENSE
add voices core/src/speech/voices/SOURCES.md
add inno-setup win/inno-setup-license.txt
# Marian: ruy and simd_utils on ARM (the Mac), intgemm on x86-64 (Windows, Linux). Read aloud: not on Linux.
only ruy macOS; only simd_utils macOS; only inno-setup Windows
if linux; then only intgemm "Windows, Linux"; only qwentts.cpp "macOS, Windows"; only ggml-qwentts "macOS, Windows"; only voices "macOS, Windows"
else only intgemm Windows; fi
{
  echo "LexiPeek — third-party components"
  echo
  echo "LexiPeek (MIT, see LICENSE) bundles the components below; each keeps its own license, in full after the list."
  echo "LexiPeek-macOS.zip: LexiPeek.app — the core with the engines and its libraries, Read aloud's libqwen.dylib."
  echo "LexiPeek-Windows.zip: the LexiPeek folder — LexiPeek.exe (the same core and engines, statically linked, with"
  echo "OpenBLAS and CLAPACK), Read aloud's qwen.dll, the Windows App SDK's runtime DLLs and resources (self-contained,"
  echo "redistributed under the Microsoft Software License Terms of the Windows App SDK: \"windows-app-sdk\" below) and"
  echo "C++/WinRT's code compiled in (MIT). LexiPeek-Windows-Setup.exe: the same folder (with this file) in an installer"
  echo "made by Inno Setup, whose setup and uninstall program it is (\"inno-setup\" below)."
  echo "LexiPeek-Linux-x64.tar.gz: the LexiPeek folder — lexipeek (the same core and engines, statically linked, with"
  echo "OpenBLAS, CLAPACK, the C++ runtime and Qt 6: qtbase and what it uses, LGPL-3.0 — LexiPeek's source and build scripts"
  echo "are public, so it can be relinked with another Qt; the system's X11, xcb and D-Bus libraries) and lib/ with xcb's"
  echo "helper libraries and xkbcommon's X11 part (libxcb, xcb-util, libxkbcommon-x11: MIT/X11)."
  echo "A component marked [macOS], [Windows], [Linux] or a list of them is only in those builds; the rest in all three."
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
