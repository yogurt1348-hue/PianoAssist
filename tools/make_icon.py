"""Builds assets/pianoassist.ico and assets/pianoassist-256.png from the
source artwork (assets/pianoassist-icon-source.png).

The source is a navy rounded square on black. The black corners become
transparent; small sizes (16-32 px), where the thin serif strokes vanish,
are drawn as a simplified bold "PA" on the same navy square.

Usage: python tools/make_icon.py   (needs Pillow)
"""
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "assets" / "pianoassist-icon-source.png"
NAVY = (9, 19, 48, 255)
CREAM = (253, 246, 232, 255)
LILAC = (169, 150, 255, 255)
SIZES = [16, 24, 32, 48, 64, 128, 256]


def square_bounds(img):
    """Bounds of the non-black rounded square."""
    grey = img.convert("L").point(lambda v: 255 if v > 12 else 0)
    return grey.getbbox()


def rounded_mask(size, radius_ratio, scale=4):
    big = Image.new("L", (size * scale, size * scale), 0)
    ImageDraw.Draw(big).rounded_rectangle(
        (0, 0, size * scale - 1, size * scale - 1), radius=int(size * scale * radius_ratio), fill=255)
    return big.resize((size, size), Image.LANCZOS)


def detailed(source, size):
    img = source.resize((size, size), Image.LANCZOS).convert("RGBA")
    img.putalpha(rounded_mask(size, 0.17))
    return img


def font(px):
    for name in ("DejaVuSans-Bold.ttf", "arialbd.ttf", "Arial Bold.ttf"):
        try:
            return ImageFont.truetype(name, px)
        except OSError:
            continue
    return ImageFont.load_default()


def simple(size):
    """Bold PA for tiny sizes: readable at 16 px."""
    scale = 8
    s = size * scale
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    draw.rounded_rectangle((0, 0, s - 1, s - 1), radius=int(s * 0.2), fill=NAVY)
    f = font(int(s * 0.62))
    box = draw.textbbox((0, 0), "PA", font=f)
    w, h = box[2] - box[0], box[3] - box[1]
    draw.text(((s - w) / 2 - box[0], (s - h) / 2 - box[1]), "PA", font=f, fill=CREAM)
    # The lilac key from the artwork, as a short bar under the letters.
    bar_w, bar_h = s * 0.18, s * 0.07
    draw.rectangle(((s - bar_w) / 2, s * 0.84, (s + bar_w) / 2, s * 0.84 + bar_h), fill=LILAC)
    return img.resize((size, size), Image.LANCZOS)


def main():
    source = Image.open(SOURCE).convert("RGB")
    left, top, right, bottom = square_bounds(source)
    side = max(right - left, bottom - top)
    inset = round(side * 0.008)  # drop the dark anti-aliased rim of the artwork
    source = source.crop((left + inset, top + inset, left + side - inset, top + side - inset))

    images = [simple(n) if n <= 32 else detailed(source, n) for n in SIZES]
    out = ROOT / "assets"
    images[-1].save(out / "pianoassist-256.png")
    images[SIZES.index(32)].save(out / "pianoassist-32.png")  # JUCE ICON_SMALL
    images[-1].save(out / "pianoassist.ico", format="ICO", sizes=[(n, n) for n in SIZES], append_images=images[:-1])

    # Preview for checking readability on dark and light backgrounds.
    preview = Image.new("RGBA", (sum(SIZES) + 10 * len(SIZES) + 10, 2 * 256 + 30), (0, 0, 0, 0))
    preview.paste((32, 32, 36, 255), (0, 0, preview.width, 256 + 15))
    preview.paste((243, 243, 243, 255), (0, 256 + 15, preview.width, preview.height))
    x = 10
    for n, im in zip(SIZES, images):
        preview.alpha_composite(im, (x, 5 + (256 - n)))
        preview.alpha_composite(im, (x, 256 + 20 + (256 - n)))
        x += n + 10
    preview.save(ROOT / "docs" / "icon-preview.png")


if __name__ == "__main__":
    main()
