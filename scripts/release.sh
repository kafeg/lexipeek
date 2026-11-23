#!/bin/bash
# A release, the same way every time: scripts/release.sh [--pre] [--no-linux]
#   checks (main, clean, pushed, tests, a CHANGELOG.md section for the version), dates that section if "unreleased",
#   tags v<VERSION>.<commits>, waits for release.yml, checks the published files (checksums, the Mac app's version,
#   the Windows installer and zip, the Linux tar.gz); --pre marks the release a pre-release. --no-linux releases
#   without the Linux tar.gz, for when its build cannot run at all (release.yml lets that one job fail): say it on
#   purpose, or a release missing it is refused. macOS (BSD sed, shasum).
#   Run again after a failure: a tag already on HEAD is kept and its build is found or started again.
set -euo pipefail
cd "$(dirname "$0")/.."
REPO=kafeg/lexipeek
die() { echo "release: $*" >&2; exit 1; }

PRE=""; LINUX=1
for a in "$@"; do
  case $a in --pre) PRE=1;; --no-linux) LINUX="";; *) die "unknown option $a";; esac
done

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
echo "build: run $run (macOS, Windows and Linux, about an hour)"
# Without Linux its job fails and the run with it, while publish went through: the checks below are then the gate. The
# watch also ends when the network drops, which is no answer about the build: ask the run itself what became of it.
if ! gh run watch "$run" --exit-status --interval 60 >/dev/null; then
  said=$(gh run view "$run" --json status,jobs -q '.status + "\t" + ([.jobs[] | select(.conclusion != "success" and .conclusion != "skipped") | .name] | join(", "))') ||
    die "the build could not be read (the network?): run this again"
  [ "${said%%$'\t'*}" = completed ] || die "the build is still running: run this again"
  bad=${said#*$'\t'}
  [ -z "$LINUX" ] && [ "$bad" = "linux / linux-app" ] ||
    die "the build failed ($bad): gh run view $run --log-failed (fix, then run this again)"
  echo "the Linux job failed; on to the checks of what was published (--no-linux)"
fi

[ -n "$PRE" ] && gh release edit "$TAG" --prerelease >/dev/null
dir=$(mktemp -d); trap 'rm -rf "$dir"' EXIT
gh release download "$TAG" -D "$dir"
files="LexiPeek-macOS.zip LexiPeek-Windows-Setup.exe LexiPeek-Windows.zip THIRD_PARTY_NOTICES.txt SHA256SUMS.txt"
[ -n "$LINUX" ] && files="$files LexiPeek-Linux-x64.tar.gz"
for f in $files; do [ -s "$dir/$f" ] || die "$f missing"; done
(cd "$dir" && shasum -a 256 -c SHA256SUMS.txt >/dev/null) || die "checksum mismatch"
ditto -x -k "$dir/LexiPeek-macOS.zip" "$dir/mac"
got=$(defaults read "$dir/mac/LexiPeek.app/Contents/Info.plist" CFBundleShortVersionString)
[ "v$got" = "$TAG" ] || die "the Mac app says $got, not ${TAG#v}"
codesign --verify --deep --strict "$dir/mac/LexiPeek.app" || die "the Mac app's signature does not verify"
# The Windows zip: the LexiPeek folder with the program, Read aloud's qwen.dll and the Windows App SDK's runtime beside it.
names=$(unzip -Z1 "$dir/LexiPeek-Windows.zip")
for f in LexiPeek.exe LexiPeek.pri qwen.dll Microsoft.WindowsAppRuntime.dll Microsoft.UI.Xaml.dll THIRD_PARTY_NOTICES.txt; do
  grep -qix "LexiPeek/$f" <<<"$names" || die "LexiPeek-Windows.zip has no LexiPeek/$f"
done
[ "$(unzip -p "$dir/LexiPeek-Windows.zip" LexiPeek/LexiPeek.exe | head -c 2)" = "MZ" ] || die "LexiPeek.exe is not a Windows program"
# The installer: a Windows program whose version resource (UTF-16, padded with spaces) names this version.
[ "$(head -c 2 "$dir/LexiPeek-Windows-Setup.exe")" = "MZ" ] || die "LexiPeek-Windows-Setup.exe is not a Windows program"
V="${TAG#v}" perl -0777 -ne 'my ($k, $v) = map { join "", map { "$_\0" } split // } "ProductVersion", $ENV{V};
  exit(/\Q$k\E\0\0(?:\0\0)?\Q$v\E[ \0]\0/ ? 0 : 1)' "$dir/LexiPeek-Windows-Setup.exe" ||
  die "LexiPeek-Windows-Setup.exe is not ${TAG#v}"
# The Linux tar.gz: the LexiPeek folder with the program (an x86-64 ELF; its version checked by linux.yml) and lib/.
if [ -n "$LINUX" ]; then
  names=$(tar -tzf "$dir/LexiPeek-Linux-x64.tar.gz")
  for f in lexipeek lib/libxcb-cursor.so.0 lib/libxcb-util.so.1 THIRD_PARTY_NOTICES.txt; do
    grep -qx "LexiPeek/$f" <<<"$names" || die "LexiPeek-Linux-x64.tar.gz has no LexiPeek/$f"
  done
  tar -xzf "$dir/LexiPeek-Linux-x64.tar.gz" -C "$dir" LexiPeek/lexipeek
  file -b "$dir/LexiPeek/lexipeek" | grep -q 'ELF 64-bit.*x86-64' || die "lexipeek is not an x86-64 Linux program"
else
  echo "released without the Linux tar.gz (--no-linux)"
fi
[ -n "$PRE" ] || [ "$(gh api "repos/$REPO/releases/latest" -q .tag_name)" = "$TAG" ] || die "$TAG is not the latest release"
echo "released $TAG: https://github.com/$REPO/releases/tag/$TAG"
