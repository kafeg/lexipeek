#!/bin/sh
# How the Mac app reads a selection in a web view: each tests/selection/<name>.html is selected in an off-screen WKWebView
# and read by the hotkey's own AX code (LEXIPEEK_SELECTION_TEST, SelectionTest.swift), compared with <name>.expected.
# Usage: scripts/selection-test.sh [out-dir]   (after `make app`; needs the Accessibility grant of the signed app;
# invisible as the snapshots: nothing on the screen, never activated). Exit 1 on a failed case.
set -eu
cd "$(dirname "$0")/.."
APP=${APP:-$PWD/mac/build/Build/Products/Release/LexiPeek.app}
OUT=${1:-build/selection-test}
[ -x "$APP/Contents/MacOS/LexiPeek" ] || { echo "no $APP: make app" >&2; exit 1; }
rm -rf "$OUT" && mkdir -p "$OUT/cases" "$OUT/data"
OUT=$(cd "$OUT" && pwd)
cp tests/selection/*.html "$OUT/cases/"

LEXIPEEK_SELECTION_TEST="$OUT" LEXIPEEK_DATA="$OUT/data" "$APP/Contents/MacOS/LexiPeek" >"$OUT/app.log" 2>&1 &
pid=$!
i=0
while [ ! -e "$OUT/done" ] && [ $i -lt 1200 ] && kill -0 "$pid" 2>/dev/null; do sleep 0.1; i=$((i + 1)); done
if [ ! -e "$OUT/done" ]; then
  pkill -P "$pid" 2>/dev/null || true
  kill "$pid" 2>/dev/null || true
  echo "the app did not finish, see $OUT/app.log" >&2
  exit 1
fi
wait "$pid" 2>/dev/null || true

python3 - "$OUT" tests/selection <<'EOF'
import difflib, pathlib, sys
out, src = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
show = lambda s: s.replace("\n", "⏎\n").replace("­", "<shy>").replace(" ", "<nbsp>").replace("\t", "<tab>")
failed, rows, diffs = 0, [], []
for html in sorted(src.glob("*.html")):
    n = html.stem
    got = (out / f"{n}.txt").read_text() if (out / f"{n}.txt").exists() else ""
    way = (out / f"{n}.way").read_text() if (out / f"{n}.way").exists() else "—"
    exp = src / f"{n}.expected"
    want = exp.read_text().strip() if exp.exists() else None
    ok = want is not None and got.strip() == want  # spaces at the ends do not count: the core trims them
    failed += not ok
    rows.append((n, way, "PASS" if ok else "FAIL" if want is not None else "FAIL (no .expected)"))
    if not ok:
        d = difflib.unified_diff(show(want or "").splitlines(), show(got).splitlines(), "expected", "read", lineterm="", n=1)
        diffs.append(f"--- {n} ---\n" + "\n".join(list(d)[:40]))
w = max(len(r[0]) for r in rows)
for r in rows: print(f"{r[0]:<{w}}  {r[1]:<18}  {r[2]}")
if any(r[1] == "no-access" for r in rows): print("\nno Accessibility for this build: grant LexiPeek in System Settings → Privacy & Security → Accessibility")
for d in diffs: print("\n" + d)
print(f"\n{len(rows) - failed} passed, {failed} failed ({out})")
sys.exit(1 if failed else 0)
EOF
