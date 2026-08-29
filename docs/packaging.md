# Packaging ween32

A program that uses ween32 names it in its `build.zig.zon`, and there are two
ways to do that. **Both are right; they are right for different programs**, and
the difference is whether the program is being developed *against* a ween32
that changes hourly.

```zig
// a checkout beside this one
.ween32 = .{ .path = "../ween32" },

// a package anybody can fetch
.ween32 = .{
    .url = "https://github.com/jdmichaud/ween32/releases/download/v0.1.0/ween32-0.1.0.tar.gz",
    .hash = "ween32-0.1.0-...",
},
```

**The path form is for a program being built with the library.** wordpad is on
it: its frame and this library's controls move together, hour by hour, and a
`.url` would mean cutting a release before an application could use a fix that
was written for it. It also costs something, and the cost is worth naming: a
tree that has no ween32 beside it cannot build. That is why wordpad's
`verify.sh` puts its throwaway worktree beside the checkouts rather than in
`/tmp`.

**The URL form is for a program that is finished with the library.** notepad is
on it: it wants to be buildable anywhere, by anybody, with nothing beside it.

## Making the package

```sh
tools/package.sh --verify
```

writes `build/package/ween32-<version>.tar.gz`, prints the hash a consumer's
`build.zig.zon` needs, and then **builds a throwaway consumer from it** -- once
against the host through the library, once against real win32 out of the
packaged headers.

That last part is the point of the script rather than a flourish. **The one
mistake this arrangement makes is a file left out of `.paths`, and nothing in
this repository can see it**: `zig build`, `make`, and `tools/verify.sh` all
have the whole tree in front of them whether or not the manifest lists it. Only
something built from the tarball can tell. Taking `fonts` out of `.paths` was
tried while writing this: the package builds, fetches and hashes without a
murmur, and the consumer answers `compile lib ween32 1 errors`.

The consumer is written by the script rather than being one of ours, so that
when it fails there is exactly one thing it can be failing about; and it
`#include`s `<ween32.h>` as well as linking the library, because a package that
carries the sources and not the headers is the same failure as
`installHeadersDirectory` staging nothing over sshfs -- the one
`ween32.addHeaders` exists to route around.

### The invariant, and the bug it is there to catch coming back

The tarball is `git archive` of HEAD. The first draft copied the listed paths
out of the working directory, which in any tree that has been built copies
`src/*.o` along with them: **78 files here against 51 in a clean worktree at
the same commit, and two different hashes for one sha**. A package hash that
moves with a build directory is worse than none, because it is reproducible for
exactly one person -- and that person is whoever wrote the script, whose tree is
always built.

So `--verify` packages **twice in one run**, through the same function, with an
untracked file planted in a packaged directory between them, and fails if the
two hashes differ. The plant is a `.o` in `src/` on purpose: that is the case
`git status` says nothing about, so it is the one the dirty-tree warning cannot
cover.

This is not quite a sabotage -- `git archive` cannot include an untracked file,
so watching it not do so would be watching git work. It is there for the other
direction: **nothing else would notice `git archive` being changed back to a
`cp -r`.** The package would still build, the consumer would still be green, and
the hash would quietly go back to being a fact about whose machine ran it. With
that change made on purpose, the check says:

```
  FAILED  planting src/.package-invariant-243211.o moved the hash:
            without it ween32-0.1.0-jgasIMLBSACaby9G1B66j7U7UE4kV7mCqpXMLnvn8Q22
            with it    ween32-0.1.0-jgasIOTBSAAAd7jg3MfNCbSlbb1aU9gwq6m2gW19GuOh
          the package is being taken from the working directory
          again, which makes it a fact about whoever ran it
```

and exits 1.

**The cleanup is a `trap`, not the next statement.** With `set -e`, a
`zig fetch` that fails while the plant is down would exit with the file still
in `src/` -- and it is a gitignored dotfile, so nothing would ever mention it
again. Measured both ways, with the fetch made to fail on purpose:

```
without the trap   exit 1, and src/.package-invariant-248115.o left behind
with the trap      exit 1, and nothing left behind
```

The general form is worth more than the fix: **the failing run is the one that
leaks, so cleanup belongs on the path a failure takes.** The same fault was
found and fixed in the Python instruments the same evening -- in a commit that
was itself about cleanup.

## Two things about `zig fetch` that are worth knowing before you design around them

**The hash is of the unpacked tree, not of the tarball's bytes.** Measured with
two tarballs of the same files: 1,315,509 bytes against 1,333,473, different
mtimes, different member order, different md5 -- and the same hash. So this
does not need a reproducible archive, and a timestamp inside one is harmless.
What must not move is the **file set**: the whole repository hashes differently
from the `.paths` set, so what is served has to be what was hashed.

**A `.path` dependency cannot be a tarball.** Zig wants a directory there and
says `NotDir`. A package is consumed by URL or not at all -- which is why
`package.sh` verifies through a `file://` URL rather than a path, and why it
needs no server to do it.

## Releasing

```sh
tools/release.sh [patch|minor|major]
```

runs `verify.sh` and `package.sh --verify` **before** it tags -- a tag pointing
at a commit that fails its own checks cannot be withdrawn once anybody has
fetched it -- then tags `v<version>`, bumps `.version` in `build.zig.zon` and
commits that. Nothing is pushed; it prints the two push commands.

CI does the rest: the `package` job runs `tools/package.sh --verify` on every
push, and on a `v*` tag the same job attaches the tarball to the release. **The
hash in the job's output is the one a consumer pins.**

## Moving a program from one form to the other

To go from a checkout to a package:

```sh
cd ween32 && tools/package.sh          # prints the hash
```

and put the release URL and that hash in the program's `build.zig.zon`. To go
back, replace both fields with `.path = "../ween32"`. Nothing else in a build
script changes: `b.dependency("ween32", ...)`, `w.artifact("ween32")`,
`ween32.addHeaders` and `ween32.addResources` read the same either way.
