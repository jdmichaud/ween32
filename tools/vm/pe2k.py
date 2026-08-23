#!/usr/bin/env python3
"""Tell a PE it may run on Windows 2000.

Modern toolchains stamp 6.0 into the OS and subsystem version fields, and
NT 5.0 refuses anything newer with "not a valid Win32 application". Nothing
else about the binary needs to change.
"""
import struct
import sys

for path in sys.argv[1:]:
    d = bytearray(open(path, "rb").read())
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    opt = pe + 24
    struct.pack_into("<HH", d, opt + 40, 4, 0)  # OS version
    struct.pack_into("<HH", d, opt + 48, 4, 0)  # subsystem version
    open(path, "wb").write(d)
    print("%s: OS and subsystem version 4.0" % path)
