#!/usr/bin/env python3
"""SLASH'EM's own 16x16 tiles (win/share/monsters.txt, objects.txt,
other.txt, the order util/tilemap numbers them) -> one PNG sheet, 40 per
row.  Index = position in that sequence = src/tile.c glyph2tile[]."""
import re, sys
from PIL import Image

PER_ROW = 40
files = ['monsters.txt', 'objects.txt', 'other.txt']
tiles = []
for name in files:
    pal, rows = {}, None
    for line in open(sys.argv[1] + '/' + name, encoding='latin-1'):
        m = re.match(r'^(\S) = \((\d+), (\d+), (\d+)\)', line)
        if m:
            pal[m.group(1)] = tuple(int(m.group(i)) for i in (2, 3, 4))
            continue
        if line.startswith('{'):
            rows = []
        elif line.startswith('}'):
            assert len(rows) == 16, (name, len(tiles))
            tiles.append([[pal[c] for c in r] for r in rows])
            rows = None
        elif rows is not None:
            r = line.strip()
            assert len(r) == 16, (name, len(tiles), r)
            rows.append(r)
n = len(tiles)
img = Image.new('RGB', (PER_ROW * 16, -(-n // PER_ROW) * 16))
px = img.load()
for i, t in enumerate(tiles):
    x0, y0 = (i % PER_ROW) * 16, (i // PER_ROW) * 16
    for y in range(16):
        for x in range(16):
            px[x0 + x, y0 + y] = t[y][x]
img.save(sys.argv[2])
print(n, 'tiles ->', sys.argv[2])
