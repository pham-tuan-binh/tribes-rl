#!/usr/bin/env python3
"""Pack web/assets sprites into one atlas (atlas.png + atlas.json).

One download instead of ~240: the site slices sprites back out with
createImageBitmap. Shelf packing, sorted by height. Run from web/:
    python3 pack_atlas.py
Requires Pillow; build.sh skips this step gracefully when it's missing
(the site falls back to per-file loading via manifest.json).
"""
import json
import os
import sys

from PIL import Image

ASSETS = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'assets')
WIDTH = 2048

sprites = []
for root, _, files in os.walk(ASSETS):
    for f in sorted(files):
        if not f.endswith('.png'):
            continue
        path = os.path.join(root, f)
        rel = os.path.relpath(path, ASSETS)
        img = Image.open(path).convert('RGBA')
        sprites.append((rel, img))

sprites.sort(key=lambda s: -s[1].height)

frames = {}
x = y = shelf_h = 0
PAD = 2  # bleed guard between neighbors
for rel, img in sprites:
    w, h = img.size
    if x + w > WIDTH:
        x, y = 0, y + shelf_h + PAD
        shelf_h = 0
    frames[rel] = [x, y, w, h]
    shelf_h = max(shelf_h, h)
    x += w + PAD

height = y + shelf_h
atlas = Image.new('RGBA', (WIDTH, height))
for rel, img in sprites:
    fx, fy, _, _ = frames[rel]
    atlas.paste(img, (fx, fy))

# WebP: one palette can't serve 244 diverse sprites (png quantization fails),
# but lossy WebP at q90 is visually clean and ~10x smaller than atlas RGBA png
atlas.save(os.path.join(ASSETS, 'atlas.webp'), quality=90, method=6)
with open(os.path.join(ASSETS, 'atlas.json'), 'w') as f:
    json.dump(frames, f)
size = os.path.getsize(os.path.join(ASSETS, 'atlas.webp'))
print(f'atlas.webp {WIDTH}x{height} ({size / 1e6:.1f} MB), {len(frames)} sprites')
