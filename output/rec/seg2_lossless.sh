#!/usr/bin/env bash
# Segment 2: lossless WebP recompression, pixel verification and no growth.
set -u
cd "$(dirname "$0")/../.." || exit 1

title() {
  clear
  printf '\n\n'
  printf '  \033[1;46m  %s  \033[0m\n\n' "$1"
}

WORK=/tmp/rec_seg2
mkdir -p "$WORK"
cp output/fixtures/ref_bars_lossless.webp output/fixtures/ref_alpha_lossless.webp \
   output/fixtures/ref_photo_lossless.webp "$WORK"/

title "SEGMENT 2/4  LOSSLESS: parse RIFF/chunks, VP8L re-encode, verify pixels"
sleep 1.5
echo '$ leanify -v ref_bars_lossless.webp                   # RIFF + VP8L recompression'
before=$(stat -c%s "$WORK/ref_bars_lossless.webp")
./leanify -v "$WORK/ref_bars_lossless.webp"
after=$(stat -c%s "$WORK/ref_bars_lossless.webp")
echo "  $before -> $after bytes"
echo
sleep 1
echo '$ ffmpeg decode before/after to raw RGBA, then cmp    # byte-for-byte identical'
ffmpeg -hide_banner -loglevel error -y -i output/fixtures/ref_bars_lossless.webp \
       -f rawvideo -pix_fmt rgba "$WORK/before.rgba"
ffmpeg -hide_banner -loglevel error -y -i "$WORK/ref_bars_lossless.webp" \
       -f rawvideo -pix_fmt rgba "$WORK/after.rgba"
cmp "$WORK/before.rgba" "$WORK/after.rgba" && echo "  cmp: pixels identical (including every RGBA sample)"
echo
sleep 1
echo '$ leanify -v ref_alpha_lossless.webp                  # alpha + exact=1 preserved'
./leanify -v "$WORK/ref_alpha_lossless.webp" | sed -n '2,4p'
ffmpeg -hide_banner -loglevel error -y -i output/fixtures/ref_alpha_lossless.webp \
       -f rawvideo -pix_fmt rgba "$WORK/a_before.rgba"
ffmpeg -hide_banner -loglevel error -y -i "$WORK/ref_alpha_lossless.webp" \
       -f rawvideo -pix_fmt rgba "$WORK/a_after.rgba"
cmp "$WORK/a_before.rgba" "$WORK/a_after.rgba" && echo "  cmp: alpha image pixels identical"
echo
sleep 1
echo '$ leanify -v ref_photo_lossless.webp                  # already optimal -> never grows'
b=$(stat -c%s "$WORK/ref_photo_lossless.webp")
./leanify -v "$WORK/ref_photo_lossless.webp" | sed -n '2,4p'
a=$(stat -c%s "$WORK/ref_photo_lossless.webp")
echo "  $b -> $a bytes (kept verbatim, not larger)"
sleep 2
