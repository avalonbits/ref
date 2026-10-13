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
# A page of what the copy of text into markup (copytext.s on the Agon, C
# on the PC) must get exactly right: runs of spaces before, inside and
# after words; capitals, codes and links inside words; words at the very
# end of a block; letters it does not copy as they are; code with spaces
# and tabs; a word longer than any kept -- compiled with and without -f.
mkdir -p "$SCRATCH/edge" "$sd/edge"
python3 - "$SCRATCH/edge/index.md" <<'PY'
import sys
page = ('# Edge  Cases\n\n'
        'one  two   three    four end\n  lead and trail  \n\n'
        'HelloWorld ABC aBc x_y_z $var snake_case foo*bar*baz _a_b_ end\n\n'
        'pre[mid](#edge-cases)post  pre**bold**post `a  b  c` ``x ` y``  q\n\n'
        'caf\u00e9 na\u00efve \u2014 dash\u2192arrow \u00a0nbsp word\n\n'
        + 'w' * 300 + ' after\n\n'
        '```\n    x  =  1\n\tTab\tbed  Code\nEND\n```\n\n'
        '| Head  One | `two  code` |\n|---|---|\n| *em  cell* | plain   cell |\n\n'
        '## Sub  Head *with*  em\n\n!!! note "Note  Title"\n    body  text\n\nlast\n')
open(sys.argv[1], 'w').write(page)
PY
cp "$SCRATCH/edge/index.md" "$sd/edge/"
"$ROOT/out/host/md2ref" -w -o "$SCRATCH/edge.dat" "$SCRATCH/edge" > /dev/null
"$ROOT/out/host/md2ref" -w -f -o "$SCRATCH/edge_f.dat" "$SCRATCH/edge" > /dev/null
printf '%s\r\n' 'md2ref -w -o /edge.dat /edge' 'md2ref -w -f -o /edge_f.dat /edge' >> "$sd/autoexec.txt"
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
check "md2ref on the Agon: text copied as on the PC, its words with it" \
      "$(md5sum < "$sd/edge.dat" 2>/dev/null) $(md5sum < "$sd/edge_f.dat" 2>/dev/null)" \
      "$(md5sum < "$SCRATCH/edge.dat") $(md5sum < "$SCRATCH/edge_f.dat")"
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
BUDGET=630
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
# The heap: make agon-md2ref-heap's peak on all of agon-docs, against a
# budget that leaves room on the Agon (333 KB of heap, and agondev's
# allocator gives none back to a block grown or cut, so the holes count
# too): so a change that holds more, or longer, is caught here.
HEAP_BUDGET=230000
HEAP_BUDGET_F=280000        # -f: every word in the search's index
if [ -n "$BIG" ] && make -s -C "$ROOT" agon-md2ref-heap > /dev/null; then
    for flags in "" "-f "; do
        budget=$HEAP_BUDGET
        [ -n "$flags" ] && budget=$HEAP_BUDGET_F
        sd=$(emu_card)
        cp "$ROOT/bin/md2ref-heap/md2ref.bin" "$sd/bin/md2ref.bin"
        mkdir -p "$sd/big"
        cp "$SCRATCH"/big/*.md "$sd/big/"
        printf 'md2ref -w %s-o /big.dat /big\r\n' "$flags" > "$sd/autoexec.txt"
        peak=$(emu_run "$sd" 900 | sed -n 's/^peak \([0-9]*\) bytes.*/\1/p' | head -1)
        check "md2ref ${flags}on the Agon: its heap within $budget bytes on agon-docs" \
              "$([ "${peak:-0}" -gt 0 ] && [ "$peak" -le "$budget" ] && echo within || echo "${peak:-none}")" within
        [ -n "$peak" ] && printf 'HEAP  all of agon-docs, md2ref %s- a peak of %d bytes\n' "${flags:-without -f }" "$peak"
    done
fi
[ -n "${KEEP:-}" ] && cp "$sd/proj/ref-doc/myprog.dat" "$SCRATCH/pc.dat" "$KEEP/"
[ $status = 0 ] || printf '%s\n' "$console" | tail -30
exit $status
