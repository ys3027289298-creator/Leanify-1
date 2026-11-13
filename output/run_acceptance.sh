#!/usr/bin/env bash
# AVIF lossless recompression acceptance tests, driven through the repository's
# own entry point (the leanify binary). No source internals are referenced here.
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LEANIFY="$ROOT/leanify"
WORK="$ROOT/output/acceptance"
FIX="$ROOT/output/fixtures"

mkdir -p "$WORK"
pass=0
fail=0

check() {
  if [ "$1" = "$2" ]; then
    echo "PASS: $3"
    pass=$((pass + 1))
  else
    echo "FAIL: $3 ($1 != $2)"
    fail=$((fail + 1))
  fi
}

# Decode an AVIF to 16-bit RGB raw frames with ffmpeg for pixel comparison.
decode_rgb48() {
  ffmpeg -hide_banner -loglevel fatal -y -i "$1" -f rawvideo -pix_fmt rgb48le "$2" 2>/dev/null || true
}

for f in "$FIX"/*.avif; do
  name="$(basename "$f")"
  cp "$f" "$WORK/$name"
  before_size="$(stat -c%s "$WORK/$name")"

  "$LEANIFY" -v "$WORK/$name" > "$WORK/$name.log" 2>&1
  after_size="$(stat -c%s "$WORK/$name")"

  # Acceptance 2: file must never grow.
  check "$([ "$after_size" -le "$before_size" ] && echo ok || echo bad)" ok \
    "size not increased: $name ($before_size -> $after_size)"

  # Acceptance 1: pixels must be identical after the round trip.
  decode_rgb48 "$f" "$WORK/$name.orig.raw"
  decode_rgb48 "$WORK/$name" "$WORK/$name.new.raw"
  if cmp -s "$WORK/$name.orig.raw" "$WORK/$name.new.raw"; then
    check ok ok "pixel identical: $name"
  else
    check bad ok "pixel identical: $name"
  fi
done

# Alpha channel must survive losslessly as well.
ffmpeg -hide_banner -loglevel fatal -y -i "$FIX/ref_alpha.avif" -f rawvideo -pix_fmt rgba "$WORK/alpha.orig" 2>/dev/null
ffmpeg -hide_banner -loglevel fatal -y -i "$WORK/ref_alpha.avif" -f rawvideo -pix_fmt rgba "$WORK/alpha.new" 2>/dev/null
if cmp -s "$WORK/alpha.orig" "$WORK/alpha.new"; then
  check ok ok "alpha channel identical"
else
  check bad ok "alpha channel identical"
fi

# Acceptance 3: unsupported / broken inputs must stay byte-for-byte intact.
for name in ref_truncated.avif ref_sequence.avif; do
  if cmp -s "$FIX/$name" "$WORK/$name"; then
    check ok ok "failure leaves file untouched: $name"
  else
    check bad ok "failure leaves file untouched: $name"
  fi
done

# Diagnostic text must be present for unsupported subformats.
grep -q "Unsupported AVIF subformat" "$WORK/ref_sequence.avif.log" \
  && check ok ok "diagnostic for image sequence" || check bad ok "diagnostic for image sequence"
grep -qE "parse failed|decode failed" "$WORK/ref_truncated.avif.log" \
  && check ok ok "diagnostic for broken file" || check bad ok "diagnostic for broken file"

# Idempotency: a second run must not change an already-optimized file.
cp "$WORK/ref_bars.avif" "$WORK/ref_bars.run1.avif"
"$LEANIFY" "$WORK/ref_bars.run1.avif" > /dev/null 2>&1
cmp -s "$WORK/ref_bars.avif" "$WORK/ref_bars.run1.avif" \
  && check ok ok "idempotent on optimized AVIF" || check bad ok "idempotent on optimized AVIF"

# Fast mode must leave AVIF untouched.
cp "$FIX/ref_bars.avif" "$WORK/ref_bars_fast.avif"
"$LEANIFY" -f "$WORK/ref_bars_fast.avif" > /dev/null 2>&1
cmp -s "$FIX/ref_bars.avif" "$WORK/ref_bars_fast.avif" \
  && check ok ok "fast mode skips AVIF" || check bad ok "fast mode skips AVIF"

# Acceptance 4: PNG/JPEG output must match the baseline (libavif-disabled) build.
if [ -x /tmp/leanify_baseline ]; then
  mkdir -p "$WORK/base" "$WORK/cur"
  cp "$FIX/ref.png" "$FIX/ref.jpg" "$WORK/base/"
  cp "$FIX/ref.png" "$FIX/ref.jpg" "$WORK/cur/"
  /tmp/leanify_baseline "$WORK/base/ref.png" "$WORK/base/ref.jpg" > /dev/null 2>&1
  "$LEANIFY" "$WORK/cur/ref.png" "$WORK/cur/ref.jpg" > /dev/null 2>&1
  cmp -s "$WORK/base/ref.png" "$WORK/cur/ref.png" \
    && check ok ok "PNG zero regression" || check bad ok "PNG zero regression"
  cmp -s "$WORK/base/ref.jpg" "$WORK/cur/ref.jpg" \
    && check ok ok "JPEG zero regression" || check bad ok "JPEG zero regression"
fi

echo
echo "RESULT: $pass passed, $fail failed"
exit "$fail"
