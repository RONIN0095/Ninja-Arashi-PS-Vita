#!/usr/bin/env python3
"""
Draws the LiveArea pictures (extras/livearea) from scratch: an ink-silhouette
scene in the game's style (no artwork taken from the game).

  icon0.png    128x128  bubble
  bg0.png      840x500  LiveArea background (no title: it is on the gate)
  startup.png  280x158  gate with the title
  pic0.png     960x544  shown while the app starts

Fonts (SIL Open Font License, from github.com/google/fonts):
  Shojumaru-Regular.ttf, NotoSerifJP[wght].ttf
Usage: tools/livearea.py <font dir> <out dir>
"""
import math
import random
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont

FONTS, OUT = sys.argv[1], sys.argv[2]
SS = 3  # supersampling

INK = (8, 10, 14)
RED = (196, 32, 38)


def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(len(a)))


def sky(w, h, top, mid, bottom, mid_at=0.62):
    im = Image.new("RGB", (w, h))
    px = im.load()
    for y in range(h):
        t = y / (h - 1)
        c = lerp(top, mid, t / mid_at) if t < mid_at else lerp(mid, bottom, (t - mid_at) / (1 - mid_at))
        for x in range(w):
            px[x, y] = c
    return im


def moon(im, cx, cy, r, color=(242, 226, 205)):
    # soft glow: one wide blurred disc (no visible rings), then the moon
    glow = Image.new("RGBA", im.size, (0, 0, 0, 0))
    g = ImageDraw.Draw(glow)
    rr = r * 2.2
    g.ellipse([cx - rr, cy - rr, cx + rr, cy + rr], fill=(255, 200, 170, 70))
    glow = glow.filter(ImageFilter.GaussianBlur(r * 0.9))
    im.alpha_composite(glow)
    d = ImageDraw.Draw(im)
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=color + (255,))
    # faint craters
    rnd = random.Random(5)
    for _ in range(6):
        a, dist, cr = rnd.uniform(0, 6.28), rnd.uniform(0, r * 0.6), rnd.uniform(r * 0.08, r * 0.18)
        x, y = cx + math.cos(a) * dist, cy + math.sin(a) * dist
        d.ellipse([x - cr, y - cr, x + cr, y + cr], fill=(228, 208, 186, 255))


def ridge(w, base, amp, seed, rough=6):
    rnd = random.Random(seed)
    pts = []
    phase = [rnd.random() * 6.28 for _ in range(rough)]
    for x in range(0, w + 8, 4):
        y = base
        for k in range(rough):
            f = (k + 1) * 1.7 / w * 3.14
            y -= amp / (k + 1.2) * math.sin(x * f + phase[k])
        pts.append((x, y))
    return pts


def mountains(im, layers):
    w, h = im.size
    for base, amp, seed, color, alpha in layers:
        lay = Image.new("RGBA", im.size, (0, 0, 0, 0))
        d = ImageDraw.Draw(lay)
        pts = ridge(w, base, amp, seed)
        d.polygon(pts + [(w, h), (0, h)], fill=color + (alpha,))
        im.alpha_composite(lay)
        # fog above the next layer
        fog = Image.new("RGBA", im.size, (0, 0, 0, 0))
        fd = ImageDraw.Draw(fog)
        fd.rectangle([0, base - amp * 0.2, w, base + amp * 0.6], fill=(255, 235, 225, 22))
        im.alpha_composite(fog.filter(ImageFilter.GaussianBlur(amp * 0.5)))


def bamboo(d, x, top, bottom, width, color, seed):
    rnd = random.Random(seed)
    d.rectangle([x, top, x + width, bottom], fill=color)
    y = bottom
    while y > top:
        y -= rnd.uniform(width * 7, width * 10)
        d.rectangle([x - width * 0.25, y, x + width * 1.25, y + width * 0.35], fill=color)
        if rnd.random() < 0.55:  # leaves
            side = rnd.choice((-1, 1))
            for k in range(rnd.randint(2, 4)):
                lx = x + width / 2
                ang = side * rnd.uniform(0.3, 1.1) + (0 if side > 0 else math.pi)
                ln = rnd.uniform(width * 4, width * 7)
                ex, ey = lx + math.cos(ang) * ln, y + math.sin(ang) * ln * 0.35 + k * width * 0.6
                mx, my = (lx + ex) / 2, (y + ey) / 2
                nx, ny = -(ey - y) / ln * width * 0.8, (ex - lx) / ln * width * 0.8
                d.polygon([(lx, y), (mx + nx, my + ny), (ex, ey), (mx - nx, my - ny)], fill=color)


