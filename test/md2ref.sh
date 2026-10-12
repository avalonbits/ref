#!/bin/bash
# md2ref on the Agon: bin/md2ref/md2ref.bin compiles a folder of pages on
# the card, as a program's ref-doc/ would be, into a document that must be
# byte for byte what the PC build writes from the same pages -- and that ref
# then lists, and finds topics in. Then agon-docs' top-level pages, 105 KB
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
mkdir -p "$sd/ref-doc"
cp "$DOC"/*.md "$sd/ref-doc/"
printf '%s\r\n' 'md2ref -o /config/ref/docs/my.dat /ref-doc' \
                'ref -p' \
                'ref -p my -k strtoul' > "$sd/autoexec.txt"
if [ -n "$BIG" ]; then
    mkdir -p "$sd/big"
    cp "$BIG"/*.md "$sd/big/"
    "$ROOT/out/host/md2ref" -w -o "$SCRATCH/big.dat" "$BIG" > /dev/null
    printf '%s\r\n' 'md2ref -w -o /big.dat /big' >> "$sd/autoexec.txt"
fi
console=$(emu_run "$sd" 300)

check "md2ref on the Agon: reports what it wrote" \
      "$(grep -c '^md2ref: /config/ref/docs/my.dat: 2 pages, 6 topics' <<< "$console")" 1
check "md2ref on the Agon: the document, byte for byte as on the PC" \
      "$(md5sum < "$sd/config/ref/docs/my.dat" 2>/dev/null)" "$(md5sum < "$SCRATCH/pc.dat")"
check "ref lists the new document" "$(grep -cE '^ *my +My Program' <<< "$console")" 1
check "ref finds a topic in it by a code span" \
      "$(grep -c '^strtol, strtoul, strtoll$' <<< "$console")" 1
if [ -n "$BIG" ]; then
    check "md2ref on the Agon: 105 KB of agon-docs, as on the PC" \
          "$(md5sum < "$sd/big.dat" 2>/dev/null)" "$(md5sum < "$SCRATCH/big.dat")"
else
    echo "SKIP  md2ref on the Agon: 105 KB of agon-docs: no build/cache (make dat)"
fi
[ -n "${KEEP:-}" ] && cp "$sd/config/ref/docs/my.dat" "$SCRATCH/pc.dat" "$KEEP/"
[ $status = 0 ] || printf '%s\n' "$console" | tail -30
exit $status
