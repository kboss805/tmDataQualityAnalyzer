#!/usr/bin/env python3
"""Generate the MSI wizard's two images from the application icon's artwork (eng/gen_app_icon.py's geometry and colours).

- assets/installer_dialog.bmp, 493 x 312: the Welcome and Finish pages' picture (WiX's WixUIDialogBmp). The wizard writes its
  text over the right of it, from about x = 180, so the art is a navy panel at the left: the icon's mark drawn large on the
  icon's own tile colour, and the rest white.
- assets/installer_banner.bmp, 493 x 58: the strip across the top of the other pages (WixUIBannerBmp). The wizard writes the
  page's title and description at its left, so it is white with the icon at the right.

Both are 24-bit BMPs, which is what Windows Installer's dialogs draw. The output is committed; run this again only after the
icon's artwork changes (it needs Pillow, as gen_app_icon.py does):

    py eng/gen_installer_images.py
"""

from __future__ import annotations

from pathlib import Path

from PIL import Image

from gen_app_icon import TILE, draw_mark

REPO = Path(__file__).resolve().parent.parent
DIALOG = REPO / "assets" / "installer_dialog.bmp"
BANNER = REPO / "assets" / "installer_banner.bmp"
WHITE = (0xFF, 0xFF, 0xFF)

DIALOG_SIZE = (493, 312)
PANEL_WIDTH = 164            # the wizard's text starts at about 180 px (135 dialog units)
DIALOG_MARK = 132            # the full mark, drawn on its tile, on a panel of the tile's colour: the tile's edge disappears
BANNER_SIZE = (493, 58)
BANNER_MARK = 48             # the icon itself, tile and all: 48 px is the smallest size that carries the full mark


def flatten(image: Image.Image, background: tuple[int, int, int]) -> Image.Image:
    out = Image.new("RGB", image.size, background)
    out.paste(image, mask=image.getchannel("A"))
    return out


def dialog() -> Image.Image:
    out = Image.new("RGB", DIALOG_SIZE, WHITE)
    out.paste(TILE, (0, 0, PANEL_WIDTH, DIALOG_SIZE[1]))
    mark = flatten(draw_mark(DIALOG_MARK, 8), TILE)
    out.paste(mark, ((PANEL_WIDTH - DIALOG_MARK) // 2, (DIALOG_SIZE[1] - DIALOG_MARK) // 2 - 12))
    return out


def banner() -> Image.Image:
    out = Image.new("RGB", BANNER_SIZE, WHITE)
    mark = flatten(draw_mark(BANNER_MARK, 8), WHITE)
    margin = (BANNER_SIZE[1] - BANNER_MARK) // 2
    out.paste(mark, (BANNER_SIZE[0] - BANNER_MARK - margin - 4, margin))
    return out


def main() -> None:
    for path, image in ((DIALOG, dialog()), (BANNER, banner())):
        image.save(path, format="BMP")
        print(f"wrote {path.relative_to(REPO)}: {image.width} x {image.height}, {path.stat().st_size} bytes")


if __name__ == "__main__":
    main()
