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

# **The cache is chosen with `ZIG_GLOBAL_CACHE_DIR`, not `--global-cache-dir`.**
# The flag is not accepted by every zig that can build this: jd's release run
# died on `error: unrecognized argument: --global-cache-dir` from `zig build`
# where the same line had just passed here, which is a version difference
# between two shells and not something this script can police. The environment
# variable is honoured by `zig build` and `zig fetch` alike and has no
# argument list to be rejected from -- so the gate stops depending on which
# zig is first on somebody's PATH.
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

# **From the commit, not from the directory.** `cp -r src` in a tree that has
# been built copies `src/*.o` in with it, and then the hash of a package is a
# fact about whether somebody has run `make` -- measured the hard way: 78
# files and one hash here against 51 files and a different hash in a clean
# worktree at the same sha. A hash that moves with the state of a build
# directory is worse than no hash, because it is reproducible for exactly one
# person. `git archive` takes tracked files at HEAD and nothing else.
#
# A function because the invariant below packages a second time, and the two
# have to be the same packaging or the check is comparing this script against
# a copy of itself that cannot go wrong.
make_tarball() {
    # **Flat: no `--prefix`, no top-level directory.** With one, this gate
    # fails -- `zig fetch` answers `ween32-0.1.0-jgasI...` and the consumer's
    # `zig build` resolves `N-V-__8AA...`, zig's "no name, no version"
    # sentinel, carrying the same size and content. That is what failed jd's
    # release.
    #
    # **The mechanism is not established and this comment will not invent
    # one.** What reproduces here, every time:
    #
    #   rooted, hash from `zig fetch` into cache C, build against C  mismatch
    #   the same rooted tarball, virgin cache, no fetch first        builds
    #   flat, either way                                             builds
    #
    # So the two tools disagree about a top-level directory somewhere, and
    # that ordering is where it shows. alice ran what she describes as the
    # same combinations without a failure, so the boundary is narrower than
    # either of us has pinned down. Flat is what ships because it works in
    # every combination either of us has tried, and **the hash is identical
    # either way** -- so nothing a consumer pins depends on the choice.
    git archive --format=tar HEAD -- $paths | gzip -n > "$1"
}

tarball="$out/$name.tar.gz"
make_tarball "$tarball"
files=$(tar tzf "$tarball" | grep -vc '/$' || true)
echo "  files $files, $(wc -c < "$tarball") bytes"
echo "  tarball $tarball"

# zig fetch is what a consumer runs, so it is what says the hash -- computing
# one here by any other means would be this script agreeing with itself.
# **Outside the repository as well, and for the same reason as the consumer
# below**: a zig cache that lives inside another package makes a fetch of
# that package come back anonymous. Both halves of this check now sit outside
# the tree they are checking.
cache=$(mktemp -d "${TMPDIR:-/tmp}/ween32-package-cache-XXXXXX")
trap 'rm -rf "$cache"' EXIT
hash=$(ZIG_GLOBAL_CACHE_DIR="$cache" zig fetch "$tarball" 2>/dev/null | tail -1)
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
# **Outside the repository, and that is not tidiness.** A build root nested
# inside another zig package cannot fetch a package by URL: with the consumer
# under `build/package/` the fetch comes back as `N-V-__8AA...`, zig's "no
# name, no version" sentinel, with the *same size and content* as the real
# package -- a root with no `build.zig.zon` visible in it. Moved to /tmp,
# byte for byte the same tarball and the same cache, it builds.
#
# That is what broke a release in jd's hands. This gate had been passing here
# only because ~/.cache/zig already held the package from earlier runs, so
# zig found it by hash and never took the fetch path at all; a cache without
# it -- anybody else's -- fetches, and fails.
work=$(mktemp -d "${TMPDIR:-/tmp}/ween32-package-check-XXXXXX")
trap 'rm -rf "$work" "$cache"' EXIT

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

# **Over HTTP, because `zig build` cannot fetch a `file://` URL.** `zig fetch`
# can, and answers the right hash; the build runner comes back with
# `N-V-__8AA...` -- zig's "no name, no version" sentinel, carrying the same
# size and content as the real package. That is what failed a release in jd's
# hands, and it is why this gate had been green here: `~/.cache/zig` already
# held the package from earlier runs, so zig found it by hash and never took
# the fetch path. A cache without it fetches, and fails.
#
# A URL is also what a consumer actually writes, so rehearsing one is the
# point rather than a workaround: notepad will name an https release asset,
# and the only thing this changes is which host answers.
serve_port_file=$(mktemp)
python3 - "$(dirname "$tarball")" "$serve_port_file" >/dev/null 2>&1 <<'PYEOF' &
import http.server, socketserver, sys
directory, port_file = sys.argv[1], sys.argv[2]


class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *a, **k):
        super().__init__(*a, directory=directory, **k)

    def log_message(self, *a):
        pass


with socketserver.TCPServer(("127.0.0.1", 0), Handler) as srv:
    with open(port_file, "w") as f:
        f.write(str(srv.server_address[1]))
    srv.serve_forever()
PYEOF
serve_pid=$!
trap 'kill $serve_pid 2>/dev/null; rm -f "$serve_port_file"; rm -rf "$work" "$cache"' EXIT
port=""
for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
    port=$(cat "$serve_port_file" 2>/dev/null || true)
    [ -n "$port" ] && break
    sleep 0.2
