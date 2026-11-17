#!/bin/sh
# Make a release: scripts/release.sh X.Y.Z [--push]
#
# Checks that the working tree is clean and CHANGELOG.md has a section for
# the version, writes VERSION, rebuilds, runs the unit tests, rebuilds the
# user's guide PDF (when pandoc and xelatex are installed), commits
# "release: vX.Y.Z" and creates the annotated tag vX.Y.Z. With --push the
# commit and the tag are pushed to origin.
set -eu

usage() { echo "usage: $0 X.Y.Z [--push]" >&2; exit 2; }

[ $# -ge 1 ] && [ $# -le 2 ] || usage
VER=$1
PUSH=no
if [ $# -eq 2 ]; then [ "$2" = "--push" ] || usage; PUSH=yes; fi

echo "$VER" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+$' || {
    echo "version must be MAJOR.MINOR.PATCH, got '$VER'" >&2; exit 1; }

cd "$(dirname "$0")/.."

[ -z "$(git status --porcelain)" ] || {
    echo "working tree is not clean; commit or stash first" >&2; exit 1; }
git rev-parse -q --verify "refs/tags/v$VER" >/dev/null && {
    echo "tag v$VER already exists" >&2; exit 1; }
grep -q "^## \[$VER\]" CHANGELOG.md || {
    echo "CHANGELOG.md has no '## [$VER]' section" >&2; exit 1; }

OLD=$(cat VERSION)
echo "$VER" > VERSION

make clean >/dev/null
make all
make test
make check-version

if command -v pandoc >/dev/null 2>&1 && command -v xelatex >/dev/null 2>&1; then
    make -C docs clean
    make -C docs
fi

git add VERSION docs/GUIDE.pdf CHANGELOG.md
git commit -m "release: v$VER" --allow-empty
git tag -a "v$VER" -m "pshell $VER"
echo "released $OLD -> $VER (tag v$VER)"

if [ "$PUSH" = yes ]; then
    git push origin HEAD "v$VER"
else
    echo "now run: git push origin HEAD v$VER"
fi
