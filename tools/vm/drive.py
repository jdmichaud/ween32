#!/usr/bin/env python3
"""Drive the Windows 2000 VM from a shell, including the drag the MCP cannot do.

The MCP tools click and type, but a paint program is mostly *drag*, and the
guest mouse is relative: a press has to stay down while the pointer walks to
its target in steps small enough that the guest's own acceleration stays 1:1.
That is one socket conversation with the VM daemon, so it lives here rather
than in a chain of separate calls.

    tools/vm/drive.py click 100,50
    tools/vm/drive.py drag 60,80 200,240          # press, walk, release
    tools/vm/drive.py drag 60,80 200,240 260,300  # more points: a polyline
    tools/vm/drive.py press 60,80 holdmove 120,140 shot /tmp/mid.png release
    tools/vm/drive.py key Enter  key KeyO:AltLeft  type "hello"
    tools/vm/drive.py wait 800  park  shot /tmp/s.png
    tools/vm/drive.py shot /tmp/win.png 132,132,654,544   # a crop of it

Commands run left to right in one connection, so a whole gesture is one
invocation. --sock and --shm pick the VM; they default to the mspaint one.
"""
import os
import json
import socket
import struct
import sys
import time

SOCK = os.environ.get("JSLINUX_SOCK", "/tmp/jslinux-paint.sock")
SHM = os.environ.get("JSLINUX_SHM", "/dev/shm/jslinux-paint.fb")


class VM:
    def __init__(self, sock):
        self.s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.s.connect(sock)
        self.f = self.s.makefile("rwb")
        self.id = 0

    def call(self, cmd, **kw):
        self.id += 1
        kw["cmd"] = cmd
        kw["id"] = self.id
        self.f.write((json.dumps(kw) + "\n").encode())
        self.f.flush()
        res = json.loads(self.f.readline())
        if not res.get("ok"):
            raise RuntimeError("%s: %s" % (cmd, res.get("error")))
        return res

    # The pointer is walked, never jumped: the guest sees PS/2 deltas and
    # applies acceleration to anything bigger than a few pixels.
    def walk(self, x, y, buttons=0, step=4):
        cx, cy = self.pos
        while (cx, cy) != (x, y):
            cx += max(-step, min(step, x - cx))
            cy += max(-step, min(step, y - cy))
            self.call("mouse", x=cx, y=cy, buttons=buttons)
            time.sleep(0.002)
        self.pos = (x, y)

    def park(self, buttons=0):
        """Pin the pointer in the bottom-right corner. One huge delta, so it
        jumps rather than walking: nothing on the way is hovered, which
        matters because a walk across an open menu changes which one is
        open, and the pointer is in every screenshot."""
        self.call("mouse", x=4000, y=4000, buttons=buttons)
        self.pos = (4000, 4000)
        time.sleep(0.03)

    def home(self, buttons=0):
        """Pin the pointer at the top-left corner, which is the only position
        a relative mouse can be *sure* of."""
        self.call("mouse", x=4000, y=4000, buttons=buttons)
        self.call("mouse", x=0, y=0, buttons=buttons)
        self.pos = (0, 0)
        time.sleep(0.03)


def point(a):
    x, y = a.split(",")
    return int(x), int(y)


def grab(path, crop=None):
    from PIL import Image

    with open(SHM, "rb") as f:
        head = f.read(64)
        w, h, stride = struct.unpack("<3I", head[8:20])
        seq = struct.unpack("<I", head[24:28])[0]
        px = f.read(stride * h)
    im = Image.frombytes("RGBA", (w, h), px, "raw", "RGBA", stride).convert("RGB")
    if crop:
        x, y, cw, ch = crop
        im = im.crop((x, y, x + cw, y + ch))
    im.save(path)
    print("%s %dx%d (frame %d)" % (path, im.width, im.height, seq))


def main(argv):
    global SOCK, SHM
    args = []
    for a in argv:
        if a.startswith("--sock="):
            SOCK = a[7:]
        elif a.startswith("--shm="):
            SHM = a[6:]
        else:
            args.append(a)

    vm = VM(SOCK)
    vm.pos = (0, 0)
    vm.home()

    i = 0
    while i < len(args):
        c = args[i]
        i += 1
        if c == "move":
            vm.home()
            vm.walk(*point(args[i]))
            i += 1
        elif c in ("click", "rclick", "dblclick"):
            vm.home()
            vm.walk(*point(args[i]))
            i += 1
            b = 2 if c == "rclick" else 1
            for _ in range(2 if c == "dblclick" else 1):
                vm.call("mouse", x=vm.pos[0], y=vm.pos[1], buttons=b)
                time.sleep(0.06)
                vm.call("mouse", x=vm.pos[0], y=vm.pos[1], buttons=0)
                time.sleep(0.04)
        elif c in ("drag", "rdrag"):
            b = 2 if c == "rdrag" else 1
            pts = []
            while i < len(args) and "," in args[i]:
                pts.append(point(args[i]))
                i += 1
            vm.home()
            vm.walk(*pts[0])
            time.sleep(0.08)
            vm.call("mouse", x=pts[0][0], y=pts[0][1], buttons=b)
            time.sleep(0.08)
            for p in pts[1:]:
                vm.walk(p[0], p[1], buttons=b)
                time.sleep(0.05)
            vm.call("mouse", x=vm.pos[0], y=vm.pos[1], buttons=0)
            time.sleep(0.08)
        elif c in ("press", "holdmove", "release"):
            # A gesture taken apart, so a screenshot can be taken in the
            # middle of it: what a tool draws while the button is down is
            # only on the screen while it is down.
            if c == "press":
                vm.home()
                vm.walk(*point(args[i]))
                i += 1
                vm.call("mouse", x=vm.pos[0], y=vm.pos[1], buttons=1)
                time.sleep(0.08)
            elif c == "holdmove":
                vm.walk(*point(args[i]), buttons=1)
                i += 1
                time.sleep(0.08)
            else:
                vm.call("mouse", x=vm.pos[0], y=vm.pos[1], buttons=0)
                time.sleep(0.08)
        elif c == "key":
            spec = args[i]
            i += 1
            name, _, mods = spec.partition(":")
            mods = [m for m in mods.split("+") if m]
            for m in mods:
                vm.call("key", name=m, down=True)
            vm.call("tap", name=name)
            for m in reversed(mods):
                vm.call("key", name=m, down=False)
        elif c == "type":
            vm.call("type", text=args[i])
            i += 1
        elif c == "wait":
            vm.call("waitStable", quiet=int(args[i]), timeout=30000)
            i += 1
        elif c == "sleep":
            time.sleep(int(args[i]) / 1000.0)
            i += 1
        elif c == "park":
            vm.park()
        elif c == "shot":
            path = args[i]
            i += 1
            crop = None
            if i < len(args) and args[i].count(",") == 3:
                crop = [int(v) for v in args[i].split(",")]
                i += 1
            grab(path, crop)
        elif c == "info":
            print(json.dumps(vm.call("info")))
        else:
            raise SystemExit("unknown command: " + c)


main(sys.argv[1:])
