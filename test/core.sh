#!/bin/bash
# The reader core on the Agon, built by agondev and by acc, against the PC.
#
# test/host/reftool.c is portable: built for the Agon, it runs in the
# emulator with its inputs and outputs in files on the card, and every mode
# must answer exactly as the PC build answers the same inputs. A compiler
# that gets the core wrong shows up here -- as agondev's clang did, loading
# `n = *p++` from after the increment, which no PC test could see.
set -uo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SCRATCH=$(mktemp -d)
trap 'rm -rf "$SCRATCH"' EXIT
source "$ROOT/test/emu.sh"

AGONDEV=${AGONDEV:-$HOME/agondev}
# MODES=<modes> runs only those; KEEP=<dir> keeps both sides' outputs there.
MODES=${MODES:-info norm lookup search from titles contents views layout79 layout39}
COMPILERS=${COMPILERS:-agondev acc}
KEEP=${KEEP:-}
ACC=${ACC:-$HOME/code/acc/bin/acc}
export PATH=$AGONDEV/bin:$PATH

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
    echo "SKIP  core on the Agon: no emulator at $EMU"

    exit 0
fi
make -s -C "$ROOT" dat host > /dev/null || { echo "FAIL  build"; exit 1; }
DAT=$ROOT/out/mos.dat
TOOL=$ROOT/out/host/reftool

# The inputs: every key, every title, and words and prefixes to search for.
Q=$SCRATCH/q
mkdir -p "$Q"
(cd "$ROOT/build" && python3 - "$Q" <<'PY'
import os, random, sys
sys.path.insert(0, '.')
from tests.agon_docs import the_book, load_rules
from refbuild import keys, render
q = sys.argv[1]
b = the_book()
ix = keys.build(b, load_rules())
distinct = sorted({k.key for k in ix.keys})
titles = [render.agon_bytes(t.title).decode('latin-1') for t in b.topics]
rnd = random.Random(5)
lookups = distinct + [k[:n] for k in rnd.sample(distinct, 150) for n in (2, 4) if len(k) > n]
lookups += ['vdu 23,27,&21', '23;27;0x21', 'VDU 23,0,&A0,1;6', 'rst 10', 'hotkey$12', 'xyzzy', 'mos_f']
words = [w for t in titles for w in t.split()]
searches = rnd.sample(words, 120) + rnd.sample(titles, 30) + ['sprite frame', 'the s', 'zz']
def write(name, lines):
    with open(os.path.join(q, name), 'w', encoding='latin-1', newline='\r\n') as f:
        f.write('\n'.join(lines) + '\n')
write('norm.txt', distinct + titles)
write('lookup.txt', lookups)
write('search.txt', searches)
write('from.txt', lookups[::7])
PY
) || { echo "FAIL  inputs"; exit 1; }

# The PC's answers.
P=$SCRATCH/pc
mkdir -p "$P"
for m in norm lookup search from; do
    tr -d '\r' < "$Q/$m.txt" | "$TOOL" "$DAT" "$m" > "$P/$m.txt"
done
"$TOOL" "$DAT" titles > "$P/titles.txt"
"$TOOL" "$DAT" info > "$P/info.txt"
"$TOOL" "$DAT" contents > "$P/contents.txt"
"$TOOL" "$DAT" views > "$P/views.bin"
"$TOOL" "$DAT" layout 79 > "$P/layout79.txt"
"$TOOL" "$DAT" layout 39 > "$P/layout39.txt"

# build_agondev <dir>: reftool.bin by agondev's own makefile, in a staged tree.
build_agondev() {
    mkdir -p "$1/src"
    cp "$ROOT"/src/core/*.c "$ROOT"/src/core/*.h "$ROOT/test/host/reftool.c" "$1/src/"
    printf 'NAME=reftool\nLDHAS_ARG_PROCESSING=0\nLDHAS_EXIT_HANDLER=0\ninclude $(shell agondev-config --makefile)\nCFLAGS += %s\n' \
        "${AGONDEV_FIX--mllvm -twoaddr-reschedule=false}" > "$1/Makefile"
    make -s -C "$1" > /dev/null 2>&1 && echo "$1/bin/reftool.bin"
}

# build_acc <dir>: reftool.bin by acc, a file at a time, then linked.
build_acc() {
    local objs=() f o
    mkdir -p "$1"
    for f in "$ROOT"/src/core/*.c "$ROOT/test/host/reftool.c"; do
        o=$1/$(basename "$f" .c).o
        "$ACC" -c "$f" -I"$ROOT/src/core" -o "$o" > /dev/null || return 1
        objs+=("$o")
    done
    "$ACC" "${objs[@]}" -o "$1/reftool.bin" > /dev/null && echo "$1/reftool.bin"
}

# on_agon <reftool.bin> <name>: run every mode in the emulator; compare.
on_agon() {
    local bin=$1 name=$2 sd m
    sd=$(emu_card "$ROOT/bin/ref.bin" "$DAT")
    cp "$bin" "$sd/bin/reftool.bin"
    mkdir -p "$sd/q" "$sd/o"
    cp "$Q"/*.txt "$sd/q/"
    for m in $MODES; do
        case $m in
            norm|lookup|search|from)
                printf 'reftool /config/ref/docs/mos.dat %s -i /q/%s.txt -o /o/%s.txt\r\n' "$m" "$m" "$m" ;;
            info|titles|contents) printf 'reftool /config/ref/docs/mos.dat %s -o /o/%s.txt\r\n' "$m" "$m" ;;
            views) printf 'reftool /config/ref/docs/mos.dat views -o /o/views.bin\r\n' ;;
            layout*) printf 'reftool /config/ref/docs/mos.dat layout %s -o /o/%s.txt\r\n' "${m#layout}" "$m" ;;
        esac
    done > "$sd/autoexec.txt"
    emu_run "$sd" 1200 > "$SCRATCH/$name.console"
    for m in $MODES; do
        if [ "$m" = views ]; then
            check "$name on the Agon: every view, byte for byte" \
                  "$(md5sum < "$sd/o/views.bin" 2>/dev/null)" "$(md5sum < "$P/views.bin")"
        else
            check "$name on the Agon: $m as on the PC" \
                  "$(tr -d '\r' < "$sd/o/$m.txt" 2>/dev/null | md5sum)" "$(md5sum < "$P/$m.txt")"
        fi
    done
    if [ -n "$KEEP" ]; then
        mkdir -p "$KEEP/$name"
        cp -r "$sd/o/." "$KEEP/$name/"
        cp "$P"/* "$KEEP/"
        cp "$SCRATCH/$name.console" "$KEEP/$name/console.txt"
    fi
    rm -rf "$sd"
}

make -s -C "$ROOT" agon > /dev/null 2>&1
if [[ " $COMPILERS " == *" agondev "* ]]; then
    bin=$(build_agondev "$SCRATCH/agondev") || { echo "FAIL  reftool by agondev"; exit 1; }
    on_agon "$bin" agondev
fi
if [[ " $COMPILERS " != *" acc "* ]]; then
    :
elif [ -x "$ACC" ]; then
    bin=$(build_acc "$SCRATCH/acc") || { echo "FAIL  reftool by acc"; status=1; }
    [ -n "${bin:-}" ] && on_agon "$bin" acc
else
    echo "SKIP  acc: none at $ACC"
fi

exit $status
