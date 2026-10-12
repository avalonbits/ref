#!/bin/bash
# That md2ref's work grows no faster than n log n: each case of
# test/stress.py at two sizes, the second twice the first, through the PC
# build without sanitizers under valgrind, which counts the instructions
# run -- the same count every time. Twice the input may take a little over
# twice the instructions, as a sort's n log n does; a step that costs n
# squared takes four times.
#
#   test/growth.sh                 # MD2REF=<md2ref> to count another
set -uo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SCRATCH=$(mktemp -d)
trap 'rm -rf "$SCRATCH"' EXIT
LIMIT=${LIMIT:-2.5}                 # the most twice the input may cost
SMALL=${SMALL:-2}

command -v valgrind > /dev/null || { echo "SKIP  growth: no valgrind"; exit 0; }
if [ -z "${MD2REF:-}" ]; then
    make -s -C "$ROOT" out/host/md2ref-plain || { echo "FAIL  build"; exit 1; }
    MD2REF=$ROOT/out/host/md2ref-plain
fi
python3 "$ROOT/test/stress.py" "$SCRATCH/a" "$SMALL"
python3 "$ROOT/test/stress.py" "$SCRATCH/b" "$(python3 -c "print($SMALL * 2)")"

refs() {
    valgrind --tool=cachegrind --cache-sim=no --cachegrind-out-file=/dev/null \
        "$MD2REF" -w -o "$SCRATCH/out.dat" "$1" 2>&1 >/dev/null \
        | sed -n 's/.*I *refs: *\([0-9,]*\).*/\1/p' | tr -d ,
}

status=0
for d in "$SCRATCH"/a/*/; do
    c=$(basename "$d")
    a=$(refs "$SCRATCH/a/$c")
    b=$(refs "$SCRATCH/b/$c")
    ba=$(cat "$SCRATCH/a/$c"/*.md | wc -c)
    bb=$(cat "$SCRATCH/b/$c"/*.md | wc -c)
    # Per byte, as the second input is about, not exactly, twice the first.
    ratio=$(python3 -c "print('%.2f' % (2 * ($b / $bb) / ($a / $ba)))" 2>/dev/null || echo 99)
    if [ -n "$a" ] && [ -n "$b" ] && python3 -c "import sys; sys.exit(0 if $ratio <= $LIMIT else 1)"; then
        printf 'PASS  growth %-9s twice the input, %s times the work\n' "$c" "$ratio"
    else
        printf 'FAIL  growth %-9s twice the input, %s times the work (at most %s)\n' "$c" "$ratio" "$LIMIT"
        status=1
    fi
done
exit $status