def ninja(d, x, y, s, color=INK, scarf=RED):
    """Standing ninja with a straw hat and a sword on the back; (x, y) = feet, s = height."""
    P = lambda pts: [(x + px * s, y - py * s) for px, py in pts]
    # scarf flowing back
    d.polygon(P([(-0.03, 0.80), (-0.42, 0.88), (-0.70, 0.80), (-0.55, 0.84), (-0.36, 0.78), (-0.02, 0.74)]), fill=scarf)
    d.polygon(P([(-0.04, 0.76), (-0.38, 0.72), (-0.58, 0.62), (-0.40, 0.70), (-0.05, 0.71)]), fill=scarf)
    # sword on the back
    d.polygon(P([(0.04, 0.70), (-0.30, 1.06), (-0.27, 1.08), (0.08, 0.72)]), fill=color)
    d.polygon(P([(-0.24, 0.98), (-0.34, 0.94), (-0.33, 0.96), (-0.23, 1.00)]), fill=color)
    # legs and cloak
    d.polygon(P([(-0.13, 0.00), (-0.05, 0.00), (0.00, 0.30), (0.05, 0.00), (0.14, 0.00),
                 (0.13, 0.36), (0.20, 0.42), (0.17, 0.66), (0.10, 0.80), (-0.10, 0.80),
                 (-0.17, 0.66), (-0.22, 0.42), (-0.13, 0.36)]), fill=color)
    # arm down to a short blade
    d.polygon(P([(0.10, 0.74), (0.24, 0.52), (0.30, 0.40), (0.26, 0.38), (0.18, 0.50), (0.08, 0.66)]), fill=color)
    d.polygon(P([(0.27, 0.40), (0.52, 0.06), (0.54, 0.07), (0.30, 0.42)]), fill=color)
    # head and straw hat (kasa)
    d.ellipse([x - 0.075 * s, y - 0.93 * s, x + 0.075 * s, y - 0.78 * s], fill=color)
    d.polygon(P([(-0.30, 0.90), (0.00, 1.04), (0.30, 0.90), (0.20, 0.885), (-0.20, 0.885)]), fill=color)
    # eyes
    d.polygon(P([(-0.045, 0.855), (-0.012, 0.85), (-0.015, 0.842), (-0.045, 0.846)]), fill=scarf)
    d.polygon(P([(0.012, 0.85), (0.045, 0.855), (0.045, 0.846), (0.015, 0.842)]), fill=scarf)


def cliff(d, w, h, x0, top, color=INK):
    rnd = random.Random(7)
    pts = [(x0, h)]
    x = x0
    y = top + 30
    while x < w + 20:
        pts.append((x, y))
        x += rnd.uniform(10, 30)
        y = top + rnd.uniform(-6, 10)
    pts.append((w + 20, h))
    d.polygon(pts, fill=color)
    for gx in range(int(x0), w, 7):  # grass
        gy = top + rnd.uniform(-4, 6)
        d.polygon([(gx, gy + 6), (gx + rnd.uniform(-4, 4), gy - rnd.uniform(6, 16)), (gx + 3, gy + 6)], fill=color)


