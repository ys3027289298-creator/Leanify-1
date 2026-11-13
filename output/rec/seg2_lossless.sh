#!/usr/bin/env bash
# Segment 2: lossless recompression, pixel identity and no-size-increase.
set -u
cd "$(dirname "$0")/../.." || exit 1

title() {
  clear
  printf '\n\n'
  printf '  \033[1;46m  %s  \033[0m\n\n' "$1"
}

WORK=output/rec/demo2
mkdir -p "$WORK"
cp output/fixtures/ref_fast_lossless.avif output/fixtures/ref_alpha.avif \
   output/fixtures/ref_10bit.avif output/fixtures/ref_bars.avif "$WORK/"

title "SEGMENT 2/4  LOSSLESS: recompress, verify pixels, never grow"
sleep 1.5
echo '$ ls -l *.avif                         # originals'
( cd "$WORK" && ls -l *.avif | awk '{print "  "$5"  "$9}' )
echo
echo '$ ffmpeg -i ref_fast_lossless.avif -f rawvideo rgb48 orig.raw   # reference pixels'
ffmpeg -hide_banner -loglevel fatal -y -i "$WORK/ref_fast_lossless.avif" \
       -f rawvideo -pix_fmt rgb48le "$WORK/orig.raw"
echo '  reference raw frame captured'
echo
echo '$ ./leanify -v *.avif'
( cd "$WORK" && ../../../leanify -v *.avif ) 2>&1 | sed 's/^/  /'
echo
echo '$ ffmpeg -i ref_fast_lossless.avif -f rawvideo rgb48 new.raw'
ffmpeg -hide_banner -loglevel fatal -y -i "$WORK/ref_fast_lossless.avif" \
       -f rawvideo -pix_fmt rgb48le "$WORK/new.raw"
echo '$ cmp orig.raw new.raw && echo PIXEL-IDENTICAL'
if cmp -s "$WORK/orig.raw" "$WORK/new.raw"; then
  echo '  PIXEL-IDENTICAL'
fi
sleep 2
