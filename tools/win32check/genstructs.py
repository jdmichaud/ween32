#!/usr/bin/env python3
"""Emit a C program that prints this library's struct layouts as assertions
about win32's.

The constants gate compares every number ween32 declares against win32's, and
the alias gate compares every name against what it stands for. Neither of them
can see a *struct* that has the right name and the wrong shape — which is how
REBARBANDINFOA sat here eight fields short of win32's for as long as it did,
with every constant around it agreeing and every example compiling.

The reason that looked uncheckable is that a general checker would have to
assert something about every type ween32 declares, and ween32 implements a
subset on purpose, so the exception list would be longer than the header. But
the set worth checking is not every type: it is the types an *application*
fills in and hands to the library. That is a list, and a list is something a
gate can carry.

Nothing here is written by hand, because a hand-written offset is a number
that drifts from the header it describes. Three steps:

  1. this reads include/ween32.h for the structs and their fields, in
     declaration order, and knows nothing about their sizes;
  2. it writes a C program that includes ween32.h, which the host compiler
     builds -- so the offsets are whatever *our* ABI really produces;
  3. running that program prints _Static_asserts, which are compiled against
     the real windows.h for x86_64-windows-gnu.

So a field that is missing, a field in the wrong order, and a field whose type
is the wrong width all fail -- the third being the one no eye catches, since a
UINT where win32 has an LPARAM reads perfectly and puts every field after it
eight bytes out.

Reads include/ween32.h, writes C to stdout.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
HEADER = os.path.join(HERE, "..", "..", "include", "ween32.h")

# Structs win32 does not have, or has in a shape we deliberately do not copy,
# and why. Everything else ween32 declares must agree field for field.
SKIP = {
    "BITMAP": "ween32's is the software surface, not GDI's DDB descriptor",
}

# Structs whose *size* is allowed to differ because win32's own definition
# grows a tail behind a version guard that ween32 deliberately stops before.
# Every field ween32 does declare is still checked, so this excuses the size
# and nothing else -- a field in the wrong place still fails.
# Fields whose own width is allowed to differ, for the same reason: the field
# is itself one of the structs above, so it carries that struct's guarded tail.
# Its offset is still checked, and so is every field of the struct it holds.
WIDTH_DIFFERS = {
    "TVINSERTSTRUCTA.itemex": "it is a TVITEMEXA, whose tail is guarded",
    "NMLVDISPINFOA.item": "it is an LVITEMA, whose tail is guarded",
}

SIZE_DIFFERS = {
    "REBARBANDINFOA": "rcChevronLocation and uChevronState, NTDDI >= Vista",
    "LVITEMA": "piColFmt and iGroup, NTDDI >= Vista",
    "LVCOLUMNA": "cxMin, cxDefault and cxIdeal, NTDDI >= Vista",
    "LVHITTESTINFO": "iGroup, NTDDI >= Vista",
    "HDITEMA": "state, NTDDI >= Vista",
    "TVITEMEXA": "uStateEx, hwnd and iExpandedImage from IE6, iReserved later",
    "NMLVDISPINFOA": "it carries an LVITEMA, whose tail is guarded as above",
    "TVINSERTSTRUCTA": "its union carries a TVITEMEXA, guarded as above",
    "PROPSHEETPAGEA": "win32's plain name is its V3 struct; ween32 fills in "
                      "V1, which win32 versions by dwSize and sizes itself "
                      "with PROPSHEETPAGEA_V1_SIZE",
}


def structs_of(body):
    """Every `typedef struct [tag] { ... } NAME[, *ALIAS...];` in declaration
    order, as (name, [field, ...]). A struct this cannot read confidently is
    handed back with no fields, so that it is reported rather than skipped."""
    out = []
    for m in re.finditer(r"typedef\s+struct\s*(?:\w+\s*)?\{(.*?)\}\s*([^;]+);",
                         body, re.S):
        inner, names = m.group(1), m.group(2)
        name = names.split(",")[0].strip()
        if not re.fullmatch(r"[A-Za-z_]\w*", name):
            continue
        # An unnamed union's members are all fields of the struct at the same
        # offset, and offsetof reaches them by name, so they are read like any
        # other. A *named* nested struct or union is not, and the struct is
        # reported as unread rather than skipped quietly.
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
        if "union" in inner or "struct" in inner:
            out.append((name, None))
            continue
        fields = []
        for line in inner.split(";"):
            line = re.sub(r"/\*.*?\*/", " ", line, flags=re.S).strip()
            if not line or line.startswith("#"):
                continue
            if "(" in line:  # a function pointer member: name is inside it
                fp = re.search(r"\(\s*\*\s*(\w+)\s*\)", line)
                if fp:
                    fields.append(fp.group(1))
                    continue
                fields = None
                break
            # `UINT a, b, c` / `char sz[80]` / `LPSTR *p`
            parts = line.split(None, 1)
            if len(parts) < 2:
                fields = None
                break
            for decl in parts[1].split(","):
                d = re.sub(r"\[[^\]]*\]", "", decl).strip().lstrip("*").strip()
                if re.fullmatch(r"[A-Za-z_]\w*", d):
                    fields.append(d)
                else:
                    fields = None
                    break
            if fields is None:
                break
        out.append((name, fields))
    return out


def main():
    body = open(HEADER, encoding="utf-8").read().split("#else", 1)[1]
    print('/* Generated by tools/win32check/genstructs.py — do not edit. */')
    print("#include <stdio.h>")
    print("#include <stddef.h>")
    print('#include <ween32.h>')  # angled, so a stale copy beside the generated
    # file cannot shadow the real header: this program is written to /tmp and a
    # quoted include searches /tmp first
    print("int main(void) {")
    for line in ('#include <windows.h>', '#include <commctrl.h>',
                 '#include <commdlg.h>', '#include <richedit.h>',
                 '#include <stddef.h>'):
        print('    puts("%s");' % line)
    checked, unread = 0, []
    for name, fields in structs_of(body):
        if name in SKIP:
            continue
        if not fields:
            unread.append(name)
            continue
        for f in fields:
            print('    printf("_Static_assert(offsetof(%s, %s) == %%zu, '
                  '\\"%s.%s\\");\\n", offsetof(%s, %s));'
                  % (name, f, name, f, name, f))
            # And the field's own width. An offset only shows a type that
            # changed width when the change moves something, and alignment can
            # absorb it: one SIZE_T made a DWORD in the middle of MEMORYSTATUS
            # leaves every offset and the size alone, because the next field is
            # eight-aligned and the padding takes it. That is the bug this gate
            # was built for and it walked through the offsets untouched.
            if "%s.%s" % (name, f) in WIDTH_DIFFERS:
                continue
            print('    printf("_Static_assert(sizeof(((%s *)0)->%s) == %%zu, '
                  '\\"%s.%s width\\");\\n", sizeof(((%s *)0)->%s));'
                  % (name, f, name, f, name, f))
        if name not in SIZE_DIFFERS:
            print('    printf("_Static_assert(sizeof(%s) == %%zu, '
                  '\\"sizeof %s\\");\\n", sizeof(%s));' % (name, name, name))
        checked += 1
    print('    puts("int main(void) { return 0; }");')
    print("    return 0;")
    print("}")
    print("/* %d structs compared field for field against the real headers */"
          % checked, file=sys.stderr)
    if unread:
        print("/* %d not read by the generator, so not compared: %s */"
              % (len(unread), ", ".join(sorted(unread))), file=sys.stderr)
    if SKIP:
        print("/* %d skipped on purpose, and why, in genstructs.py: %s */"
              % (len(SKIP), ", ".join(sorted(SKIP))), file=sys.stderr)
    print("/* %d fields not compared by width, being structs with a guarded "
          "tail of their own: %s */"
          % (len(WIDTH_DIFFERS), ", ".join(sorted(WIDTH_DIFFERS))),
          file=sys.stderr)
    print("/* %d compared field by field but not by size, win32's own tail "
          "being version-guarded: %s */"
          % (len(SIZE_DIFFERS), ", ".join(sorted(SIZE_DIFFERS))),
          file=sys.stderr)


main()
