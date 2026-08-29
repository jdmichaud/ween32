#!/usr/bin/env bash
# release.sh -- tag a version and start the next one.
#
#   tools/release.sh [patch|minor|major] [--no-test] [--dry-run]
#
# Steps:
#   1. refuse to run with uncommitted changes, or on a detached HEAD (and ask
#      before releasing from a branch that is not master);
#   2. run the same gates the reviewer runs -- tools/verify.sh -- and rehearse
#      the package with tools/package.sh --verify (skip with --no-test);
#   3. read .version from build.zig.zon and tag v<version> on HEAD;
#   4. bump the version and commit it, so the next commit is already the next
#      version's.
#
# Nothing is pushed. The push commands are printed at the end.
#
# **The gates run before the tag rather than after, because CI runs after**: a
# tag pointing at a commit that fails its own checks cannot be withdrawn once
# anybody has fetched it. This is a2a's release.sh's ordering and its reason;
# what is different here is what the gates are and what the version lives in.
#
# **The package rehearsal is not optional decoration.** A release is a URL and
# a hash, and the one mistake this arrangement makes -- a file left out of
# `.paths` -- is invisible to `zig build` and to `verify.sh` alike, because
# both have the whole tree in front of them. Only a consumer built from the
# tarball can see it, which is what package.sh --verify does.

set -euo pipefail
cd "$(dirname "$0")/.."

bump="patch"
run_tests=1
dry_run=0
for arg in "$@"; do
    case "$arg" in
        patch|minor|major) bump="$arg" ;;
        --no-test) run_tests=0 ;;
        --dry-run) dry_run=1 ;;
        *) echo "usage: $0 [patch|minor|major] [--no-test] [--dry-run]" >&2
           exit 2 ;;
    esac
done

read_version() {
    sed -n 's/^[[:space:]]*\.version[[:space:]]*=[[:space:]]*"\([^"]*\)".*/\1/p' \
        build.zig.zon | head -1
}

# 1. Clean tree, sensible branch.
if [ -n "$(git status --porcelain)" ]; then
    echo "error: uncommitted changes -- commit them first" >&2
    exit 1
fi
branch=$(git rev-parse --abbrev-ref HEAD)
if [ "$branch" = "HEAD" ]; then
    echo "error: HEAD is detached -- check out a branch first" >&2
    exit 1
fi
if [ "$branch" != "master" ]; then
    # Asked rather than refused, because releasing from a branch is a real
    # thing to want -- but not *silently*: with no terminal to ask, `read`
    # fails and `set -e` would exit with nothing printed, which is the shape
    # of failure this repository keeps finding the hard way.
    if [ ! -t 0 ]; then
        echo "error: on branch '$branch' rather than master, and there is no" >&2
        echo "       terminal to ask; run it from a shell or check out master" >&2
        exit 1
    fi
    read -r -p "warning: releasing from branch '$branch', continue? [y/N] " a
    [ "$a" = "y" ] || { echo "stopped"; exit 1; }
fi

version=$(read_version)
if [ -z "$version" ]; then
    echo "error: could not read .version from build.zig.zon" >&2
    exit 1
fi
tag="v$version"
if git rev-parse -q --verify "refs/tags/$tag" >/dev/null; then
    echo "error: tag '$tag' already exists" >&2
    exit 1
fi

# 2. The gates.
if [ "$run_tests" = 1 ]; then
    echo "==> verify"
    tools/verify.sh
    echo "==> package"
    tools/package.sh --verify
fi

# 3. Tag, then bump.
IFS=. read -r major minor patch <<< "$version"
case "$bump" in
    major) next="$((major + 1)).0.0" ;;
    minor) next="$major.$((minor + 1)).0" ;;
    patch) next="$major.$minor.$((patch + 1))" ;;
esac

if [ "$dry_run" = 1 ]; then
    echo "==> would tag $tag, then bump to $next and commit"
    exit 0
fi

# Anything failing from here leaves a tag with nothing behind it, which is the
# one state worth cleaning up without being asked: whoever ran this would
# otherwise have to know to delete it before trying again.
tag_created=0
release_done=0
cleanup() {
    if [ "$tag_created" = 1 ] && [ "$release_done" = 0 ]; then
        echo "error: release failed after tagging; removing $tag" >&2
        git tag -d "$tag" >/dev/null 2>&1 || true
    fi
}
trap cleanup EXIT

git tag -a "$tag" -m "ween32 $version"
tag_created=1
echo "==> tagged $tag"

# The same sed that read the version writes it, so the two cannot disagree
# about which `.version` line in build.zig.zon is meant.
sed -i "0,/^\([[:space:]]*\.version[[:space:]]*=[[:space:]]*\)\"[^\"]*\"/s//\1\"$next\"/" \
    build.zig.zon
if [ "$(read_version)" != "$next" ]; then
    echo "error: the version bump did not take" >&2
    exit 1
fi
git add build.zig.zon
git commit -q -m "start $next development"
release_done=1
echo "==> bumped to $next and committed"

cat <<EOF

The tag is local. To publish it -- and the tag is what CI releases on:
  git push origin $branch
  git push origin $tag

CI's release job runs tools/package.sh --verify on the tag and attaches
ween32-$version.tar.gz to the release, so a consumer's build.zig.zon can say

  .ween32 = .{
      .url = "https://github.com/jdmichaud/ween32/releases/download/$tag/ween32-$version.tar.gz",
      .hash = "<the hash that job prints>",
  },

To undo, before pushing:
  git tag -d $tag && git reset --hard HEAD~1
EOF
