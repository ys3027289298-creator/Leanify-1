#!/usr/bin/env bash
# Segment 3: unsupported subformats and broken files get diagnostics, never corrupt.
set -u
cd "$(dirname "$0")/../.." || exit 1

title() {
  clear
  printf '\n\n'
  printf '  \033[1;46m  %s  \033[0m\n\n' "$1"
}

WORK=output/rec/demo3
mkdir -p "$WORK"
cp output/fixtures/ref_sequence.avif output/fixtures/ref_truncated.avif \
   output/fixtures/ref_lossy.avif "$WORK/"
( cd "$WORK" && md5sum *.avif ) > "$WORK/before.md5"

title "SEGMENT 3/4  FAILURE SAFETY: diagnostics + original never damaged"
sleep 1.5
echo '$ md5sum *.avif                        # checksums before processing'
sed 's/^/  /' "$WORK/before.md5"
echo
echo '$ ./leanify -v ref_sequence.avif       # AVIS image sequence = unsupported subformat'
( cd "$WORK" && ../../../leanify -v ref_sequence.avif ) 2>&1 | sed 's/^/  /'
echo
echo '$ ./leanify -v ref_truncated.avif      # truncated/corrupted container'
( cd "$WORK" && ../../../leanify -v ref_truncated.avif ) 2>&1 | sed 's/^/  /'
echo
echo '$ ./leanify -v ref_lossy.avif          # lossy source: lossless re-encode is larger -> keep'
( cd "$WORK" && ../../../leanify -v ref_lossy.avif ) 2>&1 | sed 's/^/  /'
echo
echo '$ md5sum -c before.md5                  # every failed/skipped file byte-identical'
( cd "$WORK" && md5sum -c before.md5 ) | sed 's/^/  /'
sleep 2
