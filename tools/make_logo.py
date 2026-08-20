#!/usr/bin/env python3
"""Generate the project logo assets.

Draws the wordmark from scratch -- Times New Roman Bold plus procedural blue
fire -- rather than upscaling the 1999 GIF, so the letterforms stay sharp at
any size. The face was identified by overlaying candidates on the original at
3x: Times New Roman Bold lands on its serifs and letter positions almost
exactly.
The palette (deep navy ground, blue tongues, blue-white cores) is taken from
Mr Elusive's original logo; the artwork here is redrawn, not derived from his
pixels.

    python tools/make_logo.py

Writes into docs/images/:
    logo-header-dark.png    transparent, light wordmark, for dark backgrounds
    logo-header-light.png   transparent, deep wordmark, for light backgrounds
    social-banner.png       1280x640 with background, for the GitHub social preview

Requires Pillow and numpy, and the two Windows fonts named below. On other
platforms substitute any Times clone and any transitional serif.
"""

from __future__ import annotations

import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

WORDMARK_FONT = "C:/Windows/Fonts/timesbd.ttf"   # Times New Roman Bold
SERIF = "C:/Windows/Fonts/cambria.ttc"

# The 1999 wordmark is Times New Roman Bold, very slightly stretched. Overlaying
# the face on the original at 3x puts the serifs and letter positions on top of
# each other; the residual difference is about 3% and reads as a little extra
# height, so the glyph plate is scaled vertically before compositing.
WORD_STRETCH_Y = 1.12

WORD = "Gladiator Bot"
SUB = "RECONSTRUCTION"

SS = 2                 # supersample factor; output is downsampled from this
HEADER_WIDTH = 1600
BANNER_SIZE = (1280, 640)
OUT_DIR = Path(__file__).resolve().parent.parent / "docs" / "images"

# Hottest gas is blue-white; this runs cold -> hot.
DARK_STOPS = [
    (0.00, (0, 0, 20)),
    (0.18, (12, 30, 110)),
    (0.38, (30, 78, 190)),
    (0.60, (70, 140, 232)),
    (0.80, (146, 196, 246)),
    (1.00, (232, 244, 255)),
]

# Runs the other way: the core ends deep instead of white, so the wordmark
# still reads when the page behind it is white.
LIGHT_STOPS = [
    (0.00, (150, 190, 240)),
    (0.30, (84, 142, 220)),
    (0.60, (44, 96, 194)),
    (0.85, (24, 60, 155)),
    (1.00, (14, 38, 112)),
]


def fbm(h: int, w: int, octaves: int = 5, base: int = 3, rng=None) -> np.ndarray:
    """Fractal value noise, built by upsampling random grids."""
    rng = rng or np.random.default_rng(0)
    total = np.zeros((h, w), np.float32)
    amp = 1.0
    norm = 0.0
    for octave in range(octaves):
        ry = max(2, base * (2 ** octave))
        rx = max(2, int(ry * w / h))
        grid = (rng.random((ry, rx)) * 255).astype(np.uint8)
        layer = np.asarray(
            Image.fromarray(grid).resize((w, h), Image.BICUBIC), np.float32
        ) / 255.0
        total += amp * layer
        norm += amp
        amp *= 0.5
    return total / norm


def ramp(t: np.ndarray, stops) -> np.ndarray:
    t = np.clip(t, 0, 1)
    out = np.zeros(t.shape + (3,), np.float32)
    for (a, ca), (b, cb) in zip(stops, stops[1:]):
        m = (t >= a) & (t <= b)
        if not m.any():
            continue
        u = ((t[m] - a) / (b - a))[:, None]
        out[m] = np.array(ca, np.float32) * (1 - u) + np.array(cb, np.float32) * u
    return out / 255.0


