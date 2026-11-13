#!/usr/bin/env bash
# Segment 4: repository acceptance entry point + PNG/JPEG zero regression.
set -u
cd "$(dirname "$0")/../.." || exit 1

title() {
  clear
  printf '\n\n'
  printf '  \033[1;46m  %s  \033[0m\n\n' "$1"
}

title "SEGMENT 4/4  ACCEPTANCE: repo entry point, PNG/JPEG zero regression"
sleep 1.5
echo '$ ls -la /tmp/leanify_baseline leanify   # baseline = AVIF=0 build, current = AVIF build'
ls -la /tmp/leanify_baseline leanify | awk '{print "  "$5"  "$9}'
echo
echo '$ diff <(/tmp/leanify_baseline ref.png ref.jpg) <(./leanify ref.png ref.jpg)   # empty = identical'
WORK=output/rec/demo4
mkdir -p "$WORK/base" "$WORK/cur"
cp output/fixtures/ref.png output/fixtures/ref.jpg "$WORK/base/"
cp output/fixtures/ref.png output/fixtures/ref.jpg "$WORK/cur/"
( cd "$WORK/base" && /tmp/leanify_baseline ref.png ref.jpg > out.log 2>&1 )
( cd "$WORK/cur" && ../../../../leanify ref.png ref.jpg > out.log 2>&1 )
if cmp -s "$WORK/base/ref.png" "$WORK/cur/ref.png" && cmp -s "$WORK/base/ref.jpg" "$WORK/cur/ref.jpg"; then
  echo '  (no diff) PNG and JPEG output byte-identical to baseline'
fi
echo
echo '$ ./output/run_acceptance.sh             # full acceptance suite shipped in output/'
./output/run_acceptance.sh 2>&1 | tail -6 | sed 's/^/  /'
sleep 2
