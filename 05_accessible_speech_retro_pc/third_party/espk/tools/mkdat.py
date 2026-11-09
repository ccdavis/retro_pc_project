#!/usr/bin/env python3
"""Pack the espeak-ng data files ESPK needs into one file.
usage: mkdat.py espeak-ng-data-dir out.dat
Format: "ESPKDAT1", u16 count, pad to 16; count entries of {char name[12]; u32 offset; u32 length};
data, each entry 16-byte aligned."""
import sys, struct, pathlib
src = pathlib.Path(sys.argv[1]); out = sys.argv[2]
names = ["phontab", "phonindex", "phondata", "intonations", "en_dict"]
blobs = [(n, (src / n).read_bytes()) for n in names]
hdr = b"ESPKDAT1" + struct.pack("<H", len(blobs)) + b"\0" * 6
off = 16 + 20 * len(blobs)
off = (off + 15) & ~15
table = b""; data = b""
for n, b in blobs:
    table += n.encode().ljust(12, b"\0") + struct.pack("<II", off + len(data), len(b))
    data += b + b"\0" * ((-len(b)) % 16)
table += b"\0" * (off - 16 - len(table))
pathlib.Path(out).write_bytes(hdr + table + data)
for n, b in blobs: print("%-12s %7d" % (n, len(b)))
print("%-12s %7d" % ("total", len(hdr) + len(table) + len(data)))
