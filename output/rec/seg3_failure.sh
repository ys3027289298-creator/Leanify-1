#!/usr/bin/env bash
# Segment 3: broken / unsupported inputs get diagnostics and stay untouched.
set -u
cd "$(dirname "$0")/../.." || exit 1

title() {
  clear
  printf '\n\n'
  printf '  \033[1;46m  %s  \033[0m\n\n' "$1"
}

WORK=/tmp/rec_seg3
mkdir -p "$WORK"
cp output/fixtures/ref_truncated.webp output/fixtures/ref_bad_riffsize.webp \
   output/fixtures/ref_animated.webp output/fixtures/ref_lossy.webp "$WORK"/

title "SEGMENT 3/4  FAILURE SAFETY: diagnostic error, original file untouched"
sleep 1.5
for f in ref_truncated.webp ref_bad_riffsize.webp ref_animated.webp ref_lossy.webp; do
  echo "\$ leanify -v $f"
  before=$(md5sum "$WORK/$f" | cut -d' ' -f1)
  ./leanify -v "$WORK/$f" | sed -n '2,4p'
  after=$(md5sum "$WORK/$f" | cut -d' ' -f1)
  if [ "$before" = "$after" ]; then
    echo "  md5 unchanged: original file preserved"
  else
    echo "  ERROR: file was modified"
  fi
  echo
  sleep 1
done
echo 'All failure paths build the replacement in a separate buffer and write only'
echo 'one smaller contiguous payload, so a torn/corrupt output is impossible.'
sleep 2
