"""Generate the game's indexed PNGs (gfx4snes input).

  blocks_a.png  BG palette 2: blank, blocks and landing outlines of pieces 0-3, empty cell, flash
  blocks_b.png  BG palette 3: blank, blocks and outlines of pieces 4-6, junk block
  walls.png     BG palette 4: cubicle partitions between the wells, their caps, the floor
  font.png      96 ASCII glyphs (32..127) with a drop shadow, 4 colors (2bpp)
  p1..p4.png    Woka sheets, 8 frames of 32x32 (all in VRAM at once): 2/3 size standing facing down, left,
                right, up; full size walking facing the camera (step, stand, step); 2/3 size step facing down.

Each PNG is 16 tiles of 8x8 in a row; tile order must match the T_* constants in src/main.c.

Usage: mkgfx.py <assets dir> <output dir>
"""
import os
import sys
from PIL import Image

ASSETS, OUT = sys.argv[1], sys.argv[2]
KEY = (255, 0, 255)  # index 0, transparent, used nowhere else

# Piece colors, our own (not the colors any other game gives these shapes). Order: I O T S Z J L.
PIECES = [(248, 128, 56), (64, 192, 176), (248, 200, 48), (72, 104, 248),
          (112, 200, 72), (232, 72, 152), (152, 96, 232)]
JUNK = (136, 140, 156)
WELL_BG, WELL_DOT, WHITE = (24, 28, 48), (56, 64, 96), (248, 248, 248)


def snap(c):
    return tuple(min(248, max(0, int(round(v / 8)) * 8)) for v in c)


def shades(base):
    light = snap(tuple(v + (255 - v) * 0.45 for v in base))
    dark = snap(tuple(v * 0.6 for v in base))
    return [light, snap(base), dark]


def image(palette, tiles):
    """tiles: list of 8 strings of 8 chars each, '.' = index 0, digits/letters = palette index (hex)."""
    assert len(set(palette)) == len(palette), 'duplicate colors in a palette'
    im = Image.new('P', (128, 8), 0)
    px = im.load()
    for i, t in enumerate(tiles):
        for y, row in enumerate(t):
            for x, ch in enumerate(row):
                px[i * 8 + x, y] = 0 if ch == '.' else int(ch, 16)
    im.putpalette([v for c in palette for v in c])
    return im


def block(i):  # i = first of the 3 shade indices (light, base, dark)
    l, b, d = (format(i + k, 'x') for k in range(3))
    return [l * 7 + d] + [l + b * 6 + d for _ in range(6)] + [l + d * 7]


def outline(i):  # landing outline in the piece's base color over an empty cell
    b = format(i + 1, 'x')
    return [b * 8] + [b + 'dddddd' + b for _ in range(6)] + [b * 8]


BLANK = ['........'] * 8
EMPTY = ['dddddddd'] * 7 + ['ddddddde']
FLASH = ['ffffffff'] * 8


def blocks(pieces, junk):
    pal = [KEY]
    for c in pieces + ([junk] if junk else []):
        pal += shades(c)
    assert len(pal) == 13
    pal += [WELL_BG, WELL_DOT, WHITE]
    tiles = [BLANK] + [block(1 + 3 * k) for k in range(len(pieces))]
    if junk:
        tiles.append(block(1 + 3 * len(pieces)))
    tiles += [outline(1 + 3 * k) for k in range(len(pieces))]
    tiles += [EMPTY, FLASH]
    return image(pal, tiles)


# Partitions: 4 px panels; a separator column holds the right wall of one well (x 0..3) and the left wall of
# the next (x 4..7). 1 outline, 2 fabric, 3 light, 4 dark, 5 metal trim, 6 floor, 7 floor light.
WALL_PAL = [KEY, (16, 20, 36), (104, 116, 152), (152, 164, 196), (72, 80, 112), (208, 212, 224),
            (48, 52, 76), (88, 94, 124)]
R_PANEL, L_PANEL = '4231', '1324'
CAP = ['....'] * 5 + ['5555', '5555', '3333']
FLOOR = ['55555555', '77777777'] + ['66666666'] * 6


def halves(left, right):
    return [a + b for a, b in zip(left, right)]


