import struct, sys
exe = sys.argv[1]
data = open(exe, "rb").read()
ne_off = struct.unpack_from("<H", data, 0x3C)[0]
res_tab = ne_off + struct.unpack_from("<H", data, ne_off + 0x24)[0]
shift = struct.unpack_from("<H", data, res_tab)[0]
pos = res_tab + 2
while True:
    type_id = struct.unpack_from("<H", data, pos)[0]
    if type_id == 0: break
    count = struct.unpack_from("<H", data, pos + 2)[0]
    ids = []
    p2 = pos + 8
    for i in range(count):
        off, length, flags, rid = struct.unpack_from("<4H", data, p2)
        ids.append((rid & 0x7FFF, length << shift))
        p2 += 12
    print(f"type 0x{type_id & 0x7FFF:04x}: {count} resources, ids: {[(hex(i), s) for i, s in ids[:8]]}{'...' if count > 8 else ''}")
    pos = p2
