#!/bin/sh
# The Linux app (spec M10): the Qt shell into <build>/LexiPeek/ — lexipeek (Qt linked in), lib/ (what desktops may lack),
# lexipeek.png, LICENSE, THIRD_PARTY_NOTICES.txt — and dist/LexiPeek-Linux-x64.tar.gz of that folder. Build it on the
# oldest glibc it should run on (CI: Ubuntu 22.04). Usage: scripts/build-linux.sh [build dir] (default build-linux);
# CMAKE_ARGS adds to the configure.
set -eu
cd "$(dirname "$0")/.."
B=${1:-build-linux}
# shellcheck disable=SC2086
cmake -S . -B "$B" -G Ninja -DCMAKE_BUILD_TYPE=Release -DLEXIPEEK_QT=ON ${CMAKE_ARGS:-}
cmake --build "$B" --target lexipeek-qt
out="$B/LexiPeek"
rm -rf "$out"
mkdir -p "$out/lib" dist
cp "$B/qt/lexipeek" "$out/"
strip "$out/lexipeek"
cp qt/icons/icon_256.png "$out/lexipeek.png"
cp LICENSE THIRD_PARTY_NOTICES.txt "$out/"
# Qt's X11 platform needs xcb's small helper libraries; desktops without a Qt app often lack them (Ubuntu 22.04:
# xcb-cursor; a server without X: xcb-shape).
for l in libxcb-cursor.so.0 libxcb-icccm.so.4 libxcb-image.so.0 libxcb-keysyms.so.1 libxcb-render-util.so.0 libxcb-shape.so.0 libxcb-util.so.1 libxcb-xinput.so.0; do
  p=$(ldd "$out/lexipeek" | awk -v l="$l" '$1 == l { print $3 }')
  [ -n "$p" ] || continue
  cp -L "$p" "$out/lib/"
done
echo "glibc needed: $(objdump -T "$out/lexipeek" | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1)"
readelf -d "$out/lexipeek" | grep -E 'R(UN)?PATH'
ldd "$out/lexipeek" | awk '{ print "  " $1 }' | sort
tar -C "$B" -czf dist/LexiPeek-Linux-x64.tar.gz LexiPeek
ls -l dist/LexiPeek-Linux-x64.tar.gz
