#!/usr/bin/env python3
"""Draws icon.jpg (256x256, the size the Switch home menu wants). Original artwork: an ember flame
over three stat bars. Run: python3 -I tools/make_icon.py"""
import math, os
from PIL import Image, ImageDraw, ImageFilter
S, K = 256, 4; N = S * K
here = os.path.dirname(os.path.abspath(__file__))
img = Image.new("RGB", (N, N))
d = ImageDraw.Draw(img)
for y in range(N):  # dark vertical gradient, warmer at the bottom
    t = y / N
    d.line([(0, y), (N, y)], fill=(int(18 + 40 * t**2), int(14 + 14 * t**2), int(20 - 6 * t)))
glow = Image.new("RGB", (N, N), (0, 0, 0)); g = ImageDraw.Draw(glow)
cx, cy = N * 0.5, N * 0.40
for r, c in ((N*0.42, (60, 20, 0)), (N*0.32, (110, 40, 0)), (N*0.22, (170, 70, 5))):
    g.ellipse([cx - r, cy - r, cx + r, cy + r], fill=c)
glow = glow.filter(ImageFilter.GaussianBlur(N * 0.08))
from PIL import ImageChops
img = ImageChops.add(img, glow)
d = ImageDraw.Draw(img)

def flame(cx, base, h, w, col):
    pts = []
    for i in range(0, 101):  # outline: teardrop with a wavering tip
        t = i / 100.0
        a = t * math.tau
        x = math.sin(a) * w * (1 - 0.18 * math.cos(a))
        yy = -math.cos(a)
        y = yy * h * 0.5 * (1.15 if yy > 0 else 1.0)
        y2 = base - h * 0.5 - y if False else base - h * 0.5 + (-y)
        pts.append((cx + x * (0.55 + 0.45 * (1 - (yy + 1) / 2) ** 0.7) + 0.06 * w * math.sin(yy * 5), y2))
    d.polygon(pts, fill=col)
fb = N * 0.60
flame(cx, fb, N * 0.50, N * 0.20, (214, 72, 10))
flame(cx + N*0.01, fb, N * 0.38, N * 0.14, (255, 150, 30))
flame(cx + N*0.01, fb, N * 0.22, N * 0.085, (255, 226, 130))
# three stat bars (what the editor edits)
bx0, bx1 = N * 0.20, N * 0.80
for i, fill in enumerate((0.85, 0.55, 0.70)):
    y = N * (0.70 + i * 0.075); hgt = N * 0.032
    d.rounded_rectangle([bx0, y, bx1, y + hgt], radius=hgt / 2, fill=(46, 40, 52))
    d.rounded_rectangle([bx0, y, bx0 + (bx1 - bx0) * fill, y + hgt], radius=hgt / 2, fill=(232, 150, 40))
# thin frame
d.rounded_rectangle([N*0.02, N*0.02, N*0.98, N*0.98], radius=N*0.06, outline=(120, 80, 40), width=int(N*0.008))
img.resize((S, S), Image.LANCZOS).save(os.path.join(here, "..", "icon.jpg"), quality=92)
print("wrote icon.jpg")
