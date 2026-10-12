#!/bin/bash
# md2ref on the Agon: bin/md2ref/md2ref.bin, run in a program's directory
# as `md2ref myprog`, compiles the pages there into ref-doc/myprog.dat --
# which must be byte for byte what the PC build writes from the same pages
# -- and ref, run in ref-doc, lists it and finds topics in it. Then all of
# agon-docs, 637 KB of Markdown in one folder: md2ref reads a page through
# a fixed window and holds only the outline, so it has to fit the Agon.
# Then, on an emulator that counts cycles (fab-agon-emulator 2037657, or
# CYCLES_EMU), agon-docs again with make agon-md2ref-cycles, to report how
# long the Agon takes: the emulator runs flat out, so MOS's clock, and the
# "Done in" line md2ref prints from it, say nothing of the Agon's time.
set -uo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SCRATCH=$(mktemp -d)
trap 'rm -rf "$SCRATCH"' EXIT
source "$ROOT/test/emu.sh"

DOC=$ROOT/build/tests/md2ref/doc
BIG=$(ls -d "$ROOT"/build/cache/agon-docs-*/docs 2>/dev/null | head -1)

status=0
check() {
    if [ "$2" = "$3" ]; then
        printf 'PASS  %s\n' "$1"
    else
        printf 'FAIL  %s\n      got:  %s\n      want: %s\n' "$1" "$2" "$3"
        status=1
    fi
}

if [ ! -x "$EMU/agon-cli-emulator" ]; then
    echo "SKIP  md2ref on the Agon: no emulator at $EMU"

    exit 0
fi
make -s -C "$ROOT" host agon agon-md2ref agon-md2ref-cycles > /dev/null 2>&1 || { echo "FAIL  build"; exit 1; }
"$ROOT/out/host/md2ref" -o "$SCRATCH/pc.dat" "$DOC" > /dev/null || { echo "FAIL  md2ref on the PC"; exit 1; }

sd=$(emu_card)
cp "$ROOT/bin/md2ref/md2ref.bin" "$sd/bin/"
mkdir -p "$sd/proj"
cp "$DOC"/*.md "$sd/proj/"
mkdir -p "$sd/proj/bad"
cp "$ROOT/build/tests/md2ref/bad/link.md" "$sd/proj/bad/"
printf 'index.md\r\napi.md\r\n' > "$sd/proj/pages.mdl"
printf '%s\r\n' 'cd /proj' 'md2ref myprog' \
                'cd ref-doc' 'ref -p' 'ref -p myprog -k strtoul' \
                'cd /proj' 'md2ref -e listed.txt listed @pages.mdl' \
                'Try md2ref -e errors.txt bad bad/link.md' 'cd /' > "$sd/autoexec.txt"
if [ -n "$BIG" ]; then
    mkdir -p "$sd/big" "$SCRATCH/big"
    cp "$BIG"/*.md "$BIG"/*/*.md "$sd/big/"
    cp "$BIG"/*.md "$BIG"/*/*.md "$SCRATCH/big/"
    "$ROOT/out/host/md2ref" -w -o "$SCRATCH/big.dat" "$SCRATCH/big" > /dev/null
    printf '%s\r\n' 'md2ref -w -o /big.dat /big' >> "$sd/autoexec.txt"
fi
console=$(emu_run "$sd" 900)

check "md2ref myprog on the Agon: reports what it wrote" \
      "$(grep -c '^md2ref: ref-doc/myprog.dat: 2 pages, 6 topics' <<< "$console")" 1
check "  and then how long it took, worded as zap and acc word it" \
      "$(grep -A1 '^md2ref: ref-doc/myprog.dat' <<< "$console" | grep -cE '^Done in [0-9]+\.[0-9]{2} seconds$')" 1
check "md2ref on the Agon: the document, byte for byte as on the PC" \
      "$(md5sum < "$sd/proj/ref-doc/myprog.dat" 2>/dev/null)" "$(md5sum < "$SCRATCH/pc.dat")"
check "ref, in ref-doc, lists the new document" \
      "$(grep -cE '^myprog +\. My Program' <<< "$console")" 1
check "md2ref -e @list on the Agon: the same document, and no errors in the file" \
      "$(md5sum < "$sd/proj/ref-doc/listed.dat" 2>/dev/null) $(wc -c < "$sd/proj/listed.txt" 2>/dev/null)" \
      "$(md5sum < "$SCRATCH/pc.dat") 0"
check "  an error goes in the file as on the console" \
      "$(tr -d '\r' < "$sd/proj/errors.txt" 2>/dev/null)" \
      "bad/link.md:5: error: cannot resolve link 'missing.md#here'"
check "ref finds a topic in it by a code span" \
      "$(grep -c '^strtol, strtoul, strtoll$' <<< "$console")" 1
if [ -n "$BIG" ]; then
    check "md2ref on the Agon: all of agon-docs, as on the PC" \
          "$(md5sum < "$sd/big.dat" 2>/dev/null)" "$(md5sum < "$SCRATCH/big.dat")"
else
    echo "SKIP  md2ref on the Agon: all of agon-docs: no build/cache (make dat)"
fi

# The time: the cycles from port 0x40's write to 0x41's, which the
# emulator prints on the host, over the eZ80's 18.432 MHz.
CYCLES_EMU=${CYCLES_EMU:-$HOME/fab-agon-emulator-2037657}
BUDGET=1880
if [ -z "$BIG" ]; then
    echo "SKIP  md2ref's time on the Agon: no build/cache (make dat)"
elif [ ! -x "$CYCLES_EMU/agon-cli-emulator" ]; then
    echo "SKIP  md2ref's time on the Agon: no emulator that counts at $CYCLES_EMU"
else
    sd=$(emu_card)
    cp "$ROOT/bin/md2ref-cycles/md2ref.bin" "$sd/bin/"
    mkdir -p "$sd/big"
    cp "$SCRATCH"/big/*.md "$sd/big/"
    printf '%s\r\n' 'md2ref -w -o /big.dat /big' > "$sd/autoexec.txt"
    timed=$(EMU=$CYCLES_EMU emu_run "$sd" 900)
    cycles=$(sed -n 's/.*Debug OUT(0x41): \([0-9]*\) CPU cycles.*/\1/p' <<< "$timed" | head -1)
    check "md2ref on the Agon: the emulator counts its cycles" \
          "$([ "${cycles:-0}" -gt 0 ] && echo counted)" counted
    check "  and the counting build writes the same document" \
          "$(md5sum < "$sd/big.dat" 2>/dev/null)" "$(md5sum < "$SCRATCH/big.dat")"
    # And the budget, in cycles a byte of Markdown, lowered as md2ref gets
    # faster: so a change that slows it is caught here.
    bytes=$(cat "$SCRATCH"/big/*.md | wc -c)
    per_byte=$(( ${cycles:-0} / bytes ))
    check "  within its budget of $BUDGET cycles a byte" \
          "$([ "${cycles:-0}" -gt 0 ] && [ "$per_byte" -le "$BUDGET" ] && echo within || echo "$per_byte")" within
    [ -n "$cycles" ] && printf 'TIME  all of agon-docs: %d cycles, %d a byte, %d.%02d s at 18.432 MHz, SD card reads not counted\n' \
        "$cycles" "$per_byte" $((cycles / 18432000)) $((cycles % 18432000 * 100 / 18432000))
fi
[ -n "${KEEP:-}" ] && cp "$sd/proj/ref-doc/myprog.dat" "$SCRATCH/pc.dat" "$KEEP/"
[ $status = 0 ] || printf '%s\n' "$console" | tail -30
exit $status