def walls():
    none = ['....'] * 8
    rp, lp = [R_PANEL] * 8, [L_PANEL] * 8
    tiles = [BLANK,
             halves(rp, lp), halves(none, lp), halves(rp, none),          # separators: both, left, right
             halves(CAP, CAP), halves(none, CAP), halves(CAP, none),  # caps
             FLOOR, halves(none, [r[4:] for r in FLOOR]), halves([r[:4] for r in FLOOR], none)]
    return image(WALL_PAL, tiles)


# --- Font: PVSnesLib's example font plus a drop shadow ---------------------------------------------
def font():
    src = Image.open(os.path.join(ASSETS, 'font.bmp'))
    s = src.load()
    out = Image.new('P', src.size, 0)
    o = out.load()
    for y in range(src.height):
        for x in range(src.width):
            if s[x, y]:
                o[x, y] = 1
    for y in range(src.height):
        for x in range(src.width):
            if s[x, y] and (x % 8) < 7 and y < 7 and not s[x + 1, y + 1]:
                o[x + 1, y + 1] = 2
    out.putpalette(list(KEY) + [248, 248, 248] + [24, 20, 32])
    return out


# --- Players: WorkAdventure's own Woka parts (body, eyes, outfit, hair), as in Bombadventure ----------
# Outfits follow the cabinet's badge colors where they can: P1 red, P2 blue, P3 green, P4 yellow.
PLAYERS = [
    ('Character_bodye', 'Character_eye1', 'Character_outfit4b', 'character_hair18c'),  # P1 white shirt, red tie
    ('Character_bodyd', 'Character_eye1', 'Character_outfit5a', 'character_hair19a'),  # P2 blue suit
    ('Character_bodyh', 'Character_eye1', 'Character_outfit2', 'character_hair28d'),   # P3 green jacket
    ('Character_bodyb', 'Character_eye1', 'Character_outfit25', 'character_hair33b'),  # P4 pink top, ginger
]
WOKA = 21  # 2/3 of the 32 px Woka frame


def woka_frame(p, row, col):
    im = Image.new('RGBA', (32, 32))
    for layer in p:
        sheet = Image.open(os.path.join(ASSETS, 'woka', f'{layer}.png')).convert('RGBA')
        im.alpha_composite(sheet.crop((col * 32, row * 32, col * 32 + 32, row * 32 + 32)))
    return im


def to_indexed(rgba, ncolors=15):
    """Hard alpha, quantize the opaque pixels to ncolors, index 0 = transparent. No palette padding."""
    alpha = rgba.getchannel('A').point(lambda v: 255 if v > 110 else 0)
    q = rgba.convert('RGB').quantize(ncolors, method=Image.MEDIANCUT)
    out = Image.new('P', rgba.size, 0)
    out.putpalette(list(KEY) + q.getpalette()[:ncolors * 3])
    src, a, dst = q.load(), alpha.load(), out.load()
    for y in range(rgba.height):
        for x in range(rgba.width):
            if a[x, y]:
                dst[x, y] = src[x, y] + 1
    return out


def player_sheet(p):
    frames = []
    for row, col in ((0, 1), (1, 1), (2, 1), (3, 1)):  # Woka sheet rows: down, left, right, up; col 1 stands
        f = woka_frame(p, row, col).resize((WOKA, WOKA), Image.LANCZOS)
        cell = Image.new('RGBA', (32, 32))
        cell.alpha_composite(f, ((32 - WOKA) // 2, 32 - WOKA - 1))
        frames.append(cell)
    for col in range(3):
        frames.append(woka_frame(p, 0, col))
    f = woka_frame(p, 0, 0).resize((WOKA, WOKA), Image.LANCZOS)
    cell = Image.new('RGBA', (32, 32))
    cell.alpha_composite(f, ((32 - WOKA) // 2, 32 - WOKA - 1))
    frames.append(cell)
    sheet = Image.new('RGBA', (32, 32 * len(frames)))
    for i, f in enumerate(frames):
        sheet.alpha_composite(f, (0, 32 * i))
    return to_indexed(sheet)


os.makedirs(OUT, exist_ok=True)
blocks(PIECES[:4], None).save(os.path.join(OUT, 'blocks_a.png'))
blocks(PIECES[4:], JUNK).save(os.path.join(OUT, 'blocks_b.png'))
walls().save(os.path.join(OUT, 'walls.png'))
font().save(os.path.join(OUT, 'font.png'))
for i, p in enumerate(PLAYERS):
    player_sheet(p).save(os.path.join(OUT, f'p{i + 1}.png'))
