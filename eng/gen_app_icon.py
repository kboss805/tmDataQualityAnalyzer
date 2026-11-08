#!/usr/bin/env python3
"""Generate assets/icon.ico, the application's icon, from its artwork (docs/design/icon_image.png).

The icon is the exe's (src/TmDq.App/TmDq.App.csproj's ApplicationIcon: Explorer, the taskbar, the shortcuts), every
window's (an AvaloniaResource: the title bar, Alt+Tab) and the installer's (SetupIconFile). Windows picks the size it
needs from the file, and scales the nearest one when the file lacks it, so it carries every size Windows asks for at
100 % to 200 %: 16, 24, 32, 48, 64 and 256 px. The artwork is 710 by 679, so it is centred on a square transparent
canvas first; a non-square icon is stretched.

Sizes below 256 are 32-bit DIBs (with the all-zero AND mask Windows ignores for 32-bit images), 256 is PNG, as the
Windows SDK's own icons are: the DIB form is what every reader takes, and a 256 px DIB would be 256 KB.

The output is committed; run this again only after the artwork changes (needs Pillow):

    py eng/gen_app_icon.py
"""

from __future__ import annotations

import io
import struct
from pathlib import Path

from PIL import Image

REPO = Path(__file__).resolve().parent.parent
ARTWORK = REPO / "docs" / "design" / "icon_image.png"
OUTPUT = REPO / "assets" / "icon.ico"
SIZES = (16, 24, 32, 48, 64, 256)


def square(image: Image.Image) -> Image.Image:
    side = max(image.size)
    canvas = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    canvas.paste(image, ((side - image.width) // 2, (side - image.height) // 2))
    return canvas


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
    art = square(Image.open(ARTWORK).convert("RGBA"))
    images = [png(frame) if size == 256 else dib(frame)
              for size in SIZES
              for frame in [art.resize((size, size), Image.Resampling.LANCZOS)]]

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
