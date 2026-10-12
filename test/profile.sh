#!/bin/bash
# Where md2ref spends its cycles on the Agon, function by function.
#
#   test/profile.sh [pages...]     # default: all of agon-docs, flat
#
# Runs make agon-md2ref-cycles under a patched emulator
# (~/fab-agon-emulator-prof, or PROF_EMU) that charges each instruction's
# cycles to its address between the writes to IO ports 0x40 and 0x41, and
# writes them to $AGON_PROFILE. Each address goes to the function that
# holds it: the globals from the link map, the statics from each object's
# own symbols at its place in the map. Prints the total, the cycles per
# byte of Markdown, and the functions that take the most. FUNC=<name>
# also prints that function's disassembly, each instruction's cycles
# beside it; CALLERS=<name>[,<name>...], the functions that call each,
# busiest first.
set -uo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SCRATCH=$(mktemp -d)
trap 'rm -rf "$SCRATCH"' EXIT
source "$ROOT/test/emu.sh"
PROF_EMU=${PROF_EMU:-$HOME/fab-agon-emulator-prof}
TOP=${TOP:-40}

[ -x "$PROF_EMU/agon-cli-emulator" ] || { echo "no profiling emulator at $PROF_EMU"; exit 1; }
make -s -C "$ROOT" agon-md2ref-cycles > /dev/null || exit 1

mkdir -p "$SCRATCH/pages"
if [ $# -gt 0 ]; then
    cp "$@" "$SCRATCH/pages/"
else
    BIG=$(ls -d "$ROOT"/build/cache/agon-docs-*/docs 2>/dev/null | head -1)
    [ -n "$BIG" ] || { echo "no build/cache (make dat)"; exit 1; }
    cp "$BIG"/*.md "$BIG"/*/*.md "$SCRATCH/pages/"
fi
bytes=$(cat "$SCRATCH"/pages/*.md | wc -c)

sd=$(emu_card)
cp "$ROOT/bin/md2ref-cycles/md2ref.bin" "$sd/bin/"
mkdir -p "$sd/pages"
cp "$SCRATCH"/pages/*.md "$sd/pages/"
printf '%s\r\n' 'md2ref -w -o /out.dat /pages' > "$sd/autoexec.txt"
export AGON_PROFILE=$SCRATCH/profile.txt
console=$(EMU=$PROF_EMU emu_run "$sd" 1800)
cycles=$(sed -n 's/.*Debug OUT(0x41): \([0-9]*\) CPU cycles.*/\1/p' <<< "$console" | head -1)
[ -n "$cycles" ] && [ -s "$AGON_PROFILE" ] || { echo "no profile"; tail -5 <<< "$console"; exit 1; }

python3 - "$ROOT/bin/md2ref-cycles/md2ref.map" "$AGON_PROFILE" "$cycles" "$bytes" "$TOP" "${FUNC:-}" "${CALLERS:-}" "$ROOT/bin/md2ref-cycles/md2ref.bin" <<'EOF'
import re, subprocess, sys, os
mapfile, prof, cycles, nbytes, top = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5])
func = sys.argv[6]
callers, binary = sys.argv[7], sys.argv[8]
objs = []
nm = os.path.expanduser('~/agondev/bin/ez80-none-elf-nm')
syms = {}
section = None
for line in open(mapfile):
    m = re.match(r'^(\.\w+)', line)
    if m:
        section = m.group(1)
    m = re.match(r'^ \.text\s+0x([0-9a-f]+)\s+0x[0-9a-f]+\s+(\S+\.o)$', line)
    if m and section == '.text':
        base, obj = int(m.group(1), 16), m.group(2)
        objs.append((base, obj))
        if os.path.exists(obj):
            out = subprocess.run([nm, obj], capture_output=True, text=True).stdout
            for l in out.splitlines():
                p = l.split()
                if len(p) == 3 and p[1] in 'tT':
                    name = p[2].lstrip('_')
                    if p[1] == 't':     # a static: which file's
                        name += ' (' + os.path.basename(obj)[:-2] + ')'
                    syms[base + int(p[0], 16)] = name
        continue
    m = re.match(r'^\s+0x([0-9a-f]+)\s+(_\w+)$', line)
    if m and section == '.text':
        syms.setdefault(int(m.group(1), 16), m.group(2).lstrip('_'))
addrs = sorted(syms)
import bisect
by = {}
at = {}
for l in open(prof):
    a, c = l.split()
    a, c = int(a, 16), int(c)
    at[a] = c
    i = bisect.bisect_right(addrs, a) - 1
    name = syms[addrs[i]] if i >= 0 else '?'
    by[name] = by.get(name, 0) + c
total = sum(by.values())
print(f'{cycles} cycles, {nbytes} bytes: {cycles / nbytes:.0f} cycles a byte '
      f'({cycles / 18432000:.1f} s at 18.432 MHz, SD card reads not counted)')
print(f'{"function":40} {"cycles":>14} {"%":>6} {"c/byte":>8}')
for name, c in sorted(by.items(), key=lambda kv: -kv[1])[:top]:
    print(f'{name:40} {c:14} {100 * c / total:6.2f} {c / nbytes:8.1f}')
if func:
    start = next(a for a in addrs if syms[a] == func or syms[a].split(' (')[0] == func)
    end = addrs[addrs.index(start) + 1]
    base, obj = max((b, o) for b, o in objs if b <= start)
    dis = subprocess.run([os.path.expanduser('~/agondev/bin/ez80-none-elf-objdump'), '-d',
                          '--start-address=%d' % (start - base), '--stop-address=%d' % (end - base), obj],
                         capture_output=True, text=True).stdout
    print()
    for l in dis.splitlines():
        m = re.match(r'^\s+([0-9a-f]+):\s+(.*)$', l)
        if m:
            a = base + int(m.group(1), 16)
            c = at.get(a, 0)
            print(f'{c:12} {c / nbytes:7.1f}  {a:06x}  {m.group(2)}')
for callee in filter(None, callers.split(',')):
    target = next(a for a in addrs if syms[a] == callee or syms[a].split(' (')[0] == callee)
    dis = subprocess.run([os.path.expanduser('~/agondev/bin/ez80-none-elf-objdump'), '-D', '-b', 'binary',
                          '-m', 'ez80-adl', '--adjust-vma=0x40000', binary],
                         capture_output=True, text=True).stdout
    by_fn = {}
    for l in dis.splitlines():
        m = re.match(r'^\s+([0-9a-f]+):\s+(?:[0-9a-f]{2} )+\s*\t(call|jp)\s+(?:\w+,)?0x([0-9a-f]+)', l)
        if m and int(m.group(3), 16) == target:
            a = int(m.group(1), 16)
            i = bisect.bisect_right(addrs, a) - 1
            fn = syms[addrs[i]] if i >= 0 else '?'
            by_fn[fn] = by_fn.get(fn, 0) + at.get(a, 0)
    print(f'\ncalls to {callee}, by the cycles of the calls themselves:')
    for fn, c in sorted(by_fn.items(), key=lambda kv: -kv[1])[:12]:
        if c:
            print(f'{c:12} {fn}')
EOF
