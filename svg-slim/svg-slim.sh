#!/usr/bin/env bash
# Render-gated lossless SVG slimming driver.
#
# leanify itself performs the DOM-level passes (comment removal, duplicate
# definition merge, capped precision trimming). Rasterization is not part of
# the C++ binary, so this driver closes the loop: it renders every candidate
# with a fixed resvg build and only keeps a candidate when the render is
# pixel-identical to the original and the file did not grow.
#
# Precision is searched upward from the requested cap: trimming harder makes a
# smaller file, but sub-pixel rasterization differences can appear at any
# digit count (and are non-monotonic), so every level is verified. If no
# trimmed candidate renders identically, the merge-only/legacy output is kept.
#
# usage: svg-slim.sh [--svg-precision 0..15] [--verbose] input.svg [out.svg]
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TOOL_DIR="$ROOT/svg-slim/test/tools"
RENDER_DIR="${SVG_RENDER_DIR:-/tmp/svgtool}"
PY="${SVG_PYTHON:-/tmp/venv/bin/python}"
[ -x "$PY" ] || PY=python3
export NODE_PATH="$RENDER_DIR/node_modules${NODE_PATH:+:$NODE_PATH}"

precision=3
verbose=0
files=()
while [ $# -gt 0 ]; do
  case "$1" in
    --svg-precision) precision="$2"; shift 2 ;;
    -v|--verbose) verbose=1; shift ;;
    -h|--help)
      grep '^#' "$0" | sed 's/^# \{0,1\}//' | head -16; exit 0 ;;
    *) files+=("$1"); shift ;;
  esac
done

if [ "${#files[@]}" -lt 1 ] || [ "${#files[@]}" -gt 2 ]; then
  echo "usage: svg-slim.sh [--svg-precision 0..15] [--verbose] input.svg [out.svg]" >&2
  exit 2
fi
if ! [[ "$precision" =~ ^[0-9]+$ ]] || [ "$precision" -gt 15 ]; then
  echo "error: --svg-precision must be an integer between 0 and 15, got '$precision'" >&2
  exit 2
fi

input="${files[0]}"
output="${files[1]:-$input}"
[ -f "$input" ] || { echo "error: input not found: $input" >&2; exit 2; }

workdir="$(mktemp -d)"
trap 'rm -rf "$workdir"' EXIT
# Keep an immutable pristine copy; when input==output we must never overwrite
# the source until the winning candidate has been fully render-verified.
cp "$input" "$workdir/original.svg"
input="$workdir/original.svg"
node "$TOOL_DIR/render.js" "$workdir/original.svg" "$workdir/original.png" || {
  echo "error: renderer rejected the input SVG; leaving file untouched" >&2; exit 3; }

before_size=$(stat -c %s "$input")

# Candidate 0: everything except precision trimming (comments, defs merge,
# default-attribute and metadata removal).
cp "$workdir/original.svg" "$workdir/cand.svg"
"$ROOT/leanify" $([ "$verbose" -eq 1 ] && echo -v) "$workdir/cand.svg" >/dev/null
node "$TOOL_DIR/render.js" "$workdir/cand.svg" "$workdir/cand.png" || exit 3
diff_json=$("$PY" "$TOOL_DIR/pixdiff.py" "$workdir/original.png" "$workdir/cand.png")
echo "$diff_json" | grep -q '"identical": true' || {
  echo "error: structural pass changed rendering; leaving file untouched" >&2; exit 4; }
best="$workdir/cand.svg"
best_size=$(stat -c %s "$best")
cp "$best" "$workdir/best.svg"

# Candidates 1..N: precision cap and every level up to the original precision.
for p in $(seq "$precision" 9); do
  cp "$workdir/original.svg" "$workdir/cand.svg"
  "$ROOT/leanify" $([ "$verbose" -eq 1 ] && echo -v) --svg-precision "$p" \
      "$workdir/cand.svg" >/dev/null
  node "$TOOL_DIR/render.js" "$workdir/cand.svg" "$workdir/cand.png" || exit 3
  diff_json=$("$PY" "$TOOL_DIR/pixdiff.py" "$workdir/original.png" "$workdir/cand.png")
  echo "$diff_json" | grep -q '"identical": true' || {
    [ "$verbose" -eq 1 ] && echo "precision $p: rejected ($diff_json)"; continue; }
  cand_size=$(stat -c %s "$workdir/cand.svg")
  if [ "$cand_size" -lt "$best_size" ]; then
    cp "$workdir/cand.svg" "$workdir/best.svg"
    best="$workdir/best.svg"; best_size="$cand_size"
    [ "$verbose" -eq 1 ] && echo "precision $p: accepted, $cand_size bytes"
  fi
done

# Hard guarantee: never grow the file.
[ "$best_size" -le "$before_size" ] || { echo "error: candidate grew; aborting" >&2; exit 4; }
cp "$best" "$output"
echo "$input: $before_size -> $best_size bytes ($(awk -v a="$before_size" -v b="$best_size" \
  'BEGIN {printf "%.2f%%", 100.0*(a-b)/a}') saved), render pixel-identical"
