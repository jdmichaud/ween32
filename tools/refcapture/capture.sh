#!/bin/bash
# Capture a reference render of the real win32 controls under Wine's classic
# (unthemed) look — the Windows 2000 look ween32 targets.
#
#   ./capture.sh          -> reference.png (the sampler window, cropped)
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

DESK_W=700 DESK_H=520   # wine virtual desktop

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
    "$repo/examples/controls.c" -luser32 -lgdi32 -lcomctl32 \
    -o "$here/controls.exe"

# A wine virtual desktop makes wine draw its own caption and frame instead of
# handing the window to the host window manager.
wine explorer "/desktop=ween32ref,${DESK_W}x${DESK_H}" "$here/controls.exe" \
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
  if [ -n "$best" ] && [ "$bestarea" -ge $((DESK_W * DESK_H)) ]; then
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

# Only wine's own desktop window is ever read — never the root window.
import -window "$id" "$here/desktop.png"
# The sampler is the only thing on that desktop, so trimming the uniform
# desktop background leaves exactly its window — no hardcoded geometry.
magick "$here/desktop.png" -fuzz 2% -trim +repage "$here/reference.png"
wineserver -k 2>/dev/null || true
echo "wrote $here/reference.png"
