import struct, sys
exe, want = sys.argv[1], int(sys.argv[2], 16)
data = open(exe, "rb").read()
ne_off = struct.unpack_from("<H", data, 0x3C)[0]
res_tab = ne_off + struct.unpack_from("<H", data, ne_off + 0x24)[0]
shift = struct.unpack_from("<H", data, res_tab)[0]
pos = res_tab + 2
while True:
    type_id = struct.unpack_from("<H", data, pos)[0]
    if type_id == 0: break
    count = struct.unpack_from("<H", data, pos + 2)[0]
    pos += 8
    for i in range(count):
        off, length, flags, rid = struct.unpack_from("<4H", data, pos)
        pos += 12
        if (type_id & 0x7FFF) == want:
            blob = data[off << shift:(off << shift) + (length << shift)]
            # Pascal-style or NUL-separated? Show printable runs >= 3 chars with offsets
            print(f"=== res 0x{rid & 0x7fff:04x} ({length << shift} bytes)")
            run, start = [], None
            for j, b in enumerate(blob):
                if 32 <= b < 127:
                    if start is None: start = j
                    run.append(chr(b))
                else:
                    if len(run) >= 3:
                        print(f"  +0x{start:04x}: {''.join(run)!r}")
                    run, start = [], None
            if len(run) >= 3: print(f"  +0x{start:04x}: {''.join(run)!r}")
