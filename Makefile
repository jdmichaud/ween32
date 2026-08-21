# ween32 — the classic win32 API, reimplemented small and portable.
#
#   make            build libween32.a (+ examples on X11 hosts)
#   make test       build & run the headless test suite (no display needed)
#   make X11=0      build without the X11 backend (headless only)

CC      ?= cc
CFLAGS  ?= -O2
CFLAGS  += -std=c99 -Wall -Wextra -Werror -pedantic -Iinclude
X11     ?= 1

OBJS = src/surface.o src/classic.o src/font.o src/marlett.o src/fonts.o \
       src/gdi.o src/menu.o src/user.o src/dialog.o src/controls.o src/headless.o src/x11.o

LIBS =
ifeq ($(X11),1)
src/x11.o: CFLAGS += -DWEEN_BACKEND_X11
LIBS += -lX11
endif

all: libween32.a examples/dialog examples/calc examples/controls examples/menu

libween32.a: $(OBJS)
	ar rcs $@ $(OBJS)

$(OBJS): src/ween_internal.h include/ween32.h
src/fonts.o: fonts/tahoma_ttf.h fonts/tahomabd_ttf.h fonts/marlett_ttf.h

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

tests/timer_test: tests/timer_test.c libween32.a
	$(CC) $(CFLAGS) -o $@ tests/timer_test.c libween32.a $(LIBS)

tests/keys_test: tests/keys_test.c libween32.a
	$(CC) $(CFLAGS) -o $@ tests/keys_test.c libween32.a $(LIBS)

tests/menu_test: tests/menu_test.c libween32.a
	$(CC) $(CFLAGS) -o $@ tests/menu_test.c libween32.a $(LIBS)

tests/modal_test: tests/modal_test.c examples/win32_dlg.h libween32.a
	$(CC) $(CFLAGS) -o $@ tests/modal_test.c libween32.a $(LIBS)

test: tests/render_test tests/api_test tests/dlg_test tests/input_test \
      tests/resize_test tests/multiwin_test tests/timer_test \
      tests/keys_test tests/menu_test \
      tests/modal_test
	./tests/render_test
	./tests/api_test
	./tests/dlg_test
	./tests/input_test
	./tests/resize_test
	./tests/multiwin_test
	./tests/timer_test
	./tests/keys_test
	./tests/menu_test
	./tests/modal_test

clean:
	rm -f $(OBJS) libween32.a examples/dialog examples/calc examples/controls examples/menu \
	      tests/render_test tests/api_test tests/dlg_test tests/input_test \
	      tests/resize_test tests/multiwin_test tests/timer_test tests/keys_test tests/menu_test tests/timer_test

.PHONY: all test clean
