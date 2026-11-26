#!/usr/bin/env python3
"""Generate assets/icon.ico, the application's icon, drawn here (the artwork is this script's geometry).

The icon is the exe's (src/TmDq.App/TmDq.App.csproj's ApplicationIcon: Explorer, the taskbar, the shortcuts), every
window's (an AvaloniaResource: the title bar, Alt+Tab) and the installer's (SetupIconFile). Windows picks the size it
needs from the file, and scales the nearest one when the file lacks it, so it carries every size Windows asks for at
100 % to 200 %: 16, 24, 32, 48, 64 and 256 px.

The mark is a PCM bit stream (a stepped wave, teal, solid) over the Receiver SNR trace (dotted, amber) on a navy tile:
the two things the application plots, drawn the way the plot draws them (PCM solid, SNR dotted).

Two drawings, because a small icon is not a shrunk large one. 48 px and up carry the full mark (a stepped wave over the
dotted SNR curve); 16, 24 and 32 px carry a simplified one (three steps over three dots) whose every edge sits on a
whole pixel, so nothing blurs: 16 and 32 are one drawing at 1x and 2x, 24 has its own grid.

Sizes below 256 are 32-bit DIBs (with the all-zero AND mask Windows ignores for 32-bit images), 256 is PNG, as the
Windows SDK's own icons are: the DIB form is what every reader takes, and a 256 px DIB would be 256 KB.

The output is committed; run this again only after the artwork changes. It needs Pillow, the only third-party
package any script in eng/ imports (pinned in eng/requirements.txt):

    py -m pip install -r eng/requirements.txt
    py eng/gen_app_icon.py
"""

from __future__ import annotations

import io
import struct
from pathlib import Path

try:
    from PIL import Image, ImageDraw
except ImportError:
    raise SystemExit("gen_app_icon.py needs Pillow: py -m pip install -r eng/requirements.txt") from None

REPO = Path(__file__).resolve().parent.parent
OUTPUT = REPO / "assets" / "icon.ico"
SIZES = (16, 24, 32, 48, 64, 256)

TILE = (0x13, 0x29, 0x4B)
TEAL = (0x35, 0xE0, 0xC8)
AMBER = (0xFF, 0xB5, 0x47)

Point = tuple[float, float]

# The full mark, on a 64-unit grid.
FULL_UNITS = 64
FULL_TILE = (2, 2, 62, 62, 14)                       # left, top, right, bottom, corner radius
FULL_WAVE = [(8, 31), (17, 31), (17, 17), (29, 17), (29, 31), (37, 31), (37, 17), (47, 17), (47, 31), (56, 31)]
FULL_WAVE_WIDTH = 4.5
FULL_CURVE = [((10, 53), (21, 53), (26, 46), (35, 44)), ((35, 44), (44, 42), (48, 41), (55, 39))]
FULL_DOT_PITCH, FULL_DOT_RADIUS, FULL_END_DOT = 6.7, 1.75, 3.2

# The simplified mark: whole-pixel edges on its own grid (16 units; 32 px is the same drawing at 2x, 24 px is below).
SMALL_16 = dict(
    units=16, tile=(0, 0, 16, 16, 3.5),
    wave=[(2, 9), (5, 9), (5, 4), (8, 4), (8, 9), (11, 9), (11, 4), (14, 4)], width=2,
    dots=[(3, 13), (8, 13), (13, 13)], radius=1)
SMALL_24 = dict(
    units=24, tile=(0, 0, 24, 24, 5),
    wave=[(3, 13.5), (7.5, 13.5), (7.5, 6.5), (12.5, 6.5), (12.5, 13.5), (16.5, 13.5), (16.5, 6.5), (21, 6.5)], width=3,
    dots=[(5.5, 19.5), (12.5, 19.5), (19.5, 19.5)], radius=1.5)


def stepped_wave(draw: ImageDraw.ImageDraw, points: list[Point], width: float, scale: float) -> None:
    """An axis-aligned polyline with butt ends and mitred corners: a rectangle a segment, a square a corner."""
    half = width / 2
    for (x0, y0), (x1, y1) in zip(points, points[1:]):
        draw.rectangle([(min(x0, x1) - (half if x0 == x1 else 0)) * scale, (min(y0, y1) - (half if y0 == y1 else 0)) * scale,
                        (max(x0, x1) + (half if x0 == x1 else 0)) * scale - 1, (max(y0, y1) + (half if y0 == y1 else 0)) * scale - 1],
                       fill=TEAL)
    for x, y in points[1:-1]:
        draw.rectangle([(x - half) * scale, (y - half) * scale, (x + half) * scale - 1, (y + half) * scale - 1], fill=TEAL)


