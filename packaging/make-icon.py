#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generates packaging/lvdsim-256.png: a dark rounded square with a digital waveform and a cyan bus trace.
Our own drawing, no third party art. Needs Pillow. Usage: python packaging/make-icon.py [out.png]"""
import sys
from PIL import Image, ImageDraw

S = 4  # supersampling
N = 256 * S
img = Image.new("RGBA", (N, N), (0, 0, 0, 0))
d = ImageDraw.Draw(img)
d.rounded_rectangle((8 * S, 8 * S, N - 8 * S, N - 8 * S), radius=44 * S, fill=(24, 28, 38, 255))
d.rounded_rectangle((8 * S, 8 * S, N - 8 * S, N - 8 * S), radius=44 * S, outline=(70, 80, 100, 255), width=3 * S)


def trace(pts, color, width):
    d.line([(x * S, y * S) for x, y in pts], fill=color, width=width * S, joint="curve")


# clock-like square wave (green)
y0, y1 = 96, 60
x = 36
pts = [(x, y0)]
for _ in range(5):
    pts += [(x, y1), (x + 20, y1), (x + 20, y0), (x + 40, y0)]
    x += 40
    pts = pts[:-1] + [(x, y0)]
trace(pts, (80, 220, 120, 255), 6)
# data bit (yellow)
trace([(36, 150), (100, 150), (100, 126), (160, 126), (160, 150), (220, 150)], (240, 200, 70, 255), 6)
# analog trace (cyan)
import math
pts = [(36 + i * 184 / 100, 200 - 26 * math.sin(i / 100 * 2 * math.pi * 1.5)) for i in range(101)]
trace(pts, (80, 200, 255, 255), 6)
img = img.resize((256, 256), Image.LANCZOS)
img.save(sys.argv[1] if len(sys.argv) > 1 else "packaging/lvdsim-256.png")
