#!/bin/bash
# Capture a reference render of the real win32 controls under Wine's classic
# (unthemed) look — the Windows 2000 look ween32 targets.
#
#   ./capture.sh                            -> reference.png (the sampler)
#   ./capture.sh menu.c menu-reference.png  -> another example
#   CLICK_AT=24,26 ./capture.sh menu.c ...  -> click there first, then capture
#
# CLICK_AT opens something that only exists while it is being tracked — a menu
# drop-down — so the reference can be taken with one up.
#
# Needs: wine, zig (to build the sampler), ImageMagick, xwininfo, an X display.
set -e
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
# A prefix of our own (~1.4G on first run), so the caller's ~/.wine is left
# alone and the classic scheme below cannot leak into it. Safe to delete.
export WINEPREFIX=${WINEPREFIX:-${XDG_CACHE_HOME:-$HOME/.cache}/ween32-refcapture}
export WINEDLLOVERRIDES="mscoree,mshtml="
export WINEDEBUG=-all LC_ALL=C

SRC=${1:-controls.c}
OUT=${2:-reference.png}
EXE=$(basename "$SRC" .c).exe

DESK_W=${DESK_W:-700} DESK_H=${DESK_H:-520}   # wine virtual desktop

if [ ! -d "$WINEPREFIX" ]; then
  echo "creating an isolated wine prefix in $WINEPREFIX"
  wineboot -i >/dev/null 2>&1
fi
# Wine >= 9 ships a "Light" theme, turns it on by default, and uses a modern
# light colour scheme. Neither is what Windows 2000 looked like: switch the
# theme off and write the classic scheme (the palette ween32 hardcodes) into
# this prefix — which is why we do not touch the caller's ~/.wine.
wine regedit "$here/win2000.reg" >/dev/null 2>&1

# The sampler is examples/controls.c — the same file ween32 builds as an
# example. Here it compiles against the real win32 headers, so every control
# shows up; against ween32 only the ones it has implemented do.
zig cc -target x86_64-windows-gnu -std=c99 -I"$repo/include" \
    "$repo/examples/$SRC" -luser32 -lgdi32 -lcomctl32 \
    -o "$here/$EXE"

# A wine virtual desktop makes wine draw its own caption and frame instead of
# handing the window to the host window manager.
wine explorer "/desktop=ween32ref,${DESK_W}x${DESK_H}" "$here/$EXE" \
     >/dev/null 2>&1 &

# Find that desktop window. Wine names it after /desktop=, but a tiling window
# manager may have resized it, so take the largest viewable window with that
# name rather than matching on geometry.
id=""
for _ in $(seq 40); do
  best=""; bestarea=0
  for cand in $(xwininfo -root -tree 2>/dev/null | grep -i ween32ref |
                grep -oE '0x[0-9a-f]+' | head -20); do
    read -r w h state <<<"$(xwininfo -id "$cand" 2>/dev/null |
      awk '/Width:/{w=$2} /Height:/{h=$2} /Map State:/{m=$3} END{print w, h, m}')"
    [ "$state" = "IsViewable" ] || continue
    area=$((w * h))
    if [ "$area" -gt "$bestarea" ]; then bestarea=$area; best=$cand; fi
  done
  # Take the largest viewable window with that name once it is big enough to
  # be the desktop rather than a transient. A tiling window manager may hand
  # wine something other than the size asked for, so this cannot insist on
  # the exact area.
  if [ -n "$best" ] && [ "$bestarea" -ge 40000 ]; then
    id=$best
    break
  fi
  sleep 1
done
if [ -z "$id" ]; then
  echo "wine desktop window not found" >&2
  wineserver -k 2>/dev/null || true
  exit 1
fi
sleep 3   # let the controls finish painting

# A drop-down exists only while the menu is being tracked, so it has to be
# opened before the shot and left open across it. The click goes to wine's
# desktop window at desktop coordinates.
if [ -n "$CLICK_AT" ]; then
  cc -O1 -o "$here/xclick" -x c - -lX11 -l:libXtst.so.6 <<'EOF'
/* A real click, through the XTest extension. A synthetic XSendEvent one is
 * marked send_event and wine will not start menu tracking on it. */
#include <X11/Xlib.h>
#include <stdlib.h>
extern int XTestFakeMotionEvent(Display *, int, int, int, unsigned long);
extern int XTestFakeButtonEvent(Display *, unsigned, int, unsigned long);
int main(int c, char **v) { (void)c;
  Display *d = XOpenDisplay(NULL); if (!d) return 1;
  Window w = (Window)strtoul(v[1], 0, 0);
  int x = atoi(v[2]), y = atoi(v[3]), rx, ry; Window kid;
  XTranslateCoordinates(d, w, DefaultRootWindow(d), x, y, &rx, &ry, &kid);
  XTestFakeMotionEvent(d, DefaultScreen(d), rx, ry, 0); XFlush(d);
  XTestFakeButtonEvent(d, 1, True, 50);
  XTestFakeButtonEvent(d, 1, False, 50);
  XFlush(d); XCloseDisplay(d); return 0; }
EOF
  "$here/xclick" "$id" "${CLICK_AT%,*}" "${CLICK_AT#*,}"
  sleep 2   # let the drop-down open and paint
fi

# A hot toolbar button exists only while the pointer is over it, so the shot
# has to be taken with the pointer parked there — a move with no click.
if [ -n "$HOVER_AT" ]; then
  cc -O1 -o "$here/xmove" -x c - -lX11 -l:libXtst.so.6 <<'EOF'
#include <X11/Xlib.h>
#include <stdlib.h>
extern int XTestFakeMotionEvent(Display *, int, int, int, unsigned long);
int main(int c, char **v) { (void)c;
  Display *d = XOpenDisplay(NULL); if (!d) return 1;
  Window w = (Window)strtoul(v[1], 0, 0);
  int x = atoi(v[2]), y = atoi(v[3]), rx, ry; Window kid;
  XTranslateCoordinates(d, w, DefaultRootWindow(d), x, y, &rx, &ry, &kid);
  XTestFakeMotionEvent(d, DefaultScreen(d), rx, ry, 0);
  XFlush(d); XCloseDisplay(d); return 0; }
EOF
  "$here/xmove" "$id" "${HOVER_AT%,*}" "${HOVER_AT#*,}"
  sleep 2   # let the hot state paint
fi

# Only wine's own desktop window is ever read — never the root window.
import -window "$id" "$here/desktop.png"
# The sampler is the only thing on that desktop, so trimming the uniform
# desktop background leaves exactly its window — no hardcoded geometry.
magick "$here/desktop.png" -fuzz 2% -trim +repage "$here/$OUT"
wineserver -k 2>/dev/null || true
echo "wrote $here/$OUT"
