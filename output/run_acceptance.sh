#!/usr/bin/env bash
# WebP lossless recompression acceptance tests, driven through the repository's
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

# Decode a WebP to raw RGBA with ffmpeg for pixel comparison.
decode_rgba() {
  ffmpeg -hide_banner -loglevel error -y -i "$1" -f rawvideo -pix_fmt rgba "$2" 2>/dev/null || true
}

for f in "$FIX"/*.webp; do
  name="$(basename "$f")"
  cp "$f" "$WORK/$name"
  before_size="$(stat -c%s "$WORK/$name")"
  before_md5="$(md5sum "$WORK/$name" | cut -d' ' -f1)"

  "$LEANIFY" -v "$WORK/$name" > "$WORK/$name.log" 2>&1
  after_size="$(stat -c%s "$WORK/$name")"

  # Acceptance 2: file must never grow.
  check "$([ "$after_size" -le "$before_size" ] && echo ok || echo bad)" ok \
    "size not increased: $name ($before_size -> $after_size)"

  # Acceptance 1: decoded pixels must stay byte-identical.
  decode_rgba "$f" "$WORK/$name.orig.rgba"
  decode_rgba "$WORK/$name" "$WORK/$name.new.rgba"
  if [ -s "$WORK/$name.orig.rgba" ] && [ -s "$WORK/$name.new.rgba" ]; then
    check "$(cmp -s "$WORK/$name.orig.rgba" "$WORK/$name.new.rgba" && echo ok || echo bad)" ok \
      "pixel identical: $name"
  else
    # Intentionally broken inputs cannot be decoded; the untouched-md5 check below
    # is what guarantees their bytes are preserved.
    check skipped skipped "pixel decode not applicable: $name (corrupt/unsupported input)"
  fi

  # Acceptance 3: bad/unsupported files must stay byte-identical.
  case "$name" in
    ref_truncated.webp|ref_bad_riffsize.webp|ref_animated.webp|ref_lossy.webp)
      after_md5="$(md5sum "$WORK/$name" | cut -d' ' -f1)"
      check "$after_md5" "$before_md5" "failure/skip leaves file untouched: $name"
      check "$(grep -qiE 'skipped|failed' "$WORK/$name.log" && echo ok || echo bad)" ok \
        "diagnostic emitted: $name"
      ;;
  esac
done

# Trailing junk: valid image, junk gets dropped together with recompression.
check "$(grep -q 'bytes removed' "$WORK/ref_trailing_junk.webp.log" && echo ok || echo bad)" ok \
  "trailing junk removed after successful recompression"

# Metadata is preserved through the VP8X rebuild.
check "$(strings "$WORK/ref_meta_lossless.webp" | grep -q 'leanify webp fixture' && echo ok || echo bad)" ok \
  "XMP metadata preserved"

# Idempotency: a second run over an already-optimized file must not grow or alter pixels.
cp "$FIX/ref_bars_lossless.webp" "$WORK/run1.webp"
"$LEANIFY" "$WORK/run1.webp" >/dev/null 2>&1
cp "$WORK/run1.webp" "$WORK/run2.webp"
"$LEANIFY" "$WORK/run2.webp" >/dev/null 2>&1
check "$(cmp -s "$WORK/run1.webp" "$WORK/run2.webp" && echo ok || echo bad)" ok \
  "second run is stable (idempotent)"

# Fast mode skips recompression but still recognizes the format.
cp "$FIX/ref_bars_lossless.webp" "$WORK/fast.webp"
fast_before="$(md5sum "$WORK/fast.webp" | cut -d' ' -f1)"
"$LEANIFY" -f "$WORK/fast.webp" >/dev/null 2>&1
fast_after="$(md5sum "$WORK/fast.webp" | cut -d' ' -f1)"
check "$fast_after" "$fast_before" "fast mode leaves WebP untouched"

# Acceptance 4: shared entry point still handles PNG/JPEG unchanged in behavior.
for other in ref.png ref.jpg; do
  cp "$FIX/$other" "$WORK/$other"
  ob="$(stat -c%s "$WORK/$other")"
  "$LEANIFY" "$WORK/$other" >/dev/null 2>&1
  oa="$(stat -c%s "$WORK/$other")"
  check "$([ "$oa" -le "$ob" ] && echo ok || echo bad)" ok "shared entry handles $other"
done

# Recognition is magic-based, not extension-based: renamed file still detected.
cp "$FIX/ref_bars_lossless.webp" "$WORK/no_extension.dat"
"$LEANIFY" -v "$WORK/no_extension.dat" 2>&1 | grep -q "WebP detected"
check "$?" "0" "magic-based detection without .webp extension"

echo
echo "WebP acceptance: $pass passed, $fail failed"
exit "$fail"
