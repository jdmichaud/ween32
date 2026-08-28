#!/usr/bin/env python3
"""Emit a C file that checks every constant ween32 declares against the value
the real Windows headers give it.

ween32's promise is that a program compiles unchanged on either side, which
only holds if the numbers agree. They are copied by hand, so they can be
copied wrong: this found TVN_SELCHANGEDA off by one — it was the value of
TVN_SELCHANGING, so an app would have acted on the selection before it
changed — and TB_ISBUTTONCHECKED sitting on TB_ISBUTTONHIDDEN's number.

A name win32 does not define at all is the other half of that promise, and
this used to miss it: every check was wrapped in `#ifdef`, so a constant only
ween32 had was skipped — and still counted, which made the total say it had
been compared. An application could then use an invented name, build here and
fail to build on Windows, which is the one thing this gate is for.

So an absent name is a failure now. The handful that are genuinely absent are
named in ABSENT below, each with the reason it is; a name reaching that
failure without a reason beside it is a conversation, not a silent pass.

A T-layer alias -- LOGFONT for LOGFONTA -- is checked as whatever it stands
for: a type through __builtin_types_compatible_p, a class name through
__builtin_strcmp, a number as a number, a call by existing. The type check is
emitted outside any #ifdef, because win32 spells its T-names as typedefs and
a typedef is invisible to the preprocessor; one line then answers both "is it
there" and "does it mean the same". The cost is that a type alias win32 has
not got fails with the compiler's own "unknown type name" rather than with
the sentence below telling the reader to name it in ABSENT -- so if that is
what you are looking at, that is what it means.

Reads include/ween32.h, writes C to stdout, and says on stderr how many it
compared and how many it knowingly did not.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
HEADER = os.path.join(HERE, "..", "..", "include", "ween32.h")

# Values that are not integer expressions, or that win32 spells differently
# enough that comparing the text is meaningless.
SKIP = {"CALLBACK", "WINAPI", "APIENTRY", "CONST", "FAR", "NEAR", "PASCAL",
        "VOID", "IN", "OUT", "OPTIONAL"}

# Constants the real headers do not define, and why. Everything else must be
# there. These are still compared wherever they do turn up, so a reason that
# stops being true costs nothing and a name that leaves ween32.h is reported
# rather than left sitting here.
#
# The first five: 64-bit win32 defines them and then takes them straight back,
# `#undef GWL_WNDPROC` and its neighbours under `#ifdef _WIN64` in winuser.h,
# because a pointer no longer fits where a LONG did. The GWLP_/DWLP_ forms are
# what exist there, and x86_64 is what this gate builds for.
ABSENT = {
    "GWL_WNDPROC": "undefined on 64-bit win32; GWLP_WNDPROC is the one there",
    "GWL_USERDATA": "undefined on 64-bit win32; GWLP_USERDATA is the one there",
    "DWL_DLGPROC": "undefined on 64-bit win32; DWLP_DLGPROC is the one there",
    "DWL_MSGRESULT": "undefined on 64-bit win32; DWLP_MSGRESULT is the one there",
    "DWL_USER": "undefined on 64-bit win32; DWLP_USER is the one there",
    # Real, and 0x3021 is the number the Microsoft SDK's prsht.h gives it, but
    # mingw-w64 has never carried it and mingw-w64 is the header set this
    # compiles against. Including prsht.h does not help: commctrl.h already
    # pulls it in, which is why PSN_APPLY and the PSH_ flags are compared.
    "IDD_APPLYNOW": "in the Microsoft SDK's prsht.h, absent from mingw-w64",
}


def kinds_of(header):
    """What each name in ween32.h *is*, so that an alias can be checked as
    what it stands for. A T-layer alias resolves to a type, a string, a
    number or a call, and only the first three can be compared at all."""
    types, strings, calls = set(), set(), set()
    # typedef struct tagX { ... } LOGFONTA, *PLOGFONTA, *LPLOGFONTA;
    for m in re.finditer(r"\}\s*([A-Za-z_][\w,\s\*]*?);", header):
        for part in m.group(1).split(","):
            part = part.strip().lstrip("*").strip()
            if re.fullmatch(r"[A-Za-z_]\w*", part):
                types.add(part)
    # typedef DWORD LCID;  /  typedef WCHAR *LPWSTR, *PWSTR;
    for m in re.finditer(r"^typedef\s+[\w\s\*]+?([A-Za-z_]\w*)\s*;", header, re.M):
        types.add(m.group(1))
    for m in re.finditer(r'^#define\s+([A-Za-z_]\w*)\s+"', header, re.M):
        strings.add(m.group(1))
    # function-like macros, and functions this header declares
    for m in re.finditer(r"^#define\s+([A-Za-z_]\w*)\(", header, re.M):
        calls.add(m.group(1))
    for m in re.finditer(r"^[A-Za-z_][\w\s\*]*?\b([A-Za-z_]\w*)\s*\(", header, re.M):
        calls.add(m.group(1))
    return types, strings, calls


def main():
    header = open(HEADER, encoding="utf-8").read()
    types, strings, calls = kinds_of(header)
    body = header.split("#else", 1)[1]
    out = ["/* Generated by tools/win32check/genconsts.py — do not edit. */",
           "#include <windows.h>",
           "#include <commctrl.h>"]
    compared, aliases, absent = 0, 0, []
    for m in re.finditer(r"^#define\s+([A-Z][A-Z0-9_]{2,})\s+(.+?)\s*(?:/\*.*)?$",
                         body, re.M):
        name, val = m.group(1), m.group(2).strip()
        if name in SKIP or name.startswith("WEEN"):
            continue
        if not re.fullmatch(r"[-+*/()\s0-9A-Za-z_ULul]+", val):
            continue          # not an integer expression
        if re.search(r"\bsizeof\b", val):
            continue
        check = ('_Static_assert((long long)(%s) == (long long)(%s), "%s");'
                 % (name, val, name))
        if name in ABSENT:
            # Known missing: compare it anyway wherever it is not.
            out += ["/* %s: %s */" % (name, ABSENT[name]),
                    "#ifdef %s" % name, check, "#endif"]
            absent.append(name)
            continue
        # A name whose value is another name is an alias rather than a number:
        # the T-layer's LOGFONT for LOGFONTA, SB_SETTEXT for SB_SETTEXTA,
        # MAKEINTRESOURCE for MAKEINTRESOURCEA. What has to hold for one of
        # these is that win32 has the name -- and it may have it as a typedef
        # or as a function-like macro, neither of which can be compared as a
        # number, so nothing is: what the alias resolves to is a name of
        # win32's own and is compared on its own line. A name win32 has not
        # got at all is still a failure, since the typedef below will not
        # compile.
        if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", val):
            # What the alias stands for, or what it would stand for if it
            # pointed at the other half: an alias naming LOGFONTW is still a
            # type alias, and a wrong one, which is the case worth catching.
            def like(kind, v=val):
                return v in kind or (v.endswith(("A", "W")) and
                                     v[:-1] + "A" in kind)
            # Where win32 has the name too, the two are compared as whatever
            # they are: types with __builtin_types_compatible_p, class names
            # with __builtin_strcmp, numbers as numbers. That is what catches
            # a T-name pointing at the wrong half -- LOGFONT at LOGFONTW on a
            # build without UNICODE -- which merely existing would not.
            if like(types):
                # A type is compared outright rather than under an #ifdef:
                # win32 spells its T-names as typedefs, which no #ifdef can
                # see, and a name it has not got at all fails here as an
                # unknown type. One line that is both checks at once.
                out.append('_Static_assert(__builtin_types_compatible_p(%s, %s)'
                           ', "%s");' % (name, val, name))
                aliases += 1
                continue
            if like(strings):
                same = ('_Static_assert(__builtin_strcmp(%s, %s) == 0, "%s");'
                        % (name, val, name))
            elif like(calls):
                same = None  # a call: nothing about it is a constant
            else:
                same = check
            out += ["#ifndef %s" % name,
                    "typedef %s ween_typeck_%s; /* a type there, then */"
                    % (name, name),
                    "#endif"]
            if same:
                out += ["#ifdef %s" % name, same, "#endif"]
            aliases += 1
            continue
        out += ["#ifndef %s" % name,
                '#error "%s: ween32 declares it, the real headers do not. '
                'If that is right, name it in ABSENT in '
                'tools/win32check/genconsts.py with the reason."' % name,
                "#else", check, "#endif"]
        compared += 1
    out.append("int main(void) { return 0; }")
    print("\n".join(out))

    stale = sorted(set(ABSENT) - set(absent))
    if stale:
        print("genconsts: ABSENT names ween32.h no longer declares, so the "
              "reason beside each is now unread: %s" % ", ".join(stale),
              file=sys.stderr)
        return 1
    print("/* %d constants compared against the real headers */" % compared,
          file=sys.stderr)
    print("/* %d name aliases checked to exist there, as a macro or a type */"
          % aliases, file=sys.stderr)
    print("/* %d known absent, and why, in genconsts.py: %s */"
          % (len(absent), ", ".join(sorted(absent))), file=sys.stderr)
    return 0


sys.exit(main())
