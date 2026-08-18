"""Dump DTMP (custom resource type 0xFF04) dialog-layout blobs from SIMTOWER.EXE.

Format (decoded 2026-08-15, annotated/seg_1100_InfoDlgT.c:74):
  [sz caption][w:word][h:word][nitems:word][nitems x LE RECT(l,t,r,b), 1-based]
Empty rect = item hidden. Item index = dialog control id / pseudo-item id.

Usage: python3 ne_dump_dtmp.py SIMTOWER.EXE [first_id_hex last_id_hex]
"""
import struct, sys

exe = sys.argv[1]
lo = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0x0000
hi = int(sys.argv[3], 16) if len(sys.argv) > 3 else 0xFFFF
data = open(exe, "rb").read()
ne_off = struct.unpack_from("<H", data, 0x3C)[0]
res_tab = ne_off + struct.unpack_from("<H", data, ne_off + 0x24)[0]
shift = struct.unpack_from("<H", data, res_tab)[0]
pos = res_tab + 2
while True:
    type_id = struct.unpack_from("<H", data, pos)[0]
    if type_id == 0:
        break
    count = struct.unpack_from("<H", data, pos + 2)[0]
    pos += 8
    for _ in range(count):
        off, length, flags, rid = struct.unpack_from("<4H", data, pos)
        pos += 12
        rid &= 0x7FFF
        if (type_id & 0x7FFF) != 0x7F04 and (type_id & 0x7FFF) != 0xFF04 & 0x7FFF:
            continue
        if not (lo <= rid <= hi):
            continue
        blob = data[off << shift:(off << shift) + (length << shift)]
        n = blob[0]
        cap = blob[1:1 + n].decode("latin-1")
        p = 1 + n
        w, h, nitems = struct.unpack_from("<3H", blob, p)
        p += 6
        print(f"=== DTMP 0x{rid:04x} \"{cap}\" {w}x{h} items={nitems}")
        for i in range(nitems):
            l, t, r, b = struct.unpack_from("<4h", blob, p)
            p += 8
            if (l, t, r, b) != (0, 0, 0, 0):
                print(f"  item {i+1:2d}: ({l},{t})-({r},{b})  {r-l}x{b-t}")
