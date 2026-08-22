#!/usr/bin/env python3
"""Give wine's Tahoma the advances GDI draws with, for the win32 comparison.

Tahoma carries embedded bitmap strikes — 8, 9, 10, 11, 12, 13, 15 and 16
pixels — and at eight points GDI draws the 11-pixel one.  So does ween32,
which rasterises the same strike.  Wine draws it too, and then measures
something else: it scales and hints the outline instead, and comes out a tenth
wider.  Every layout an application works out from a measurement inherits
that, so the same source lays its bars out differently there.

This takes the hinting away and writes each glyph's advance so that wine's
scaling arrives at the strike's number.  It is the environment being repaired,
not the application: what wine draws does not change, only what it says about
it.  Use a prefix of its own — the stored wine references were captured with
the stock font.

    tools/winecmp/gdi_advances.py fonts/tahoma.ttf out.ttf units.txt 11

`units.txt` is what tools/winecmp/fit.py works out: one line per character,
the advance in font units at which wine reports the pixel width ween32 draws.
"""

import struct, sys

src, dst, advfile, ppem = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4])
d = bytearray(open(src, 'rb').read())
ntab = struct.unpack('>H', d[4:6])[0]
tabs = {}
for i in range(ntab):
    o = 12 + 16 * i
    tag = bytes(d[o:o+4])
    csum, off, ln = struct.unpack('>III', d[o+4:o+16])
    tabs[tag] = (csum, off, ln, o)

upem = struct.unpack('>H', d[tabs[b'head'][1]+18:tabs[b'head'][1]+20])[0]
hhea = tabs[b'hhea'][1]
nhm = struct.unpack('>H', d[hhea+34:hhea+36])[0]
hmtx = tabs[b'hmtx'][1]

# cmap (format 4) so we can map the characters we have advances for
cm = tabs[b'cmap'][1]
n = struct.unpack('>H', d[cm+2:cm+4])[0]
sub = None
for i in range(n):
    pid, eid, off = struct.unpack('>HHI', d[cm+4+8*i:cm+12+8*i])
    if (pid, eid) in ((3, 1), (3, 0), (0, 3)):
        sub = cm + off
segX2 = struct.unpack('>H', d[sub+6:sub+8])[0]
seg = segX2 // 2
ends = [struct.unpack('>H', d[sub+14+2*i:sub+16+2*i])[0] for i in range(seg)]
sb = sub + 16 + segX2
starts = [struct.unpack('>H', d[sb+2*i:sb+2+2*i])[0] for i in range(seg)]
db = sb + segX2
deltas = [struct.unpack('>h', d[db+2*i:db+2+2*i])[0] for i in range(seg)]
rb = db + segX2
ranges = [struct.unpack('>H', d[rb+2*i:rb+2+2*i])[0] for i in range(seg)]

def gid(c):
    for i in range(seg):
        if starts[i] <= c <= ends[i]:
            if ranges[i] == 0:
                return (c + deltas[i]) & 0xffff
            a = rb + 2*i + ranges[i] + 2*(c - starts[i])
            g = struct.unpack('>H', d[a:a+2])[0]
            return (g + deltas[i]) & 0xffff if g else 0
    return 0

want = {}
for line in open(advfile):
    c, a = line.split()
    g = gid(int(c))
    if g:
        want[g] = int(a)

changed = 0
for g, units in want.items():   # the file gives units outright now
    if g >= nhm:
        continue
    struct.pack_into('>H', d, hmtx + 4*g, max(0, min(0xffff, units)))
    changed += 1

# and no hinting, so nothing moves the advances afterwards
drop = {b'fpgm', b'prep', b'cvt '}
keep = sorted(t for t in tabs if t not in drop)

def csum_of(b):
    b = bytes(b) + b'\0' * (-len(b) % 4)
    return sum(struct.unpack('>%dI' % (len(b)//4), b)) & 0xffffffff

nk = len(keep)
sr = 1
while sr * 2 <= nk:
    sr *= 2
out = bytearray(struct.pack('>IHHHH', 0x00010000, nk, sr*16,
                            sr.bit_length()-1, nk*16 - sr*16))
body = bytearray()
off0 = 12 + 16*nk
recs = []
for t in keep:
    _, off, ln, _ = tabs[t]
    data = bytes(d[off:off+ln])
    if t == b'head':
        data = data[:8] + b'\0\0\0\0' + data[12:]
    recs.append((t, off0 + len(body), len(data)))
    body += data + b'\0' * (-len(data) % 4)
for t, off, ln in recs:
    out += t + struct.pack('>III', csum_of(body[off-off0:off-off0+ln]), off, ln)
out += body
open(dst, 'wb').write(bytes(out))
print('patched %d advances, dropped hinting, wrote %s' % (changed, dst))
