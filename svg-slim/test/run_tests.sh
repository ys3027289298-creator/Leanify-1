#!/usr/bin/env bash
# End-to-end verification for the SVG lossless slimming passes.
# Produces:
#   out/<name>/<mode>.svg          optimized files
#   out/<name>/<mode>.png          renders (before + after)
#   out/report.json                machine-readable comparison data
#   out/verbose.log                skip/merge records
set -u

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TESTDIR="$ROOT/svg-slim/test"
OUT="$TESTDIR/out"
RENDER_DIR="/tmp/svgtool"
export NODE_PATH="$RENDER_DIR/node_modules"
PY="$([ -x /tmp/venv/bin/python ] && echo /tmp/venv/bin/python || echo python3)"
MODES=("legacy" "merge" "gated")

rm -rf "$OUT"
mkdir -p "$OUT"

declare -a ROWS
failures=0

json_escape() { sed 's/\\/\\\\/g; s/"/\\"/g'; }

for input in "$TESTDIR"/inputs/*.svg; do
  name="$(basename "$input" .svg)"
  mkdir -p "$OUT/$name"
  cp "$input" "$OUT/$name/original.svg"
  before_size=$(stat -c %s "$input")

  node "$TESTDIR/tools/render.js" "$OUT/$name/original.svg" "$OUT/$name/original.png" || {
    echo "render failed for $name (original)"; failures=$((failures + 1)); continue; }

  for mode in "${MODES[@]}"; do
    work="$OUT/$name/$mode.svg"
    cp "$input" "$work"
    case "$mode" in
      legacy) "$ROOT/leanify" "$work" >>"$OUT/verbose.log" 2>&1 ;;
      merge)  "$ROOT/leanify" -v "$work" >>"$OUT/verbose.log" 2>&1 ;;
      gated)  "$TESTDIR/../svg-slim.sh" --svg-precision 3 "$work" "$work" \
                >>"$OUT/verbose.log" 2>&1 ;;
    esac
    after_size=$(stat -c %s "$work")

    node "$TESTDIR/tools/render.js" "$work" "$OUT/$name/$mode.png"
    diff_json=$("$PY" "$TESTDIR/tools/pixdiff.py" "$OUT/$name/original.png" "$OUT/$name/$mode.png")

    grew="false"; [ "$after_size" -gt "$before_size" ] && grew="true"
    saved=$((before_size - after_size))
    pct=$(awk -v a="$before_size" -v b="$after_size" 'BEGIN {printf "%.3f", 100.0 * (a - b) / a}')
    ROWS+=("{\"file\":\"$name\",\"mode\":\"$mode\",\"before_bytes\":$before_size,\"after_bytes\":$after_size,\"saved_bytes\":$saved,\"saved_pct\":$pct,\"grew\":$grew,$(echo "$diff_json" | sed 's/^{//; s/}$//')}")

    if [ "$grew" = true ]; then echo "SIZE REGRESSION $name/$mode"; failures=$((failures + 1)); fi
    identical=$(echo "$diff_json" | sed -n 's/.*"identical": \([a-z]*\).*/\1/p')
    if [ "$identical" != "true" ] && [ "$mode" != "ungated3_demo" ]; then
      echo "PIXEL DIFF $name/$mode: $diff_json"; failures=$((failures + 1)); fi
  done
done

# Ungated demonstration: a hard 3-digit cap without the render gate. This is
# kept deliberately separate to document why the gate is mandatory; its
# measured pixel error is reported, and the row is excluded from the
# zero-difference assertions below.
for input in "$TESTDIR"/inputs/02-precision.svg "$TESTDIR"/inputs/01-gradient-symbol.svg; do
  name="$(basename "$input" .svg)-ungated3"
  mkdir -p "$OUT/$name"
  cp "$input" "$OUT/$name/original.svg"
  before_size=$(stat -c %s "$input")
  node "$TESTDIR/tools/render.js" "$OUT/$name/original.svg" "$OUT/$name/original.png"
  work="$OUT/$name/ungated3.svg"
  cp "$input" "$work"
  "$ROOT/leanify" --svg-precision 3 "$work" >>"$OUT/verbose.log" 2>&1
  after_size=$(stat -c %s "$work")
  node "$TESTDIR/tools/render.js" "$work" "$OUT/$name/ungated3.png"
  diff_json=$("$PY" "$TESTDIR/tools/pixdiff.py" "$OUT/$name/original.png" "$OUT/$name/ungated3.png")
  saved=$((before_size - after_size))
  pct=$(awk -v a="$before_size" -v b="$after_size" 'BEGIN {printf "%.3f", 100.0 * (a - b) / a}')
  ROWS+=("{\"file\":\"$name\",\"mode\":\"ungated3_demo\",\"before_bytes\":$before_size,\"after_bytes\":$after_size,\"saved_bytes\":$saved,\"saved_pct\":$pct,\"grew\":false,$(echo "$diff_json" | sed 's/^{//; s/}$//')}")
done

printf '[%s]\n' "$(IFS=,; echo "${ROWS[*]}")" > "$OUT/report.json"
"$PY" -m json.tool "$OUT/report.json" > "$OUT/report.pretty.json"

# Persisted before/after artifacts for the required gradient + symbol sample.
demo="$OUT/01-gradient-symbol/demo"
mkdir -p "$demo"
cp "$TESTDIR/inputs/01-gradient-symbol.svg" "$demo/before.svg"
node "$TESTDIR/tools/render.js" "$demo/before.svg" "$demo/before.png"
"$TESTDIR/../svg-slim.sh" --svg-precision 3 "$demo/before.svg" "$demo/after.svg" >/dev/null
node "$TESTDIR/tools/render.js" "$demo/after.svg" "$demo/after.png"
cmp "$demo/before.png" "$demo/after.png" || {
  echo "GRADIENT SAMPLE RENDER MISMATCH"; failures=$((failures + 1)); }

# Legacy mode on an already-minimal file must not change any content byte;
# the only allowed difference is the trailing newline that pugixml omits.
strip_nl() { tr -d '\n' < "$1"; }
if ! diff -q <(strip_nl "$OUT/04-already-min/original.svg") \
             <(strip_nl "$OUT/04-already-min/legacy.svg") >/dev/null; then
  echo "LEGACY CONTENT MISMATCH on 04-already-min"; failures=$((failures + 1))
fi

echo "---- report ----"
cat "$OUT/report.pretty.json"
echo "failures=$failures"
exit "$failures"
