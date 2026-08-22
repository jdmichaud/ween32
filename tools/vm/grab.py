#!/usr/bin/env python3
"""Read the Windows 2000 VM's screen out of shared memory, as PNG.

The jslinux MCP mirrors the guest frame buffer into a file on tmpfs so that
anything can read the screen without going through the socket. That is what
this uses: pixel-perfect work needs the actual bytes, not a picture pasted
into a conversation.

    tools/vm/grab.py out.png                  # the whole 1024x768 screen
    tools/vm/grab.py out.png 133 136 654 536  # a window off it

Layout: a 64-byte header — magic, width, height, stride, flags, sequence —
then height rows of stride bytes, RGBA.
"""
import os
import struct
import sys

FB = os.environ.get("JSLINUX_SHM", "/dev/shm/jslinux-mcp.fb")


def read():
    with open(FB, "rb") as f:
        head = f.read(64)
        w, h, stride = struct.unpack("<3I", head[8:20])
        seq = struct.unpack("<I", head[24:28])[0]
        return w, h, stride, seq, f.read(stride * h)


def main():
    from PIL import Image
    w, h, stride, seq, px = read()
    im = Image.frombytes("RGBA", (w, h), px, "raw", "RGBA", stride).convert("RGB")
    out = sys.argv[1] if len(sys.argv) > 1 else "/tmp/vm.png"
    if len(sys.argv) > 5:
        x, y, cw, ch = (int(a) for a in sys.argv[2:6])
        im = im.crop((x, y, x + cw, y + ch))
    im.save(out)
    print("%s %dx%d (frame %d)" % (out, im.width, im.height, seq))


main()
