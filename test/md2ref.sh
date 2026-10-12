#!/bin/bash
# md2ref on the Agon: bin/md2ref/md2ref.bin, run in a program's directory
# as `md2ref myprog`, compiles the pages there into ref-doc/myprog.dat --
# which must be byte for byte what the PC build writes from the same pages
# -- and ref, run in ref-doc, lists it and finds topics in it. Then agon-docs' top-level pages, 105 KB
# of Markdown: what md2ref holds at once has to fit the Agon's memory.
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
make -s -C "$ROOT" host agon agon-md2ref > /dev/null 2>&1 || { echo "FAIL  build"; exit 1; }
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
    mkdir -p "$sd/big"
    cp "$BIG"/*.md "$sd/big/"
    "$ROOT/out/host/md2ref" -w -o "$SCRATCH/big.dat" "$BIG" > /dev/null
    printf '%s\r\n' 'md2ref -w -o /big.dat /big' >> "$sd/autoexec.txt"
fi
console=$(emu_run "$sd" 300)

check "md2ref myprog on the Agon: reports what it wrote" \
      "$(grep -c '^md2ref: ref-doc/myprog.dat: 2 pages, 6 topics' <<< "$console")" 1
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
    check "md2ref on the Agon: 105 KB of agon-docs, as on the PC" \
          "$(md5sum < "$sd/big.dat" 2>/dev/null)" "$(md5sum < "$SCRATCH/big.dat")"
else
    echo "SKIP  md2ref on the Agon: 105 KB of agon-docs: no build/cache (make dat)"
fi
[ -n "${KEEP:-}" ] && cp "$sd/proj/ref-doc/myprog.dat" "$SCRATCH/pc.dat" "$KEEP/"
[ $status = 0 ] || printf '%s\n' "$console" | tail -30
exit $status
