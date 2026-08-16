#!/usr/bin/env python3
"""Rasterize site icons from the existing ADDITION wordmark.

Reads docs/assets/logo-transparent.png. Does not invent a new mark.
"""

from __future__ import annotations

import shutil
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "docs" / "assets" / "logo-transparent.png"
PUBLIC = ROOT / "web" / "public"
RED_MIN = 80


def content_bbox(im: Image.Image) -> tuple[int, int, int, int]:
    px = im.convert("RGBA")
    width, height = px.size
    min_x, min_y, max_x, max_y = width, height, -1, -1
    for y in range(height):
        for x in range(width):
            red, _green, _blue, alpha = px.getpixel((x, y))
            if alpha > 10 and red > RED_MIN:
                if x < min_x:
                    min_x = x
                if y < min_y:
                    min_y = y
                if x > max_x:
                    max_x = x
                if y > max_y:
                    max_y = y
    if max_x < 0:
        raise RuntimeError("logo-transparent.png has no visible red ink")
    return min_x, min_y, max_x + 1, max_y + 1


def letter_a_box(im: Image.Image) -> tuple[int, int, int, int]:
    px = im.convert("RGBA")
    width, height = px.size
    start = None
    end = None
    in_run = False
    for x in range(width):
        hit = False
        for y in range(0, height, 2):
            red, _green, _blue, alpha = px.getpixel((x, y))
            if alpha > 10 and red > RED_MIN:
                hit = True
                break
        if hit and not in_run:
            start = x
            in_run = True
        elif not hit and in_run:
            end = x
            break
    if start is None or end is None:
        raise RuntimeError("could not isolate the capital A")
    _x0, y0, _x1, y1 = content_bbox(im)
    pad = 8
    return max(0, start - pad), max(0, y0 - pad), min(width, end + pad), min(height, y1 + pad)


def square_on_black(src: Image.Image, size: int) -> Image.Image:
    cropped = src.convert("RGBA").crop(letter_a_box(src))
    side = max(cropped.size)
    tile = Image.new("RGBA", (side, side), (0, 0, 0, 255))
    tile.paste(cropped, ((side - cropped.size[0]) // 2, (side - cropped.size[1]) // 2), cropped)
    return tile.resize((size, size), Image.Resampling.LANCZOS)


def write_og(src: Image.Image, dest: Path) -> None:
    canvas = Image.new("RGBA", (1200, 630), (0, 0, 0, 255))
    mark = src.convert("RGBA")
    max_w, max_h = 960, 220
    scale = min(max_w / mark.size[0], max_h / mark.size[1])
    new_size = (max(1, int(mark.size[0] * scale)), max(1, int(mark.size[1] * scale)))
    mark = mark.resize(new_size, Image.Resampling.LANCZOS)
    x = (1200 - mark.size[0]) // 2
    y = (630 - mark.size[1]) // 2
    canvas.paste(mark, (x, y), mark)
    canvas.convert("RGB").save(dest, "PNG")


def main() -> int:
    if not SRC.is_file():
        print("error: missing %s" % SRC, file=sys.stderr)
        return 2
    PUBLIC.mkdir(parents=True, exist_ok=True)
    shutil.copy2(SRC, PUBLIC / "logo-transparent.png")
    src = Image.open(SRC)
    fav32 = square_on_black(src, 32)
    fav32.save(PUBLIC / "favicon-32.png", "PNG")
    apple = square_on_black(src, 180)
    apple.save(PUBLIC / "apple-touch-icon.png", "PNG")
    ico16 = square_on_black(src, 16)
    ico48 = square_on_black(src, 48)
    ico16.save(
        PUBLIC / "favicon.ico",
        format="ICO",
        sizes=[(16, 16), (32, 32), (48, 48)],
        append_images=[fav32, ico48],
    )
    write_og(src, PUBLIC / "og.png")
    shutil.copy2(PUBLIC / "og.png", PUBLIC / "twitter.png")
    print("wrote logo, favicon, apple-touch-icon, og, twitter under web/public/")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
