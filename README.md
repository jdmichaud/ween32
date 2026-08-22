# ween32

**The classic win32 API, reimplemented small and portable.**

ween32 aims to let you write plain old win32 code — `RegisterClassA`,
`CreateWindowExA`, a `WndProc`, `BeginPaint`, `"BUTTON"` child windows sending
`WM_COMMAND` — and run it outside Windows, with the classic Windows 2000 look
reproduced pixel for pixel. The same program compiles unchanged against real
`windows.h` on Windows, where `<ween32.h>` simply defers to it.

![the classic calculator, recreated as plain win32 code](docs/calc.png)

A file browser, laid out from a screenshot of the real one — a rebar, a tree
and a list either side of a splitter, and a status bar in three parts:

![a Windows 2000 file browser](docs/explorer.png)

**This is a work in progress.** See [ROADMAP.md](ROADMAP.md) for what works
today and what does not, [docs/design.md](docs/design.md) for how it is
built, and [docs/testing.md](docs/testing.md) for how to check it.

The library code is MIT. The embedded fonts are Wine's redistributable Tahoma
and Marlett replacements and carry their own (LGPL) license.
