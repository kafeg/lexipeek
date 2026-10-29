#!/bin/bash
# A release, the same way every time: scripts/release.sh [--pre]
#   checks (main, clean, pushed, tests, a CHANGELOG.md section for the version), dates that section if "unreleased",
#   tags v<VERSION>.<commits>, waits for release.yml, checks the published files (checksums, the Mac app's version,
#   the Windows zip); --pre marks the release a pre-release. macOS (BSD sed, shasum).
#   Run again after a failure: a tag already on HEAD is kept and its build is found or started again.
set -euo pipefail
cd "$(dirname "$0")/.."
REPO=kafeg/lexipeek
die() { echo "release: $*" >&2; exit 1; }

[ "$(git branch --show-current)" = main ] || die "not on main"
[ -z "$(git status --porcelain)" ] || die "uncommitted changes"
git fetch -q origin main
[ "$(git rev-parse HEAD)" = "$(git rev-parse origin/main)" ] || die "main is not the same as origin/main"
V=$(sed -n 's/^VERSION := //p' Makefile)
grep -q "^## $V\b" CHANGELOG.md || die "CHANGELOG.md has no section '## $V'"
make test >/dev/null || die "tests failed: make test"

if grep -q "^## $V — unreleased" CHANGELOG.md; then
  sed -i '' "s/^## $V — unreleased/## $V — $(date +%F)/" CHANGELOG.md
  git commit -qam "CHANGELOG: $V released"
  git push -q origin main || die "push failed: git push origin main, then run this again"
fi
TAG="v$V.$(git rev-list --count HEAD)"
if git rev-parse -q --verify "refs/tags/$TAG" >/dev/null; then
  [ "$(git rev-list -n1 "$TAG")" = "$(git rev-parse HEAD)" ] || die "tag $TAG exists on another commit"
  echo "tag $TAG is there"
else
  git tag "$TAG"; echo "tagged $TAG"
fi
git ls-remote -q --exit-code --tags origin "refs/tags/$TAG" >/dev/null || git push -q origin "$TAG"

# The tag's run (by its title, see release.yml); GitHub's webhooks sometimes lose a push: then it is started by hand.
find_run() { gh run list --workflow release.yml --limit 20 --json databaseId,displayTitle -q ".[] | select(.displayTitle == \"Release $TAG\") | .databaseId" | head -1; }
run=""
for _ in $(seq 1 12); do run=$(find_run); [ -n "$run" ] && break; sleep 5; done
if [ -z "$run" ]; then
  gh workflow run release.yml -f tag="$TAG"
  for _ in $(seq 1 12); do sleep 5; run=$(find_run); [ -n "$run" ] && break; done
fi
[ -n "$run" ] || die "no build found for $TAG: gh run list --workflow release.yml"
echo "build: run $run (macOS and Windows, about an hour)"
gh run watch "$run" --exit-status --interval 60 >/dev/null || die "the build failed: gh run view $run --log-failed (fix, then run this again)"

[ "${1:-}" = "--pre" ] && gh release edit "$TAG" --prerelease >/dev/null
dir=$(mktemp -d); trap 'rm -rf "$dir"' EXIT
gh release download "$TAG" -D "$dir"
for f in LexiPeek-macOS.zip LexiPeek-Windows.zip THIRD_PARTY_NOTICES.txt SHA256SUMS.txt; do [ -s "$dir/$f" ] || die "$f missing"; done
(cd "$dir" && shasum -a 256 -c SHA256SUMS.txt >/dev/null) || die "checksum mismatch"
ditto -x -k "$dir/LexiPeek-macOS.zip" "$dir/mac"
got=$(defaults read "$dir/mac/LexiPeek.app/Contents/Info.plist" CFBundleShortVersionString)
[ "v$got" = "$TAG" ] || die "the Mac app says $got, not ${TAG#v}"
codesign --verify --deep --strict "$dir/mac/LexiPeek.app" || die "the Mac app's signature does not verify"
# The Windows zip: the LexiPeek folder with the program and the Windows App SDK's runtime beside it.
names=$(unzip -Z1 "$dir/LexiPeek-Windows.zip")
for f in LexiPeek.exe Microsoft.WindowsAppRuntime.dll Microsoft.UI.Xaml.dll resources.pri; do
  grep -qx "LexiPeek/$f" <<<"$names" || die "LexiPeek-Windows.zip has no LexiPeek/$f"
done
[ "$(unzip -p "$dir/LexiPeek-Windows.zip" LexiPeek/LexiPeek.exe | head -c 2)" = "MZ" ] || die "LexiPeek.exe is not a Windows program"
[ "${1:-}" = "--pre" ] || [ "$(gh api "repos/$REPO/releases/latest" -q .tag_name)" = "$TAG" ] || die "$TAG is not the latest release"
echo "released $TAG: https://github.com/$REPO/releases/tag/$TAG"
