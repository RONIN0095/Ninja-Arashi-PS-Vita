#!/usr/bin/env python3
"""Makes plain placeholder LiveArea images (no game art) in extras/livearea/.

The Vita needs 8-bit palette PNGs. Requires Pillow.

    python3 tools/make_livearea.py
"""
import os
from PIL import Image, ImageDraw, ImageFont

out = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "extras", "livearea")
BG = (18, 20, 30)
FG = (220, 60, 50)
TXT = (235, 235, 235)


def font(size):
    for f in ("DejaVuSans-Bold.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"):
        try:
            return ImageFont.truetype(f, size)
        except OSError:
            pass
    return ImageFont.load_default()


def make(name, w, h, title, sub=None):
    im = Image.new("RGB", (w, h), BG)
    d = ImageDraw.Draw(im)
    d.rectangle([0, h - max(4, h // 20), w, h], fill=FG)
    f = font(max(12, h // 6))
    tw = d.textlength(title, font=f)
    d.text(((w - tw) / 2, h / 2 - f.size), title, font=f, fill=TXT)
    if sub:
        f2 = font(max(10, h // 12))
        sw = d.textlength(sub, font=f2)
        d.text(((w - sw) / 2, h / 2 + f.size * 0.3), sub, font=f2, fill=FG)
    im.convert("P", palette=Image.ADAPTIVE, colors=64).save(os.path.join(out, name), optimize=True)


make("icon0.png", 128, 128, "NA", "Vita")
make("pic0.png", 960, 544, "Ninja Arashi", "PS Vita loader")
make("bg0.png", 840, 500, "Ninja Arashi", "PS Vita loader")
make("startup.png", 280, 158, "Ninja Arashi", "Start")
