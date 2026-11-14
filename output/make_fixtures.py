#!/usr/bin/env python3
"""Generate the WebP fixture set used by run_acceptance.sh.

All images are synthetic, produced with Pillow (libwebp), so the suite is
self-contained. Pixel comparison later uses ffmpeg and raw RGBA dumps.
"""
import os
import struct

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
FIX = os.path.join(HERE, "fixtures")
os.makedirs(FIX, exist_ok=True)


def gradient_rgb(w, h):
    img = Image.new("RGB", (w, h))
    px = img.load()
    for y in range(h):
        for x in range(w):
            px[x, y] = ((x * 7 + 11) & 0xFF, (y * 13 + 5) & 0xFF, ((x ^ y) * 9) & 0xFF)
    return img


def logo_rgb(w, h):
    img = Image.new("RGB", (w, h), (240, 240, 240))
    px = img.load()
    for y in range(h):
        for x in range(w):
            if x % 17 < 6 or y % 13 < 4 or (x + y) % 23 < 3:
                px[x, y] = ((x * 31) & 0xFF, 90, (y * 17 + 40) & 0xFF)
    return img


def with_alpha(img):
    rgba = img.convert("RGBA")
    px = rgba.load()
    w, h = rgba.size
    for y in range(h):
        for x in range(w):
            r, g, b, _ = px[x, y]
            a = 0 if (x // 8 + y // 8) % 4 == 0 else 255
            if a == 0:
                r, g, b = (r + 73) & 0xFF, (g + 31) & 0xFF, (b + 17) & 0xFF
            px[x, y] = (r, g, b, a)
    return rgba


def save(img, name, **kwargs):
    img.save(os.path.join(FIX, name), "WEBP", **kwargs)
    print("wrote", name)


bars = logo_rgb(160, 110)
photo = gradient_rgb(180, 120)
alpha = with_alpha(logo_rgb(120, 90))

# Lossless still images. method=0 keeps the stream easy to recompress smaller.
save(bars, "ref_bars_lossless.webp", lossless=True, quality=100, method=0, exact=True)
save(alpha, "ref_alpha_lossless.webp", lossless=True, quality=100, method=0, exact=True)
save(photo, "ref_photo_lossless.webp", lossless=True, quality=100, method=6, exact=True)

# A lossy source: must remain pixel-identical (kept verbatim if lossless is bigger).
save(photo, "ref_lossy.webp", lossless=False, quality=82, method=4)

# Metadata-bearing lossless file (VP8X container with XMP and EXIF).
xmp = b'<?xpacket begin=""?><x:xmpmeta>leanify webp fixture padding ' + b"x" * 400 + b"</x:xmpmeta>"
exif = b"Exif\x00\x00" + bytes(range(256)) * 2
save(bars, "ref_meta_lossless.webp", lossless=True, quality=100, method=0, exact=True,
     xmp=xmp, exif=exif)

# Animated WebP: must be skipped untouched.
frames = [logo_rgb(64, 48), gradient_rgb(64, 48)]
frames[0].save(os.path.join(FIX, "ref_animated.webp"), "WEBP", lossless=True, quality=100,
               method=4, exact=True, save_all=True, append_images=frames[1:],
               duration=200, loop=0)
print("wrote ref_animated.webp")

# Simple (non-extended) lossless file without any VP8X chunk.
simple = bars.crop((0, 0, 96, 64))
save(simple, "ref_simple_lossless.webp", lossless=True, quality=100, method=0, exact=True)


def corrupt_fixtures():
    base = open(os.path.join(FIX, "ref_bars_lossless.webp"), "rb").read()

    # Truncated: cut through the VP8L bitstream and fix nothing else.
    with open(os.path.join(FIX, "ref_truncated.webp"), "wb") as f:
        f.write(base[: len(base) - 120])
    print("wrote ref_truncated.webp")

    # Bad RIFF size field: claims one size, payload has another.
    bad = bytearray(base)
    struct.pack_into("<I", bad, 4, struct.unpack_from("<I", bad, 4)[0] + 99)
    with open(os.path.join(FIX, "ref_bad_riffsize.webp"), "wb") as f:
        f.write(bad)
    print("wrote ref_bad_riffsize.webp")

    # Trailing junk after a valid RIFF payload (size field left intact).
    with open(os.path.join(FIX, "ref_trailing_junk.webp"), "wb") as f:
        f.write(base + b"JUNK" * 64)
    print("wrote ref_trailing_junk.webp")


corrupt_fixtures()

# A PNG and JPEG exercising the shared GetType entry, for the regression check.
bars.save(os.path.join(FIX, "ref.png"))
bars.convert("RGB").save(os.path.join(FIX, "ref.jpg"), quality=88)
print("wrote ref.png ref.jpg")
