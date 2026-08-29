#!/usr/bin/env python3
"""Emit a C program that prints the Zig module's struct layouts as assertions
about the C header's.

There are three declarations of this API: include/ween32.h, the real Windows
headers, and zig/ween32.zig. `make win32` checks the first against the second,
field by field. Nothing checked the third, and it drifted -- MEMORYSTATUS was
declared with DWORD byte counts on both sides, and when the C header was fixed
to SIZE_T the Zig module went on saying DWORD for an hour, wrong in exactly
the way that reads correctly and compiles.

This closes the triangle. It compares the Zig module against the C header, and
the C header is already compared against win32, so a Zig struct that agrees
here agrees with Windows.

The numbers are nobody's opinion: the C offsets come from the host compiler
reading ween32.h, and the Zig offsets from @offsetOf in the Zig compiler. This
writes a C program that prints Zig, which is then compiled -- if a field is in
a different place on the two sides, the Zig compiler says so and says which.

    tools/zigbind/genstructs.py > dump.c
    cc -Iinclude dump.c -o dump && ./dump > check.zig
    zig build-obj check.zig

Reads zig/ween32.zig and include/ween32.h, writes C to stdout.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")
ZIG = os.path.join(ROOT, "zig", "ween32.zig")
HEADER = os.path.join(ROOT, "include", "ween32.h")

# Structs the Zig module declares that the C header does not, and why. A name
# that is on one side and not the other is otherwise a failure: the two are
# meant to be the same API.
ONLY_ZIG = {
    "BITMAPFILEHEADER": "the .bmp file header, which the Zig examples write "
                        "and no C caller has needed",
}


def zig_structs(text):
    """Every `pub const NAME = extern struct { ... };` and its field names, in
    declaration order."""
    out = []
    for m in re.finditer(r"pub const (\w+) = extern struct \{", text):
        name = m.group(1)
        # matched braces, because a struct is written on one line as often as
        # on ten and a regex that assumes either misreads the other: POINT is
        # `{ x: LONG, y: LONG }` and swallowed the whole of MSG.
        i = m.end() - 1
        depth = 0
        for j in range(i, len(text)):
            if text[j] == "{":
                depth += 1
            elif text[j] == "}":
                depth -= 1
                if depth == 0:
                    break
        body = text[i + 1:j]
        fields = []
        for part in re.split(r"[,\n]", body):
            part = part.strip()
            if not part or part.startswith("//") or part.startswith("///"):
                continue
            f = re.match(r"(\w+)\s*:", part)
            if f:
                fields.append(f.group(1))
        out.append((name, fields))
    return out


def c_structs(body):
    """The same, from the C header, so a field the Zig side has and the C side
    does not is caught rather than silently compared against nothing."""
    out = {}
    for m in re.finditer(r"typedef\s+struct\s*(?:\w+\s*)?\{(.*?)\}\s*([^;]+);",
                         body, re.S):
        inner, names = m.group(1), m.group(2)
        name = names.split(",")[0].strip()
        if not re.fullmatch(r"[A-Za-z_]\w*", name):
            continue
        # Comments first: the check below refuses a struct with a nested one
        # in it, and a comment that merely says "struct" is not that. TBBUTTON
        # went unread for exactly that reason.
        inner = re.sub(r"/\*.*?\*/", " ", inner, flags=re.S)
        # This builds for x86_64, so where win32 and the header both branch on
        # _WIN64 the 64-bit arm is the one that is real. Without this the
        # directives eat the fields around them: a `#endif` and the field after
        # it are one statement to a parser that splits on semicolons, so
        # TBBUTTON lost bReserved and dwData both.
        inner = re.sub(r"#\s*ifdef\s+_WIN64(.*?)#\s*else.*?#\s*endif",
                       r"\1", inner, flags=re.S)
        inner = re.sub(r"^\s*#.*$", "", inner, flags=re.M)
        inner = re.sub(r"__extension__\s*", " ", inner)
        inner = re.sub(r"union\s*\{([^{}]*)\}\s*;", r"\1;", inner)
        fields = []
        for stmt in inner.split(";"):
            stmt = re.sub(r"/\*.*?\*/", " ", stmt, flags=re.S).strip()
            if not stmt or stmt.startswith("#") or "{" in stmt:
                continue
            # `INT_PTR(CALLBACK *lpfnHook)(HWND, ...)` as well as `(*fn)`
            fp = re.search(r"\(\s*\w*\s*\*\s*(\w+)\s*\)", stmt)
            if fp:
                fields.append(fp.group(1))
                continue
            parts = stmt.split(None, 1)
            if len(parts) < 2:
                continue
            for decl in parts[1].split(","):
                d = re.sub(r"\[[^\]]*\]", "", decl).strip().lstrip("*").strip()
                if re.fullmatch(r"[A-Za-z_]\w*", d):
                    fields.append(d)
        out[name] = fields
    return out


def main():
    zig = zig_structs(open(ZIG, encoding="utf-8").read())
    c = c_structs(open(HEADER, encoding="utf-8").read().split("#else", 1)[1])

    print('/* Generated by tools/zigbind/genstructs.py — do not edit. */')
    print("#include <stdio.h>")
    print("#include <stddef.h>")
    print('#include <ween32.h>')  # angled, so a stale copy beside the generated
    # file cannot shadow the real header: this program is written to /tmp and a
    # quoted include searches /tmp first
    print("int main(void) {")
    print('    puts("// Generated — do not edit. See tools/zigbind/'
          'genstructs.py.");')
    print('    puts("const w = @import(\\"ween32\\");");')
    print('    puts("comptime {");')
    checked, missing, skipped = 0, [], []
    for name, fields in zig:
        if name in ONLY_ZIG:
            skipped.append(name)
            continue
        if name not in c:
            missing.append(name)
            continue
        for f in fields:
            if f not in c[name]:
                missing.append("%s.%s" % (name, f))
                continue
            print('    printf("    if (@offsetOf(w.%s, \\"%s\\") != %%zu) '
                  '@compileError(\\"%s.%s is not where the C header has it\\");'
                  '\\n", offsetof(%s, %s));' % (name, f, name, f, name, f))
            # And how wide it is, not only where it starts. A field whose type
            # changed width is only visible in an offset when the change moves
            # something, and alignment can absorb it: turning one SIZE_T into a
            # DWORD in MEMORYSTATUS leaves every offset and the size alone,
            # because the next field is eight-aligned and the padding takes it.
            # That is the exact bug this gate was built for, and it walked
            # through the offsets untouched until this line was added.
            print('    printf("    if (@sizeOf(@FieldType(w.%s, \\"%s\\")) '
                  '!= %%zu) @compileError(\\"%s.%s is not the width the C '
                  'header makes it\\");\\n", sizeof(((%s *)0)->%s));'
                  % (name, f, name, f, name, f))
        print('    printf("    if (@sizeOf(w.%s) != %%zu) '
              '@compileError(\\"%s is not the size the C header makes it\\");'
              '\\n", sizeof(%s));' % (name, name, name))
        checked += 1
    print('    puts("}");')
    print("    return 0;")
    print("}")

    print("/* %d Zig structs compared field for field against the C header */"
          % checked, file=sys.stderr)
    if skipped:
        print("/* %d declared only in Zig, and why, in genstructs.py: %s */"
              % (len(skipped), ", ".join(sorted(skipped))), file=sys.stderr)
    if missing:
        print("genstructs: declared in Zig and not in the C header, so not "
              "compared: %s" % ", ".join(sorted(missing)), file=sys.stderr)
        return 1
    return 0


sys.exit(main())
