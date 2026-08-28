# ween32 — the classic win32 API, reimplemented small and portable.
#
#   make            build libween32.a (+ examples on X11 hosts)
#   make test       build & run the headless test suite (no display needed)
#   make X11=0      build without the X11 backend (headless only)

CC      ?= cc
CFLAGS  ?= -O2
# -Wundef: a feature gate that names a flag nobody defines is a feature
# silently switched off, which is how the explorer lost its cursors.
CFLAGS  += -std=c99 -Wall -Wextra -Werror -pedantic -Wundef -Iinclude
X11     ?= 1

OBJS = src/surface.o src/classic.o src/font.o src/marlett.o src/fonts.o src/propsheet.o \
       src/file.o src/kernel.o src/shell.o src/shellart.o src/cursorart.o src/gdi.o src/draw.o src/comdlg.o src/printing.o src/menu.o src/imagelist.o src/user.o src/dialog.o src/registry.o src/resource.o src/resource_none.o src/winmain.o src/controls.o src/headless.o src/x11.o

LIBS =
ifeq ($(X11),1)
src/x11.o: CFLAGS += -DWEEN_BACKEND_X11
LIBS += -lX11
endif

# One list, used to build them, to run them and to clean them. Kept in one
# place because it drifted: three of the tests were run without being named
# as something `test` depends on, so a stale binary could report a pass for
# code that was no longer there.
TESTS = tests/render_test tests/api_test tests/dlg_test tests/input_test \
        tests/resize_test tests/multiwin_test tests/timer_test \
        tests/keys_test tests/menu_test tests/modal_test tests/clip_test \
        tests/image_test tests/geometry_test tests/views_test \
        tests/toolbar_test tests/draw_test tests/popup_test \
        tests/propsheet_test tests/comdlg_test tests/resource_test \
        tests/edit_test tests/registry_test tests/kernel_test

EXAMPLES = examples/dialog examples/calc examples/controls examples/menu \
           examples/explorer

all: libween32.a $(EXAMPLES)

libween32.a: $(OBJS)
	ar rcs $@ $(OBJS)

$(OBJS): src/ween_internal.h include/ween32.h
src/fonts.o: fonts/tahoma_ttf.h fonts/tahomabd_ttf.h fonts/marlett_ttf.h \
             fonts/mssans_ttf.h

examples/dialog: examples/dialog.c libween32.a
	$(CC) $(CFLAGS) -o $@ examples/dialog.c libween32.a $(LIBS)

examples/calc: examples/calc.c examples/win32_dlg.h libween32.a
	$(CC) $(CFLAGS) -o $@ examples/calc.c libween32.a $(LIBS) -lm

# The control sampler is shared with tools/refcapture, which builds the very
# same file against real win32 to produce the reference render.
examples/controls: examples/controls.c libween32.a
	$(CC) $(CFLAGS) -o $@ examples/controls.c libween32.a $(LIBS)

examples/menu: examples/menu.c libween32.a
	$(CC) $(CFLAGS) -o $@ examples/menu.c libween32.a $(LIBS)

examples/explorer: examples/explorer.c examples/fs.h libween32.a
	$(CC) $(CFLAGS) -o $@ examples/explorer.c libween32.a $(LIBS)

tests/render_test: tests/render_test.c libween32.a
	$(CC) $(CFLAGS) -o $@ tests/render_test.c libween32.a $(LIBS)

tests/api_test: tests/api_test.c libween32.a
	$(CC) $(CFLAGS) -o $@ tests/api_test.c libween32.a $(LIBS)

tests/dlg_test: tests/dlg_test.c examples/win32_dlg.h libween32.a
	$(CC) $(CFLAGS) -o $@ tests/dlg_test.c libween32.a $(LIBS)

tests/input_test: tests/input_test.c libween32.a
	$(CC) $(CFLAGS) -o $@ tests/input_test.c libween32.a $(LIBS)

tests/resize_test: tests/resize_test.c libween32.a
	$(CC) $(CFLAGS) -o $@ tests/resize_test.c libween32.a $(LIBS)

tests/multiwin_test: tests/multiwin_test.c libween32.a
	$(CC) $(CFLAGS) -o $@ tests/multiwin_test.c libween32.a $(LIBS)

tests/popup_test: tests/popup_test.c libween32.a
	$(CC) $(CFLAGS) -o $@ tests/popup_test.c libween32.a $(LIBS)

tests/propsheet_test: tests/propsheet_test.c libween32.a
	$(CC) $(CFLAGS) -o $@ tests/propsheet_test.c libween32.a $(LIBS)

tests/comdlg_test: tests/comdlg_test.c libween32.a
	$(CC) $(CFLAGS) -o $@ tests/comdlg_test.c libween32.a $(LIBS)

tests/resource_test: tests/resource_test.c libween32.a
	$(CC) $(CFLAGS) -o $@ tests/resource_test.c libween32.a $(LIBS)

tests/edit_test: tests/edit_test.c libween32.a
	$(CC) $(CFLAGS) -o $@ tests/edit_test.c libween32.a $(LIBS)

