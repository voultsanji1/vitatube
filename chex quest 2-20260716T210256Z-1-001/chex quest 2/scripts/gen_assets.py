#!/usr/bin/env python3
"""Convert assets/ PNGs to 8-bit indexed (palette) PNGs for PS Vita sce_sys.

PS Vita firmware requires ALL sce_sys images to be 8-bit indexed PNG.
"""
from PIL import Image, PngImagePlugin
import os

# Arte ufficiale stile CQ2:
#   cq2_cover.png  — copertina verticale → icona bolla (128×128)
#   cq2_title.png  — schermata titolo orizzontale → bg0 (area carta LiveArea) + startup
#   cq2_banner.png — banner grande “esterno” (schede LiveArea) → pic0.png
SPECS = [
    ("assets/cq2_cover.png", "sce_sys/icon0.png",                      (128, 128)),
    ("assets/cq2_title.png", "sce_sys/livearea/contents/bg0.png",      (840, 500)),
    ("assets/cq2_title.png", "sce_sys/livearea/contents/startup.png", (280, 158)),
    ("assets/cq2_banner.png", "sce_sys/pic0.png",                    (960, 544)),
]

os.makedirs("sce_sys", exist_ok=True)
os.makedirs("sce_sys/livearea/contents", exist_ok=True)

for src, dst, size in SPECS:
    if not os.path.isfile(src):
        print("SKIP (not found): " + src)
        continue
    img = Image.open(src)
    if img.mode in ("RGBA", "LA", "PA"):
        bg = Image.new("RGB", img.size, (0, 0, 0))
        bg.paste(img, mask=img.split()[-1])
        img = bg
    else:
        img = img.convert("RGB")
    img = img.resize(size, Image.LANCZOS)
    img = img.quantize(colors=256, method=Image.Quantize.MEDIANCUT)
    pnginfo = PngImagePlugin.PngInfo()
    img.save(dst, "PNG", optimize=True, pnginfo=pnginfo)
    sz = os.path.getsize(dst)
    v = Image.open(dst)
    print("%s: %dx%d mode=%s  %d bytes" % (dst, v.size[0], v.size[1], v.mode, sz))

print("Done!")
