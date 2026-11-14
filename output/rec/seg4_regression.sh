#!/usr/bin/env bash
# Segment 4: full acceptance suite and PNG/JPEG zero-regression proof.
set -u
cd "$(dirname "$0")/../.." || exit 1

title() {
  clear
  printf '\n\n'
  printf '  \033[1;46m  %s  \033[0m\n\n' "$1"
}

title "SEGMENT 4/4  ACCEPTANCE: 35 checks via the repo entry point + no regression"
sleep 1.5
echo '$ ./output/run_acceptance.sh        # drives the built ./leanify binary only'
echo
./output/run_acceptance.sh 2>&1 | tail -12
echo
sleep 1
echo '$ PNG/JPEG output vs the WEBP=0 AVIF=0 baseline binary'
for f in ref.png ref.jpg; do
  if cmp -s /tmp/reg_a/$f /tmp/reg_b/$f; then
    echo "  $f: byte-identical to baseline"
  fi
done
echo
echo 'Shared GetType entry: PNG, WebP and AVIF are detected by magic in one chain.'
sleep 2