def contour_emitters(mask: np.ndarray, band: int, seed: int, roughness: float = 1.0):
    """Emit fire from the letterforms' own upper contour.

    Sparse, separated blobs make tongues that read as a row of candles standing
    behind the type. For flamed lettering the source has to be the letters: a
    continuous band hugging the top edge of every stroke, so the fire grows out
    of the glyph outline with no seam between the two.

    A continuous band alone produces one broad column per letter -- a curtain --
    so the ragged silhouette comes from `vigour` instead: fine-scale noise that
    varies sharply between neighbouring columns, letting adjacent tongues die at
    very different heights.
    """
    h, w = mask.shape
    rng = np.random.default_rng(seed)
    ink = mask > 0.5
    present = ink.any(axis=0)
    if not present.any():
        return np.zeros_like(mask), np.ones(w, np.float32)

    top = np.argmax(ink, axis=0)
    rows = np.arange(h)[:, None]
    band_px = max(int(band), 1)
    emitters = ((rows >= top[None, :])
                & (rows < (top + band_px)[None, :])
                & present[None, :]).astype(np.float32)

    # Uneven heat along the contour, so the base of the fire is not a flat line.
    grain = fbm(1, w, octaves=6, base=14, rng=rng)[0]
    emitters *= (0.60 + 0.40 * grain)[None, :]

    # Fine-scale vigour: neighbouring columns differ strongly, which is what
    # breaks the curtain into separate tongues of visibly different height.
    v = fbm(1, w, octaves=7, base=18, rng=rng)[0]
    v = np.clip((v - 0.5) * roughness + 0.5, 0, 1)
    vigour = 0.12 + 1.55 * v ** 2

    emitters /= max(emitters.max(), 1e-6)
    return emitters, vigour.astype(np.float32)


def rise(emitters: np.ndarray, vigour: np.ndarray, height_px: float,
         seed: int, wander: float) -> np.ndarray:
    """Carry heat upward into curling tongues.

    `height_px` is a tongue's e-folding height, so the per-row decay is
    exp(-1/height) -- very close to 1. A small decay extinguishes the fire
    within a few rows instead of a few hundred.
    """
    h, w = emitters.shape
    rng = np.random.default_rng(seed)
    turb = fbm(h, w, octaves=6, base=6, rng=rng)
    sway = fbm(h, w, octaves=3, base=2, rng=rng) - 0.5

    heat = emitters.astype(np.float32).copy()
    xs = np.arange(w, dtype=np.float32)
    for y in range(h - 2, -1, -1):
        above = heat[y + 1]
        idx = np.clip(xs + sway[y] * wander, 0, w - 1)
        i0 = idx.astype(np.int32)
        i1 = np.minimum(i0 + 1, w - 1)
        f = idx - i0
        carried = above[i0] * (1 - f) + above[i1] * f
        reach = np.maximum(height_px * vigour * (0.35 + 1.10 * turb[y]), 1.0)
        heat[y] = np.maximum(emitters[y], carried * np.exp(-1.0 / reach))
    return np.clip(heat, 0, 1)


def _tracked(draw, xy, text, font, fill, tracking):
    x, y = xy
    for ch in text:
        draw.text((x, y), ch, font=font, fill=fill)
        x += draw.textlength(ch, font=font) + tracking


def _tracked_width(draw, text, font, tracking):
    return (sum(draw.textlength(c, font=font) for c in text)
            + tracking * (len(text) - 1))