def circle(draw: ImageDraw.ImageDraw, centre: Point, radius: float, scale: float, colour: tuple[int, int, int]) -> None:
    x, y = centre
    draw.ellipse([(x - radius) * scale, (y - radius) * scale, (x + radius) * scale - 1, (y + radius) * scale - 1], fill=colour)


def bezier(curve: tuple[Point, Point, Point, Point], steps: int = 2000) -> list[Point]:
    (x0, y0), (x1, y1), (x2, y2), (x3, y3) = curve
    out = []
    for i in range(steps + 1):
        t = i / steps
        a, b, c, d = (1 - t) ** 3, 3 * (1 - t) ** 2 * t, 3 * (1 - t) * t ** 2, t ** 3
        out.append((a * x0 + b * x1 + c * x2 + d * x3, a * y0 + b * y1 + c * y2 + d * y3))
    return out


def dotted(curves, pitch: float) -> list[Point]:
    """Points at every `pitch` of arc length along the curves, from the first one's start (a dash pattern of dots)."""
    path = [p for curve in curves for p in bezier(curve)]
    out, travelled, next_at = [path[0]], 0.0, pitch
    for (xa, ya), (xb, yb) in zip(path, path[1:]):
        travelled += ((xb - xa) ** 2 + (yb - ya) ** 2) ** 0.5
        if travelled >= next_at:
            out.append((xb, yb))
            next_at += pitch
    return out


def draw_mark(size: int, supersample: int) -> Image.Image:
    """The icon at `size` px, drawn `supersample` times larger and reduced, which anti-aliases every edge exactly."""
    big = size * supersample
    image = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    if size >= 48:
        scale = big / FULL_UNITS
        left, top, right, bottom, radius = FULL_TILE
        draw.rounded_rectangle([left * scale, top * scale, right * scale - 1, bottom * scale - 1], radius * scale, fill=TILE)
        stepped_wave(draw, FULL_WAVE, FULL_WAVE_WIDTH, scale)
        for centre in dotted(FULL_CURVE, FULL_DOT_PITCH):
            circle(draw, centre, FULL_DOT_RADIUS, scale, AMBER)
        circle(draw, FULL_CURVE[-1][-1], FULL_END_DOT, scale, AMBER)
    else:
        art = SMALL_24 if size == 24 else SMALL_16
        scale = big / art["units"]
        left, top, right, bottom, radius = art["tile"]
        draw.rounded_rectangle([left * scale, top * scale, right * scale - 1, bottom * scale - 1], radius * scale, fill=TILE)
        stepped_wave(draw, art["wave"], art["width"], scale)
        for x, y in art["dots"]:   # squares: a round dot this small is a dim blur; 32 px is the 16-unit drawing at 2x
            r = art["radius"]
            draw.rectangle([(x - r) * scale, (y - r) * scale, (x + r) * scale - 1, (y + r) * scale - 1], fill=AMBER)
    return image.resize((size, size), Image.Resampling.BOX)


def dib(image: Image.Image) -> bytes:
    """A 32-bit BGRA DIB as an icon holds it: bottom-up rows, the height doubled for the AND mask that follows."""
    size = image.width
    header = struct.pack("<IiiHHIIiiII", 40, size, size * 2, 1, 32, 0, 0, 0, 0, 0, 0)
    rows = image.tobytes("raw", "BGRA", 0, -1)  # bottom-up
    mask_row = ((size + 31) // 32) * 4
    return header + rows + bytes(mask_row * size)


def png(image: Image.Image) -> bytes:
    out = io.BytesIO()
    image.save(out, format="PNG", optimize=True)
    return out.getvalue()


def main() -> None:
    images = [png(frame) if size == 256 else dib(frame)
              for size in SIZES
              for frame in [draw_mark(size, 4 if size >= 256 else 16 if size <= 32 else 8)]]

    directory = struct.pack("<HHH", 0, 1, len(SIZES))
    offset = 6 + 16 * len(SIZES)
    for size, data in zip(SIZES, images):
        side = 0 if size == 256 else size  # 0 means 256 in an icon directory
        directory += struct.pack("<BBBBHHII", side, side, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
    OUTPUT.write_bytes(directory + b"".join(images))
    print(f"wrote {OUTPUT.relative_to(REPO)}: {', '.join(map(str, SIZES))} px, {offset} bytes")


if __name__ == "__main__":
    main()