done
[ -n "$port" ] || { echo "  FAILED  could not start a local server to serve the package"
                    exit 1; }

cat > "$work/build.zig.zon" <<EOF
.{
    .name = .consumer,
    .version = "0.0.0",
    .fingerprint = 0x705b3727628aaf97,
    .minimum_zig_version = "0.17.0-dev.1158",
    .dependencies = .{
        .ween32 = .{
            .url = "http://127.0.0.1:$port/$name.tar.gz",
            .hash = "$hash",
        },
    },
    .paths = .{ "build.zig", "build.zig.zon", "main.c" },
}
EOF

# **In the same cache the hash was computed in**, which is the whole of the
# next paragraph. jd ran `release.sh minor` and this gate failed with a hash
# mismatch that had nothing to do with the package: the declared and the
# fetched hash decoded to the same size and the same bytes, differing only in
# zig's "no name, no version" sentinel -- a package root with no
# `build.zig.zon` visible in it, which is what a half-unpacked cache entry
# looks like. The hash above was computed in a pristine cache of our own and
# the consumer built out of `~/.cache/zig`, which four agents and jd share and
# which prints `failed deleting temporary directory ... DirNotEmpty` on every
# fetch on this filesystem.
#
# **Two statements about one directory, or the gate cannot say which half was
# wrong.** The trade is deliberate and worth stating: a real consumer uses the
# shared cache, so this proves *the package is coherent*, not that anybody's
# cache is healthy. For a release gate that is the right way round -- a gate
# that fails on somebody else's cache is one people learn to re-run rather
# than read.
if ( cd "$work" && ZIG_GLOBAL_CACHE_DIR="$cache" ZIG_LOCAL_CACHE_DIR="$work/.zig-cache" \
        zig build > "$out/consumer-native.log" 2>&1 ); then
    echo "  ok      a consumer fetches the package and builds against the host"
else
    echo "  FAILED  a consumer fetches the package and builds against the host"
    if grep -q "hash mismatch" "$out/consumer-native.log"; then
        echo "          both hashes below were made in one cache:"
        echo "            $cache"
        echo "          the one this script printed came from \`zig fetch\`"
        echo "          there, and the one the consumer resolved came from"
        echo "          \`zig build\` there, so a disagreement is the package"
        echo "          and not a cache somebody else was writing to."
    fi
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
# The tarball is flat, so the package *is* the directory it is unpacked into.
pkg="$out/unpacked"
rm -rf "$pkg"
mkdir -p "$pkg"
tar xzf "$tarball" -C "$pkg"
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

# ---- the invariant ----------------------------------------------------------
#
# **A file in a packaged directory that git does not track must not move the
# hash.** This is not a sabotage in the usual sense -- `git archive` cannot
# include an untracked file, so demonstrating that it does not would be
# demonstrating that git works. It is here for the *other* direction: nothing
# in this script or in CI would notice `git archive` being changed back to a
# `cp -r` of the working directory. The package would still build, the
# consumer would still be green, and the hash would quietly go back to being
# a fact about whose machine ran it -- 78 files on a built tree against 51 on
# a clean one, which is the bug this line exists to catch coming back.
#
# The planted file is a `.o` in `src/` on purpose: that is the case
# `git status` stays silent about, so it is the one a dirty-tree warning
# cannot cover.
plant="src/.package-invariant-$$.o"
# The trap goes on before the file does, and it is the point: with
# `set -e`, a `zig fetch` that fails here would exit the script with the
# plant still sitting in src/ -- and it is a gitignored dotfile, so nothing
# would ever mention it again. **The failing run is the one that leaks, so
# the cleanup belongs on the path a failure takes**, not on the line after
# the last success. (The same fault was fixed in the Python instruments the
# same evening, in the commit that was fixing cleanup.)
trap 'kill $serve_pid 2>/dev/null; rm -f "$plant" "$serve_port_file"; rm -rf "$work" "$cache"' EXIT
rm -f "$plant"
printf 'not tracked, must not be packaged\n' > "$plant"
make_tarball "$out/again.tar.gz"
again=$(ZIG_GLOBAL_CACHE_DIR="$out/again-cache" zig fetch "$out/again.tar.gz" \
        2>/dev/null | tail -1)
rm -f "$plant"
trap 'kill $serve_pid 2>/dev/null; rm -f "$serve_port_file"; rm -rf "$work" "$cache"' EXIT
rm -rf "$out/again.tar.gz" "$out/again-cache"
if [ "$again" = "$hash" ]; then
    echo "  ok      an untracked file in a packaged directory does not move"
    echo "          the hash, so the package is the commit's and not the"
    echo "          working directory's"
else
    echo "  FAILED  planting $plant moved the hash:"
    echo "            without it $hash"
    echo "            with it    $again"
    echo "          the package is being taken from the working directory"
    echo "          again, which makes it a fact about whoever ran it"
    exit 1
fi

[ "$keep" = 1 ] || rm -rf "$out/unpacked"
[ "$keep" = 1 ] && trap - EXIT && echo "  the consumer is at $work"

echo
echo "  the hash for a consumer's build.zig.zon:"
echo "    .hash = \"$hash\","
echo
echo "A missing line means that step did not run, not that it passed."