tests/registry_test: tests/registry_test.c libween32.a
	$(CC) $(CFLAGS) -o $@ tests/registry_test.c libween32.a $(LIBS)

tests/kernel_test: tests/kernel_test.c libween32.a
	$(CC) $(CFLAGS) -o $@ tests/kernel_test.c libween32.a $(LIBS)

tests/timer_test: tests/timer_test.c libween32.a
	$(CC) $(CFLAGS) -o $@ tests/timer_test.c libween32.a $(LIBS)

tests/keys_test: tests/keys_test.c libween32.a
	$(CC) $(CFLAGS) -o $@ tests/keys_test.c libween32.a $(LIBS)

tests/menu_test: tests/menu_test.c libween32.a
	$(CC) $(CFLAGS) -o $@ tests/menu_test.c libween32.a $(LIBS)

tests/modal_test: tests/modal_test.c examples/win32_dlg.h libween32.a
	$(CC) $(CFLAGS) -o $@ tests/modal_test.c libween32.a $(LIBS)

tests/clip_test: tests/clip_test.c libween32.a
	$(CC) $(CFLAGS) -o $@ tests/clip_test.c libween32.a $(LIBS)

tests/image_test: tests/image_test.c libween32.a
	$(CC) $(CFLAGS) -o $@ tests/image_test.c libween32.a $(LIBS)

tests/geometry_test: tests/geometry_test.c libween32.a
	$(CC) $(CFLAGS) -o $@ tests/geometry_test.c libween32.a $(LIBS)

tests/views_test: tests/views_test.c libween32.a
	$(CC) $(CFLAGS) -o $@ tests/views_test.c libween32.a $(LIBS)

tests/toolbar_test: tests/toolbar_test.c libween32.a
	$(CC) $(CFLAGS) -o $@ tests/toolbar_test.c libween32.a $(LIBS)

tests/draw_test: tests/draw_test.c libween32.a
	$(CC) $(CFLAGS) -o $@ tests/draw_test.c libween32.a $(LIBS)

test: $(TESTS)
	@for t in $(TESTS); do ./$$t || exit 1; done

# The other half of the promise: the same example source has to build against
# the real windows.h, and the constants have to be the numbers Windows gives
# them. Needs zig for its bundled mingw-w64 headers; skipped without it.
ZIGWIN = $(ZIG) cc -target x86_64-windows-gnu -std=c99 -Iinclude

# Which zig. The C half needs nothing but its bundled mingw-w64 headers, so
# any will do; the package -- and with it examples/paint, which is written
# in Zig -- needs the version build.zig.zon asks for, and is skipped rather
# than failed when the one on PATH is older.
ZIG ?= zig
ZIG_NEEDS = 0.17

win32:
	@command -v $(ZIG) >/dev/null || { echo "win32: zig not installed, skipped"; exit 0; }
	@for src in $(EXAMPLES:%=%.c); do \
	   echo "  win32 $$src"; \
	   $(ZIGWIN) $$src -luser32 -lgdi32 -lcomctl32 -lshell32 -o /tmp/ween32-win32.exe || exit 1; \
	 done
	@python3 tools/win32check/genconsts.py > /tmp/ween32-consts.c
	@$(ZIG) cc -target x86_64-windows-gnu -std=c11 -o /tmp/ween32-consts.exe \
	   /tmp/ween32-consts.c && echo "  win32 constants agree"
	@python3 tools/win32check/genstructs.py > /tmp/ween32-dump.c
	@$(CC) -std=c99 -Iinclude -o /tmp/ween32-dump /tmp/ween32-dump.c
	@/tmp/ween32-dump > /tmp/ween32-structs.c
	@$(ZIG) cc -target x86_64-windows-gnu -std=c11 -o /tmp/ween32-structs.exe \
	   /tmp/ween32-structs.c && echo "  win32 structs agree"
	@python3 tools/zigbind/checkconsts.py include/ween32.h zig/ween32.zig
	@python3 tools/zigbind/genstructs.py > /tmp/ween32-zdump.c
	@$(CC) -std=c99 -Iinclude -o /tmp/ween32-zdump /tmp/ween32-zdump.c
	@/tmp/ween32-zdump > /tmp/ween32-zcheck.zig
	@$(ZIG) build-obj -femit-bin=/tmp/ween32-zcheck.o \
	   --dep ween32 -Mroot=/tmp/ween32-zcheck.zig -Mween32=zig/ween32.zig \
	   && echo "  zig binding agrees with the header"
	@case "$$($(ZIG) version)" in \
	   $(ZIG_NEEDS)*) echo "  win32 examples/paint (zig)"; \
	      $(ZIG) build paint -Dtarget=x86_64-windows-gnu || exit 1;; \
	   *) echo "  win32 examples/paint: needs zig $(ZIG_NEEDS), skipped";; \
	 esac

clean:
	rm -f $(OBJS) libween32.a $(EXAMPLES) $(TESTS)

.PHONY: all test win32 clean
