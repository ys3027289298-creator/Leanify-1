# AVIF lossless recompression — deliverables

## What changed

Leanify previously skipped AVIF files because no format handler existed.
The change adds one new optional format module plus three small wiring edits:

- `formats/avif.h`, `formats/avif.cpp` — new `Avif` handler.
- `leanify.cpp` — one detection branch: `ftyp` box type at offset 4.
- `Makefile` — optional, pkg-config-auto-detected libavif linkage
  (`AVIF=0` or a missing pkg-config entry builds the original feature set).
- `README.md`, `CHANGELOG.md` — documentation.

No PNG/JPEG code path was modified.

## How the lossless guarantee works

1. Recognize the ISOBMFF container via its `ftyp` box and validate the
   `avif`/`avis` compatible brands with `avifPeekCompatibleFileType`.
2. Parse and decode the image with libavif, preserving its exact YUV format,
   bit depth, range and alpha plane (no RGB conversion).
3. Re-encode with libaom through libavif at `AVIF_QUALITY_LOSSLESS`
   (color and alpha), keeping every pixel format parameter identical.
4. Decode the result again and compare every YUV/alpha sample byte for byte;
   any mismatch aborts and the original file is kept.
5. The new payload is only written when it is strictly smaller. All work
   happens in separate memory buffers; the mapped file is touched once, with
   a single contiguous `memmove`, so a failure can never leave a torn file
   (write-nothing / effectively-atomic replacement semantics).

Unsupported subformats print diagnostics and leave the file untouched:

- non-`avif`/`avis` ftyp brands,
- AVIF image sequences (`imageCount != 1`, e.g. the `avis` brand),
- progressive images,
- truncated or corrupt containers (libavif parse/decode error string),
- `-f` fast mode skips recompression.

## Build

The repository entry point is the Makefile. libavif is optional and picked up
through pkg-config; when absent the handler compiles to an explicit
"not compiled in" skip:

```sh
# with AVIF support (this environment uses a static libavif+libaom prefix)
export PKG_CONFIG_PATH=/tmp/pkgcfg
make

# original build, zero AVIF dependencies
make AVIF=0
```

Both GCC and Clang build clean with the repo's `-Wall -Werror -Wextra` flags.
The AVIF-enabled binary links libavif/libaom statically here, so its runtime
`NEEDED` entries are unchanged from the baseline.

## Verification

`run_acceptance.sh` drives the built `leanify` binary over every fixture and
checks the four acceptance criteria. Latest run: **35 passed, 0 failed**
(see `acceptance_report.txt`; working artifacts under `acceptance/`).

- Lossless identity: ffmpeg decode of before/after to raw frames, `cmp`
  per fixture, plus a separate RGBA alpha-plane check and a 10-bit YUV check.
- Size never increases: asserted for every fixture; already-optimal and
  lossy sources whose lossless re-encode would be larger are kept verbatim.
- Failure never corrupts: sequence, truncated and lossy inputs remain
  byte-identical (checksum comparison) with diagnostic messages.
- Zero regression: PNG/JPEG output is byte-identical to the `AVIF=0`
  baseline binary (`/tmp/leanify_baseline`).

Fixtures in `fixtures/` cover lossless YUV444/YUV420 8-bit, 10-bit, alpha,
 a fast-encoded (recompressable) source, XMP-padded input, a lossy source,
 a real 3-frame AVIS sequence, a truncated file and trailing-junk input.

## Screen recordings (segmented, titled)

Generated from `rec/seg*.sh` via `rec/make_casts.py` (asciicast v2) and
rendered with `agg`:

- `rec/seg1.gif` — build and optional dependency wiring.
- `rec/seg2.gif` — lossless recompression, pixel verification, no growth.
- `rec/seg3.gif` — unsupported/broken inputs: diagnostics, file untouched.
- `rec/seg4.gif` — acceptance suite and PNG/JPEG zero-regression proof.

Raw asciicasts are alongside them in `rec/casts/`.
