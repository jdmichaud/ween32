#!/usr/bin/env bash
# package.sh -- make the source package, and prove a consumer can build it.
#
#   tools/package.sh                 # write the tarball and print its hash
#   tools/package.sh --verify        # and build a throwaway consumer from it
#   tools/package.sh --out DIR       # somewhere other than build/package
#
# What this is for: `notepad/build.zig.zon` names ween32 by `.url` and
# `.hash`, so a tree anywhere can build it without a ween32 beside it. The
# tarball this writes is what that URL serves and the hash it prints is what
# goes in the `.hash` field.
#
# **The tarball is not the thing worth testing.** A package that leaves a
# file out of `.paths` fetches happily and hashes cleanly, and then fails in
# the *consumer* -- where this library's own build cannot see it, because the
# in-tree build has every file whether it is listed or not. Leaving `fonts`
# out was tried: `zig build` here stays green and the consumer answers
# `compile lib ween32 1 errors`. So --verify is the point of this script and
# the tarball falls out of it.
#
# Two things measured while writing it, because both retire a worry:
#
#   - **`zig fetch` hashes the unpacked tree, not the tarball's bytes.** Two
#     tarballs of the same files -- 1,315,509 bytes and 1,333,473, different
#     mtimes, different order, different md5 -- hash identically. So this
#     script does not have to produce reproducible bytes, and a timestamp in
#     the archive is harmless. What must not move is the *file set*: the
#     whole repository hashes differently from the `.paths` set, so whatever
#     is served has to be what was hashed.
#   - **A `.path` dependency cannot be a tarball** -- zig wants a directory
#     and says `NotDir` -- so a package is consumed by URL or not at all. A
#     `file://` URL works, which is what lets this run with no server.
#
# Every step prints a named line. A missing line means that step did not run,
# not that it passed.

set -euo pipefail
cd "$(dirname "$0")/.."
repo=$(pwd)

verify=0
out="$repo/build/package"
keep=0
# A `while` with a real `shift` rather than `for arg in "$@"`: that form
# expands the list once, so `shift` moves the arguments without moving the
# iteration and `--out DIR` reads its own directory as the next option. The
# header documented the space form and the code only did `--out=`, which is
# the sort of thing nothing catches because the person who writes the usage
# line is the person who never runs it.
while [ $# -gt 0 ]; do
    case "$1" in
        --verify) verify=1 ;;
        --keep) keep=1 ;;
        --out) shift; out="${1:-}"
               [ -n "$out" ] || { echo "error: --out wants a directory" >&2
                                  exit 2; } ;;
        --out=*) out="${1#--out=}" ;;
        *) echo "usage: $0 [--verify] [--out DIR] [--keep]" >&2; exit 2 ;;
    esac
    shift
done

# The version and the file list come out of build.zig.zon, so this script and
# the manifest cannot disagree about what ween32 is or what is in it.
version=$(sed -n 's/^[[:space:]]*\.version[[:space:]]*=[[:space:]]*"\([^"]*\)".*/\1/p' \
          build.zig.zon | head -1)
