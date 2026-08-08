import struct, sys
exe = sys.argv[1]
data = open(exe, "rb").read()
ne_off = struct.unpack_from("<H", data, 0x3C)[0]
res_tab = ne_off + struct.unpack_from("<H", data, ne_off + 0x24)[0]
shift = struct.unpack_from("<H", data, res_tab)[0]
pos = res_tab + 2
rows = []
while True:
    type_id = struct.unpack_from("<H", data, pos)[0]
    if type_id == 0: break
    count = struct.unpack_from("<H", data, pos + 2)[0]
    pos += 8
    for i in range(count):
        off, length, flags, rid = struct.unpack_from("<4H", data, pos)
        pos += 12
        if (type_id & 0x7FFF) == 0x0002:  # RT_BITMAP
            blob_off = off << shift
            hsz = struct.unpack_from("<I", data, blob_off)[0]
            if hsz == 40:
                w, h = struct.unpack_from("<ii", data, blob_off + 4)
            elif hsz == 12:
                w, h = struct.unpack_from("<HH", data, blob_off + 4)
            else:
                w = h = -1
            rows.append((rid & 0x7FFF, w, h))
for rid, w, h in sorted(rows):
    tag = " <== STRIP?" if w >= 760 and 8 <= abs(h) <= 40 else ""
    print(f"0x{rid:04x}  {w:5d} x {h:4d}{tag}")
