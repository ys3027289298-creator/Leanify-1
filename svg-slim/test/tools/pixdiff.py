#!/usr/bin/env python3
"""Pixel-exact comparison of two PNGs.

Exit status: 0 = byte-identical pixels, 1 = differences, 2 = usage/IO error.
Prints a JSON line with width/height, the number of differing pixels and the
maximum per-channel absolute error.
"""
import json
import sys

from PIL import Image, ImageChops


def main():
    if len(sys.argv) != 3:
        print("usage: pixdiff.py a.png b.png", file=sys.stderr)
        return 2
    with Image.open(sys.argv[1]) as im_a, Image.open(sys.argv[2]) as im_b:
        if im_a.size != im_b.size or im_a.mode != im_b.mode:
            print(json.dumps({"error": "size-or-mode-mismatch",
                              "a": [im_a.size, im_a.mode], "b": [im_b.size, im_b.mode]}))
            return 1
        rgba_a = im_a.convert("RGBA")
        rgba_b = im_b.convert("RGBA")
        diff = ImageChops.difference(rgba_a, rgba_b)
        diff_bytes = diff.tobytes()
        differing_pixels = sum(
            1 for i in range(0, len(diff_bytes), 4) if diff_bytes[i:i + 4] != b"\x00\x00\x00\x00"
        )
        histogram = diff.histogram()
        max_error = 0
        for band in range(4):
            for value in range(255, 0, -1):
                if histogram[band * 256 + value]:
                    max_error = max(max_error, value)
                    break
        print(json.dumps({
            "width": im_a.size[0],
            "height": im_a.size[1],
            "differing_pixels": differing_pixels,
            "total_pixels": im_a.size[0] * im_a.size[1],
            "max_channel_error": max_error,
            "identical": differing_pixels == 0,
        }))
        return 0 if differing_pixels == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
