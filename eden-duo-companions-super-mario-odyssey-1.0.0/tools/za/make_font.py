#!/usr/bin/env python3
"""Build the redistributable ZA MFNT/PNG font (Pillow required).

Usage: python tools/za/make_font.py --ttf /path/to/DejaVuSans.ttf
The TTF is a development input; only the atlas, metrics and license ship.
"""
import argparse
import struct
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ttf', type=Path, required=True)
    args = parser.parse_args()
    font = ImageFont.truetype(str(args.ttf), 40)
    ascent, descent = font.getmetrics()
    line_height = ascent + descent
    atlas = Image.new('RGBA', (1024, 1024), (255, 255, 255, 0))
    draw = ImageDraw.Draw(atlas)
    records = [bytes(14)]  # MFNT's first record is .notdef, then U+0020.
    x = y = 2
    row_height = 0
    for cp in range(0x20, 0x180):
        ch = chr(cp)
        left, top, right, bottom = font.getbbox(ch, anchor='ls')
        w, h = right - left, bottom - top
        if x + w + 2 > atlas.width:
            x, y = 2, y + row_height + 4
            row_height = 0
        if y + h + 2 > atlas.height:
            raise ValueError('font atlas overflow')
        draw.text((x - left, y - top), ch, font=font, anchor='ls', fill='white')
        records.append(struct.pack('<HHHHhhH', x, y, w, h, left, -top,
                                   round(font.getlength(ch))))
        x += w + 4
        row_height = max(row_height, h)
    header = bytearray(0x60)
    header[:4] = b'MFNT'
    struct.pack_into('<I', header, 0x1C, line_height)
    struct.pack_into('<I', header, 0x20, len(records))
    struct.pack_into('<I', header, 0x28, len(header))
    out = Path(__file__).resolve().parents[2] / 'packages/PokemonLegendsZA/dualscreen/font'
    out.mkdir(parents=True, exist_ok=True)
    (out / 'za.mfnt').write_bytes(header + b''.join(records))
    atlas.save(out / 'za.png', optimize=True)
    print(f'font: {len(records)-1} glyphs, line height {line_height}, {out}')


if __name__ == '__main__':
    main()