def petals(im, n, seed, area):
    rnd = random.Random(seed)
    lay = Image.new("RGBA", im.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(lay)
    x0, y0, x1, y1 = area
    for _ in range(n):
        x, y = rnd.uniform(x0, x1), rnd.uniform(y0, y1)
        r = rnd.uniform(2, 5) * SS
        a = rnd.uniform(0, 3.14)
        pts = [(x + math.cos(a + k * 2.1) * r * (1.0 if k % 2 == 0 else 0.45),
                y + math.sin(a + k * 2.1) * r * (1.0 if k % 2 == 0 else 0.45)) for k in range(3)]
        d.polygon(pts, fill=(232, 70, 70, rnd.randint(120, 220)))
    im.alpha_composite(lay)


def scene(w, h, moon_at, ninja_at, ninja_h, cliff_at, bamboo_left=True, bamboo_right=False):
    W, H = w * SS, h * SS
    im = sky(W, H, (24, 20, 38), (120, 40, 44), (226, 118, 76)).convert("RGBA")
    moon(im, moon_at[0] * SS, moon_at[1] * SS, moon_at[2] * SS)
    mountains(im, [
        (H * 0.62, H * 0.16, 3, (92, 40, 52), 255),
        (H * 0.72, H * 0.13, 5, (58, 26, 38), 255),
        (H * 0.83, H * 0.10, 9, (30, 16, 24), 255),
    ])
    lay = Image.new("RGBA", im.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(lay)
    if bamboo_left:
        for i, bx in enumerate((0.02, 0.07, 0.115, 0.16)):
            bamboo(d, bx * W, -10, H, (5 - i * 0.6) * SS, INK, 11 + i)
    if bamboo_right:
        for i, bx in enumerate((0.97, 0.93)):
            bamboo(d, bx * W, -10, H, (4.5 - i * 0.8) * SS, INK, 31 + i)
    cliff(d, W, H, cliff_at[0] * SS, cliff_at[1] * SS)
    ninja(d, ninja_at[0] * SS, ninja_at[1] * SS, ninja_h * SS)
    im.alpha_composite(lay)
    petals(im, int(w * h / 4000), 21, (0, 0, W, H * 0.8))
    # vignette
    vig = Image.new("L", im.size, 0)
    vd = ImageDraw.Draw(vig)
    vd.ellipse([-W * 0.25, -H * 0.35, W * 1.25, H * 1.35], fill=255)
    vig = vig.filter(ImageFilter.GaussianBlur(W * 0.08))
    dark = Image.new("RGBA", im.size, (0, 0, 0, 170))
    im = Image.composite(im, Image.alpha_composite(im, dark), vig)
    return im


def title(im, cx, cy, size, sub=True):
    """Title in brush-like letters, with 嵐 (arashi, storm) in a red seal."""
    d = ImageDraw.Draw(im)
    f = ImageFont.truetype(f"{FONTS}/Shojumaru-Regular.ttf", size)
    text = "NINJA ARASHI"
    tw = d.textlength(text, font=f)
    # shadow
    sh = Image.new("RGBA", im.size, (0, 0, 0, 0))
    ImageDraw.Draw(sh).text((cx - tw / 2 + size * 0.05, cy + size * 0.06), text, font=f, fill=(0, 0, 0, 200), anchor="lm")
    im.alpha_composite(sh.filter(ImageFilter.GaussianBlur(size * 0.08)))
    d.text((cx - tw / 2, cy), text, font=f, fill=(246, 240, 230), anchor="lm", stroke_width=max(1, size // 22),
           stroke_fill=INK)
    if sub:
        # red seal with the kanji
        s = size * 0.95
        sx, sy = cx + tw / 2 + size * 0.25, cy - s / 2
        d.rounded_rectangle([sx, sy, sx + s, sy + s], radius=s * 0.12, fill=RED)
        jf = ImageFont.truetype(f"{FONTS}/NotoSerifJP[wght].ttf", int(s * 0.8))
        try:
            jf.set_variation_by_axes([900])
        except Exception:
            pass
        d.text((sx + s / 2, sy + s / 2), "嵐", font=jf, fill=(250, 238, 228), anchor="mm")


def save(im, w, h, name, colors=None):
    im = im.convert("RGB").resize((w, h), Image.LANCZOS)
    if colors:  # LiveArea pictures must be 8-bit indexed PNGs
        im = im.quantize(colors=colors, method=Image.MEDIANCUT, dither=Image.FLOYDSTEINBERG)
    im.save(f"{OUT}/{name}", optimize=True)


# LiveArea background: art only; the title is on the gate (startup.png)
bg = scene(840, 500, (600, 120, 70), (690, 395), 150, (560, 395), bamboo_left=True)
save(bg, 840, 500, "bg0.png", 256)

# gate: dark band with the title
g = scene(280, 158, (215, 52, 30), (236, 132), 60, (190, 132), bamboo_left=False)
dark = Image.new("RGBA", g.size, (6, 6, 10, 110))
g = Image.alpha_composite(g, dark)
title(g, 280 * SS * 0.43, 158 * SS * 0.52, 23 * SS)
save(g, 280, 158, "startup.png", 256)

# boot picture: art with the title once
p = scene(960, 544, (700, 130, 80), (790, 430), 170, (650, 430), bamboo_left=True)
title(p, 960 * SS * 0.36, 544 * SS * 0.22, 54 * SS)
save(p, 960, 544, "pic0.png", 256)

# icon: ninja in front of the moon
W = 128 * SS
ic = sky(W, W, (30, 18, 34), (140, 42, 44), (210, 96, 66), 0.6).convert("RGBA")
moon(ic, W * 0.5, W * 0.42, W * 0.30)
lay = Image.new("RGBA", ic.size, (0, 0, 0, 0))
d = ImageDraw.Draw(lay)
d.rectangle([0, W * 0.86, W, W], fill=INK)
ninja(d, W * 0.5, W * 0.88, W * 0.70)
ic.alpha_composite(lay)
save(ic, 128, 128, "icon0.png", 256)
