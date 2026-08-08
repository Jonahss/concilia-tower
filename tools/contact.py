import struct, sys, subprocess, os
from PIL import Image, ImageDraw
S = os.path.dirname(os.path.abspath(__file__))
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
        if (type_id & 0x7FFF) == 0x0002:
            blob_off = off << shift
            hsz = struct.unpack_from("<I", data, blob_off)[0]
            if hsz == 40:
                w, h = struct.unpack_from("<ii", data, blob_off + 4)
                rows.append((rid & 0x7FFF, w, abs(h)))
picks = [(r,w,h) for r,w,h in sorted(rows) if h <= 40 and w >= 24]
tiles = []
for rid, w, h in picks:
    out = f"{S}/cs_{rid:04x}.bmp"
    if not os.path.exists(out):
        r = subprocess.run(["env","SDL_VIDEODRIVER=dummy",f"{S}/dump_sprite",
            os.path.expanduser("~/.claude-agent/archive/openclaw/workspace/projects/OpenSkyscraper/data/SIMTOWER.EXE"),
            hex(0x8000+rid), out], capture_output=True)
        if r.returncode != 0: continue
    try: im = Image.open(out)
    except: continue
    im = im.resize((im.width//4, im.height//4))  # back to 1x
    tiles.append((rid, im))
W = 700
sheet_h = sum(im.height + 14 for _, im in tiles) + 10
sheet = Image.new("RGB", (W, sheet_h), (30,30,30))
d = ImageDraw.Draw(sheet)
y = 5
for rid, im in tiles:
    d.text((4, y), f"0x{rid:04x} {im.width}x{im.height}", fill=(255,255,0))
    sheet.paste(im.crop((0,0,min(im.width, W-90),im.height)), (88, y))
    y += im.height + 14
sheet.save(f"{S}/contact_sheet.png")
print(len(tiles), "tiles ->", f"{S}/contact_sheet.png", sheet.size)
