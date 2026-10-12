#!/bin/bash
# md2ref on the Agon against Markdown made to make each per-item step pay
# (test/stress.py): each case compiled on the emulator that counts cycles,
# and held to a budget of cycles a byte of its own. Each step there was once
# worse than n log n -- anchors and titles looked up one by one, sorts by
# inserting, a scan to the end for every [ or * that never closes -- and
# such a step shows as many times its case's budget, where these are about
# a quarter over what md2ref takes now.
#
#   test/stress.sh                 # MD2REF_BIN=<md2ref.bin> to time another
set -uo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SCRATCH=$(mktemp -d)
trap 'rm -rf "$SCRATCH"' EXIT
source "$ROOT/test/emu.sh"
CYCLES_EMU=${CYCLES_EMU:-$HOME/fab-agon-emulator-2037657}

declare -A BUDGET=(
    [anchors]=6000 [common]=4800 [keys]=13500 [prefix]=5200 [repeats]=5000
    [run]=13000 [span]=9300 [spans]=10500 [table]=3600 [unclosed]=5700
)

if [ ! -x "$CYCLES_EMU/agon-cli-emulator" ]; then
    echo "SKIP  stress: no emulator that counts at $CYCLES_EMU"

    exit 0
fi
if [ -z "${MD2REF_BIN:-}" ]; then
    make -s -C "$ROOT" agon-md2ref-cycles > /dev/null || { echo "FAIL  build"; exit 1; }
    MD2REF_BIN=$ROOT/bin/md2ref-cycles/md2ref.bin
fi
python3 "$ROOT/test/stress.py" "$SCRATCH/cases"

sd=$(emu_card)
cp "$MD2REF_BIN" "$sd/bin/md2ref.bin"
: > "$sd/autoexec.txt"
cases=()
for d in "$SCRATCH"/cases/*/; do
    c=$(basename "$d")
    cases+=("$c")
    mkdir -p "$sd/s/$c"
    cp "$d"*.md "$sd/s/$c/"
    printf 'md2ref -w -o /o.dat /s/%s\r\n' "$c" >> "$sd/autoexec.txt"
done
console=$(EMU=$CYCLES_EMU emu_run "$sd" 1800)
mapfile -t counts < <(sed -n 's/.*Debug OUT(0x41): \([0-9]*\) CPU cycles.*/\1/p' <<< "$console")

status=0
for i in "${!cases[@]}"; do
    c=${cases[$i]}
    bytes=$(cat "$SCRATCH/cases/$c"/*.md | wc -c)
    n=${counts[$i]:-0}
    per=$(( n / bytes ))
    if [ "$n" -gt 0 ] && [ "$per" -le "${BUDGET[$c]}" ]; then
        printf 'PASS  stress %-9s %6d cycles a byte, budget %d\n' "$c" "$per" "${BUDGET[$c]}"
    else
        printf 'FAIL  stress %-9s %6d cycles a byte, budget %d\n' "$c" "$per" "${BUDGET[$c]}"
        status=1
    fi
done
[ $status = 0 ] || grep -i 'error' <<< "$console" | grep -v warning | head -5
exit $status