def build(word_px=300, sub_px=60, tracking=28, seed=11,
          stops=None, sub_fill=(190, 216, 248, 255)) -> Image.Image:
    stops = stops or DARK_STOPS
    word_font = ImageFont.truetype(WORDMARK_FONT, word_px * SS)
    sub_font = ImageFont.truetype(SERIF, sub_px * SS)

    probe = ImageDraw.Draw(Image.new("L", (8, 8)))
    wbox = probe.textbbox((0, 0), WORD, font=word_font)
    ww = wbox[2] - wbox[0]
    wh = int((wbox[3] - wbox[1]) * WORD_STRETCH_Y)
    sw = _tracked_width(probe, SUB, sub_font, tracking * SS)

    pad_x = int(120 * SS)
    head = int(wh * 1.30)          # headroom for the tongues
    gap = int(58 * SS)
    sub_h = int(sub_px * 1.7 * SS)
    w = int(max(ww, sw) + 2 * pad_x)
    h = head + wh + gap + sub_h + int(40 * SS)

    # Draw the wordmark on its own plate so it can be stretched, then paste it.
    glyphs = Image.new("L", (wbox[2] - wbox[0] + 4, wbox[3] - wbox[1] + 4), 0)
    ImageDraw.Draw(glyphs).text((2 - wbox[0], 2 - wbox[1]), WORD,
                                font=word_font, fill=255)
    glyphs = glyphs.crop(glyphs.getbbox())
    glyphs = glyphs.resize((ww, wh), Image.LANCZOS)

    plate = Image.new("L", (w, h), 0)
    plate.paste(glyphs, (int((w - ww) / 2), head))
    mask = np.asarray(plate, np.float32) / 255.0

    emitters, vigour = contour_emitters(mask, band=max(3, int(wh * 0.05)),
                                        seed=seed, roughness=1.35)
    tongues = rise(emitters, vigour, height_px=wh * 0.55, seed=seed, wander=0.75)
    tongues = np.clip(tongues, 0, 1) ** 0.80

    # The fire has to be continuous with the glyphs it comes off, so the letters
    # themselves sit at full heat and the tongues blend straight out of them.
    tongues = np.maximum(tongues, mask)

    glow = np.asarray(
        Image.fromarray((mask * 255).astype(np.uint8))
        .filter(ImageFilter.GaussianBlur(9 * SS)), np.float32) / 255.0

    heat = np.maximum(np.maximum(tongues, mask), glow * 0.55)
    heat = np.asarray(
        Image.fromarray((heat * 255).astype(np.uint8))
        .filter(ImageFilter.GaussianBlur(0.8 * SS)), np.float32) / 255.0
    heat = np.maximum(heat, mask)

    rgb = ramp(heat, stops)
    alpha = np.maximum(np.clip(heat * 1.30, 0, 1) ** 0.90, mask)
    img = Image.fromarray(
        (np.concatenate([rgb, alpha[..., None]], axis=2) * 255).astype(np.uint8))

    sub = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    _tracked(ImageDraw.Draw(sub), ((w - sw) / 2, head + wh + gap),
             SUB, sub_font, sub_fill, tracking * SS)
    img = Image.alpha_composite(img, sub.filter(ImageFilter.GaussianBlur(7 * SS)))
    img = Image.alpha_composite(img, sub)

    box = img.getbbox()
    pad = int(70 * SS)
    return img.crop((max(0, box[0] - pad), max(0, box[1] - pad),
                     min(w, box[2] + pad), min(h, box[3] + pad)))


def _fit(img: Image.Image, width: int) -> Image.Image:
    return img.resize((width, round(img.height * width / img.width)), Image.LANCZOS)


def make_banner(logo: Image.Image) -> Image.Image:
    w, h = BANNER_SIZE
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)

    # Diagonal navy wash echoing the 1999 art: lit upper-left, falling to black.
    g = 1.0 - np.clip((xx / w) * 0.75 + (yy / h) * 0.45, 0, 1)
    base = np.zeros((h, w, 3), np.float32)
    base[..., 2] = 0.015 + 0.30 * g ** 1.5
    base[..., 1] = 0.010 * g

    cx, cy = w / 2, h / 2
    r = np.sqrt(((xx - cx) / cx) ** 2 + ((yy - cy) / cy) ** 2)
    base *= np.clip(1.15 - 0.45 * r ** 2, 0, 1)[..., None]

    bg = Image.fromarray((np.clip(base, 0, 1) * 255).astype(np.uint8)).convert("RGBA")
    scaled = _fit(logo, int(w * 0.88))
    bg.alpha_composite(scaled, ((w - scaled.width) // 2,
                                max(0, int(h * 0.5 - scaled.height * 0.5))))
    return bg.convert("RGB")


def main() -> int:
    for path in (WORDMARK_FONT, SERIF):
        if not Path(path).exists():
            print(f"font not found: {path}", file=sys.stderr)
            return 1

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    dark = build()
    light = build(stops=LIGHT_STOPS, sub_fill=(30, 66, 150, 255))

    _fit(dark, HEADER_WIDTH).save(OUT_DIR / "logo-header-dark.png")
    _fit(light, HEADER_WIDTH).save(OUT_DIR / "logo-header-light.png")
    make_banner(dark).save(OUT_DIR / "social-banner.png")

    for name in ("logo-header-dark.png", "logo-header-light.png", "social-banner.png"):
        p = OUT_DIR / name
        print(f"  {name:24} {Image.open(p).size}  {p.stat().st_size:,} B")
    return 0


if __name__ == "__main__":
    sys.exit(main())