paths=$(awk '/^[[:space:]]*\.paths[[:space:]]*=/ { in_p = 1; next }
             in_p && /}/ { exit }
             in_p { if (match($0, /"[^"]+"/)) print substr($0, RSTART + 1, RLENGTH - 2) }' \
        build.zig.zon)
[ -n "$version" ] || { echo "error: no .version in build.zig.zon" >&2; exit 1; }
[ -n "$paths" ] || { echo "error: no .paths in build.zig.zon" >&2; exit 1; }

echo "== package =="
sha=$(git rev-parse --short HEAD 2>/dev/null || echo "unknown")
if [ -n "$(git status --porcelain 2>/dev/null)" ]; then
    echo "  at $sha, and the tree has uncommitted changes -- what is packaged"
    echo "     below is HEAD, not what you are looking at"
else
    echo "  at $sha, tree clean"
fi
echo "  version $version"

name="ween32-$version"
mkdir -p "$out"
missing=0
for p in $paths; do
    if ! git cat-file -e "HEAD:$p" 2>/dev/null; then
        echo "  MISSING from the commit but named in .paths: $p"
        missing=1
    fi
done
[ "$missing" = 0 ] || { echo "error: .paths names something HEAD does not have" >&2
                        exit 1; }

tarball="$out/$name.tar.gz"
# **From the commit, not from the directory.** `cp -r src` in a tree that has
# been built copies `src/*.o` in with it, and then the hash of a package is a
# fact about whether somebody has run `make` -- measured the hard way: 78
# files and one hash here against 51 files and a different hash in a clean
# worktree at the same sha. A hash that moves with the state of a build
# directory is worse than no hash, because it is reproducible for exactly one
# person. `git archive` takes tracked files at HEAD and nothing else.
git archive --format=tar --prefix="$name/" HEAD -- $paths | gzip -n > "$tarball"
files=$(tar tzf "$tarball" | grep -vc '/$' || true)
echo "  files $files, $(wc -c < "$tarball") bytes"
echo "  tarball $tarball"

# zig fetch is what a consumer runs, so it is what says the hash -- computing
# one here by any other means would be this script agreeing with itself.
cache="$out/fetch-cache"
rm -rf "$cache"
hash=$(zig fetch --global-cache-dir "$cache" "$tarball" 2>/dev/null | tail -1)
[ -n "$hash" ] || { echo "error: zig fetch printed no hash" >&2; exit 1; }
echo "  hash $hash"

if [ "$verify" = 0 ]; then
    echo
    echo "A missing line means that step did not run, not that it passed."
    exit 0
fi

# ---- the consumer -----------------------------------------------------------
#
# Written here rather than pointed at notepad, so that this fails for one
# reason only: the package. It uses a header as well as the library, because
# the failure this arrangement is most likely to have is a package that
# carries the sources and not the headers -- the same shape as
# `installHeadersDirectory` staging nothing over sshfs, which is why
# `ween32.addHeaders` exists.

echo
echo "== verify =="
work="$out/consumer"
rm -rf "$work"
mkdir -p "$work"

cat > "$work/main.c" <<'EOF'
/* A consumer of the package: it includes ween32's own header and calls into
 * the library, so a package missing either fails here. */
#include <ween32.h>

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    HWND w;
    (void)prev;
    (void)cmd;
    (void)show;
    w = CreateWindowExA(0, "STATIC", "packaged", WS_POPUP, 0, 0, 40, 20, NULL,
                        NULL, inst, NULL);
    return w ? 0 : 1;
}
EOF

cat > "$work/build.zig" <<'EOF'
const std = @import("std");
const ween32 = @import("ween32");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});
    const exe = b.addExecutable(.{
        .name = "consumer",
        .root_module = b.createModule(.{
            .target = target,
            .optimize = optimize,
            .link_libc = true,
        }),
    });
    exe.root_module.addCSourceFile(.{
        .file = b.path("main.c"),
        .flags = &.{"-std=c99"},
    });
    const w = b.dependency("ween32", .{ .target = target, .optimize = optimize });
    exe.root_module.linkLibrary(w.artifact("ween32"));
    ween32.addHeaders(w, exe);
    b.installArtifact(exe);
}
EOF

cat > "$work/build.zig.zon" <<EOF
.{
    .name = .consumer,
    .version = "0.0.0",
    .fingerprint = 0x705b3727628aaf97,
    .minimum_zig_version = "0.17.0-dev.1158",
    .dependencies = .{
        .ween32 = .{
            .url = "file://$tarball",
            .hash = "$hash",
        },
    },
    .paths = .{ "build.zig", "build.zig.zon", "main.c" },
}
EOF

if ( cd "$work" && zig build > "$out/consumer-native.log" 2>&1 ); then
    echo "  ok      a consumer fetches the package and builds against the host"
else
    echo "  FAILED  a consumer fetches the package and builds against the host"
    tail -8 "$out/consumer-native.log"
    exit 1
fi

# The other half of the dual-compile contract, out of the *packaged* headers
# rather than the repository's: on Windows a consumer links the real win32 and
# ween32.h defers to it, so what has to survive packaging is the include tree.
#
# Unpacked here rather than read out of zig's cache: this zig keeps a fetched
# package as `p/<hash>.tar.gz` and an older one kept it unpacked, so reaching
# into the cache would be this script depending on a layout that has already
# changed once.
pkg="$out/unpacked/$name"
rm -rf "$out/unpacked"
mkdir -p "$out/unpacked"
tar xzf "$tarball" -C "$out/unpacked"
if [ ! -d "$pkg/include" ]; then
    echo "  FAILED  the package has no include directory in it"
    exit 1
fi
# Only `include`, the way the four gates and CI do it: `include/win32` holds
# the shims ween32.h reaches for when it is *standing in* for win32, and
# putting it in the path here would shadow the real headers -- which shows up
# as mingw's own rpcasync.h not knowing what a ULONG is.
if zig cc -target x86_64-windows-gnu -std=c99 \
        -I"$pkg/include" \
        "$work/main.c" -o "$work/consumer.exe" \
        -luser32 -lgdi32 -lcomctl32 > "$out/consumer-win32.log" 2>&1; then
    echo "  ok      and the same source compiles against real win32 out of"
    echo "          the packaged headers"
else
    echo "  FAILED  the same source against real win32 out of the packaged headers"
    tail -8 "$out/consumer-win32.log"
    exit 1
fi

[ "$keep" = 1 ] || rm -rf "$work" "$cache" "$out/unpacked"

echo
echo "  the hash for a consumer's build.zig.zon:"
echo "    .hash = \"$hash\","
echo
echo "A missing line means that step did not run, not that it passed."
