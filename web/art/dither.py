import sys
import numpy as np
from PIL import Image

src, dst = sys.argv[1], sys.argv[2]
ncolors = int(sys.argv[3]) if len(sys.argv) > 3 else 56
strength = float(sys.argv[4]) if len(sys.argv) > 4 else 14.0

im = Image.open(src).convert('RGB')
a = np.asarray(im).astype(np.float32)

# 4x4 Bayer threshold matrix, centered on 0
bayer = np.array([[ 0, 8, 2,10],
                  [12, 4,14, 6],
                  [ 3,11, 1, 9],
                  [15, 7,13, 5]], dtype=np.float32) / 16.0 - 0.46875
h, w, _ = a.shape
tile = np.tile(bayer, (h // 4 + 1, w // 4 + 1))[:h, :w]
a = a + tile[:, :, None] * strength
a = np.clip(a, 0, 255).astype(np.uint8)

noisy = Image.fromarray(a)
# adaptive palette from the ORIGINAL (stable colors), applied without
# error diffusion so the Bayer pattern is what carries the gradients
pal = im.quantize(colors=ncolors, method=Image.MEDIANCUT)
out = noisy.quantize(colors=ncolors, palette=pal, dither=Image.Dither.NONE)
out.convert('RGB').save(dst)
print(dst, 'colors:', len(out.getcolors(256) or []), 'size:', out.size)
