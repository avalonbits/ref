#!/bin/bash
# ref on the Agon, in fab-agon-emulator (MOS 3.0.2), driven as hub's and
# zap's harnesses drive it -- see test/emu.sh -- never by typing into stdin.
#
#   test/run.sh            every check
#
# The interactive checks run the scripted build (REF_SCRIPT): keys come from
# /ref.keys on the card, and ref writes what it drew to /ref.out, which the
# checks read back. The release build is checked through -p and its exit
# codes; under hub, through a stand-in for ade's F1 (test/agon/hubf1); on the
# full emulator, whose VDP is the real firmware, what ref drew is read back
# from the VDP itself. Timings are taken paced, from MOS's own clock.
set -uo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SCRATCH=$(mktemp -d)
trap 'rm -rf "$SCRATCH"' EXIT
source "$ROOT/test/emu.sh"

HUB=${HUB:-$HOME/code/devel}
AGONDEV=${AGONDEV:-$HOME/agondev}
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
has() { grep -qF -- "$2" <<< "$1" && echo yes || echo no; }

if [ ! -x "$EMU/agon-cli-emulator" ]; then
    echo "SKIP  emulator: none at $EMU"

    exit 0
fi

# --- builds ---
make -s -C "$ROOT" dat > /dev/null || { echo "FAIL  ref.dat"; exit 1; }
make -s -C "$ROOT" agon agon-script > /dev/null 2>&1 || { echo "FAIL  build"; exit 1; }
RELEASE=$ROOT/bin/ref.bin
SCRIPTED=$ROOT/bin/script/ref.bin

# scripted <command> <keys>: run the scripted ref with these keys (one a
# line, as input.c reads them); the transcript, \r removed.
scripted() {
    local sd
    sd=$(emu_card "$SCRIPTED")
    printf '%s\r\n' "$1" > "$sd/autoexec.txt"
    printf '%s\n' "$2" > "$sd/ref.keys"
    emu_run "$sd" 120 > /dev/null
    tr -d '\r' < "$sd/ref.out" 2>/dev/null
    rm -rf "$sd"
}

# dump <transcript> <name>: the screen ref drew at dump:<name>.
dump() {
    awk -v want="== $2" '$0 == want { p = 1; next } /^== |^key |^exit |^lit / { p = 0 } p' <<< "$1"
}

# --- the release build: printing, and exit codes ---
sd=$(emu_card "$RELEASE")
printf 'ref mos -p -k vdu 23 27 3\r\n' > "$sd/autoexec.txt"
out=$(emu_run "$sd" 60)
rm -rf "$sd"
check "ref -p -k prints the topic" \
      "$(has "$out" 'VDU 23, 27, 3, x; y;: Draw current bitmap on screen at pixel position x, y')" yes
check "  all of it" "$(has "$out" 'commands instead.')" yes

sd=$(emu_card "$RELEASE")
printf 'ref -v mos\r\n' > "$sd/autoexec.txt"
out=$(emu_run "$sd" 60)
rm -rf "$sd"
version=$(sed -n 's/^#define REF_VERSION "\(.*\)"/\1/p' "$ROOT/src/viewer/version.h")
commit=$(sed -n 's/^commit \(.\{12\}\).*/\1/p' "$ROOT/SOURCE")
check "ref -v says its version, and which agon-docs" \
      "$(grep -a '^ref ' <<< "$out")" "ref $version, mos: $commit (2026-07-15)"

# Each command under Try, so autoexec carries on after a failure, and its
# result shown. 0 when something was found, 100 when not; never 1, 4 or 5.
sd=$(emu_card "$RELEASE")
{
    printf 'Try ref mos -p -k mos_fopen\r\nShow Try$ReturnCode\r\n'
    printf 'Try ref mos -k xyzzy\r\nShow Try$ReturnCode\r\n'
    printf 'Try ref mos -p xyzzy plugh\r\nShow Try$ReturnCode\r\n'
    printf 'Try ref -f /none.dat cd\r\nShow Try$ReturnCode\r\n'
} > "$sd/autoexec.txt"
out=$(emu_run "$sd" 60)
rm -rf "$sd"
codes=$(grep -a 'Try$ReturnCode' <<< "$out" | grep -oE '[0-9]+$' | tr '\n' ' ')
check "results: found 0; nothing, nothing searched, no file: 100" "$codes" "0 100 100 100 "
check "  saying why when nothing matched" "$(has "$out" 'ref: nothing for xyzzy')" yes
check "  and when there is no ref.dat" "$(has "$out" 'ref: cannot read /none.dat')" yes

# --- the documents: a card with two, mos and a copy of it as zap, and a
# file that is not one ---
sd=$(emu_card "$RELEASE")
cp "$sd/config/ref/docs/mos.dat" "$sd/config/ref/docs/zap.dat"
printf 'not a document\r\n' > "$sd/config/ref/docs/notes.txt"
{
    printf 'ref -p\r\n'
    printf 'Try ref nosuch cd\r\nShow Try$ReturnCode\r\n'
    printf 'Try ref Bad! cd\r\nShow Try$ReturnCode\r\n'
    printf 'Try ref -f /config/ref/docs/zap.dat -p -k cd\r\nShow Try$ReturnCode\r\n'
} > "$sd/autoexec.txt"
out=$(emu_run "$sd" 60)
rm -rf "$sd"
check "ref -p lists the documents in /config/ref/docs, by name, with their titles" \
      "$(grep -a '  Community Agon' <<< "$out" | tr -s ' ' | tr '\n' ',')" \
      "mos Community Agon Platform documentation,zap Community Agon Platform documentation,"
check "  and nothing that is not a .dat file" "$(grep -ac '^notes ' <<< "$out")" 0
check "  a document that is not there says so" "$(has "$out" 'ref: no document nosuch; ref lists them')" yes
check "  as does a name that cannot be one" "$(has "$out" 'ref: Bad! is not a document')" yes
check "  both return 100, and -f reads a document by its path" \
      "$(grep -a 'Try$ReturnCode' <<< "$out" | grep -oE '[0-9]+$' | tr '\n' ' ')" "100 100 0 "
check "  which found cd there" "$(grep -ac '^Change current directory\.$' <<< "$out")" 1

sd=$(emu_card "$SCRIPTED")
cp "$sd/config/ref/docs/mos.dat" "$sd/config/ref/docs/zap.dat"
printf 'ref\r\n' > "$sd/autoexec.txt"
printf '%s\n' dump:shelf enter esc down enter c:q > "$sd/ref.keys"
emu_run "$sd" 60 > /dev/null
t=$(tr -d '\r' < "$sd/ref.out")
rm -rf "$sd"
check "ref alone shows the documents to choose from" \
      "$(dump "$t" shelf | head -2 | sed 's/ *$//' | tr -s ' ' | tr '\n' ',')" \
      " mos Community Agon Platform documentation, zap Community Agon Platform documentation,"
check "  Enter opens one at its contents, Esc comes back to the documents" \
      "$(grep -E '^(shelf|toc) ' <<< "$t" | cut -d' ' -f1-4 | tr '\n' ',')" \
      "shelf row 0 of,toc row 0 node,shelf row 0 of,shelf row 1 of,toc row 0 node,"
check "  and q quits" "$(grep -c '^exit 0$' <<< "$t")" 1

# --- a topic, its links and its history ---
t=$(scripted "ref mos -k vdu 23 27 3" "dump:open
tab
dump:tab
enter
dump:link
back
dump:back
alt+right
dump:forward")
check "ref -k vdu 23 27 3 opens Draw current bitmap" \
      "$(dump "$t" open | head -1)" \
      "VDU 23, 27, 3, x; y;: Draw current bitmap on screen at pixel position x, y"
check "  Tab puts the cursor on its first link" "$(grep -c '^view topic 502 top 0 lines 22 cur 4:20$' <<< "$t")" 2
check "  Enter follows it" "$(dump "$t" link | head -1)" "Bitmap plots (PLOT codes &E8-&EF)"
check "  the status line says where it is" \
      "$(dump "$t" link | tail -1 | sed 's/  *[0-9]*%$//')" \
      " VDU 25: PLOT commands > Bitmap plots (PLOT codes &E8-&EF)"
check "  Backspace goes back" "$(dump "$t" back | head -1)" \
      "VDU 23, 27, 3, x; y;: Draw current bitmap on screen at pixel position x, y"
check "  and puts the cursor back on the link it followed" \
      "$(grep -A1 '^key back$' <<< "$t" | grep -c '^view topic 502 top 0 lines 22 cur 4:20$')" 1
check "  Alt+Right goes forward again" "$(dump "$t" forward | head -1)" \
      "Bitmap plots (PLOT codes &E8-&EF)"

# --- a hypertext: headings, links and finding with CTRL ---
t=$(scripted "ref mos -k copper" "down
enter
ctrl+down
ctrl+down
ctrl+down
dump:third
ctrl+up
dump:back")
check "Ctrl+Down puts the cursor on each heading in turn, at the top of the screen" \
      "$(grep -oE 'cur [0-9]+:0' <<< "$t" | sed -n '2,4p' | tr '\n' ' ')" "cur 34:0 cur 55:0 cur 57:0 "
check "  the third is Command 0" "$(dump "$t" third | head -1)" "Command 0: Create a palette"
check "  Ctrl+Up goes back to the one before" "$(grep -c 'top 55 lines 169 cur 55:0 scrolled -2$' <<< "$t")" 1
t=$(scripted "ref mos -k VDU 23,0,&A0,1;6" "ctrl+right
ctrl+right
ctrl+left
ctrl+f
text:buffer 12
enter
dump:found
ctrl+n
ctrl+p
ctrl+f
back
back
back
back
back
back
back
back
back
text:zzzz
enter
dump:notfound")
check "Ctrl+Right and Ctrl+Left step through the links" \
      "$(grep -oE 'cur [0-9]+:[0-9]+' <<< "$t" | sed -n '2,4p' | tr '\n' ' ')" "cur 30:25 cur 44:37 cur 30:25 "
check "Ctrl+F finds words, spaces and all, and shows them" \
      "$(grep -m1 "^find " <<< "$t")" "find 'buffer 12' at 87:30"
check "  the match is on the screen" "$(dump "$t" found | grep -c 'buffer 12')" 2
check "  Ctrl+N goes to the next, Ctrl+P back" \
      "$(grep -o "at [0-9]*:" <<< "$t" | tr '\n' ' ')" "at 87: at 91: at 87: "
t2=$(scripted "ref mos -k VDU 23,0,&A0,1;6" "ctrl+f
text:COMBINING THE ABOVE
enter")
check "  ignoring case: COMBINING THE ABOVE finds Combining the above" "$(grep -c "^find 'COMBINING THE ABOVE' at " <<< "$t2")" 1
check "  and a word that is not there says so" "$(dump "$t" notfound | grep -c '^ Not found: zzzz$')" 1

# A heading that wraps is lit whole, from whichever of its lines the cursor
# is on, and moving off it leaves nothing lit behind.
t=$(scripted "ref mos -k vdu 23 27 6" "dump:heading
down
dump:second
down
down
dump:off")
check "a heading that wraps is two lines" "$(dump "$t" heading | sed -n 2p | grep -c '^buffer ID')" 1
check "  the cursor on its first lights both" "$(grep '^lit heading:' <<< "$t")" "lit heading: 0 1"
check "  and on its second" "$(grep '^lit second:' <<< "$t")" "lit second: 0 1"
check "  and off the heading, nothing stays lit" "$(grep '^lit off:' <<< "$t")" "lit off:"
# The FAQ's list of questions: links that wrap are lit whole, too.
keys="end"; for i in $(seq 8); do keys="$keys
ctrl+left"; done
t=$(scripted "ref mos -k frequently" "$keys
dump:wrapped
down
dump:tail
down
dump:next
up
ctrl+left
dump:prev")
check "a link that wraps is lit on both its lines" "$(grep '^lit wrapped:' <<< "$t")" "lit wrapped: 11 12"
check "  from its second line too" "$(grep '^lit tail:' <<< "$t")" "lit tail: 11 12"
check "  and the next is lit whole when the cursor gets to it" "$(grep '^lit next:' <<< "$t")" "lit next: 13 14"
check "  Ctrl+Left from a second line goes to the link before, not its own start" \
      "$(grep '^lit prev:' <<< "$t")" "lit prev: 10"
t=$(scripted "ref mos -k vdu 23 27 3" "dump:one")
check "the blank line after a lit heading is not lit" "$(grep '^lit one:' <<< "$t")" "lit one: 0"

# --- scrolling a long topic ---
t=$(scripted "ref mos -k VDU 23,0,&A0,1;6" "dump:top
pgdn
dump:page
end
dump:end
home
dump:home")
check "a buffered command by its sequence: Command 6" \
      "$(dump "$t" top | head -1)" "Command 6: Conditionally call a buffer"
check "  PgDn scrolls a page: the text rows, less one" "$(grep -c '^view topic 543 top 22 ' <<< "$t")" 1
check "  End shows the end, at 100%" "$(dump "$t" end | tail -1 | grep -oE '[0-9]+%$')" "100%"
check "  Home the top again" "$(dump "$t" home | head -1)" "Command 6: Conditionally call a buffer"

# --- several matches: a list ---
t=$(scripted "ref mos mos_f" "dump:list
down
down
enter
dump:chosen")
check "ref mos_f lists the keys that start with it" \
      "$(dump "$t" list | head -3 | sed 's/ *$//')" \
      " mos_fclose  0x0B: mos_fclose  - What is the MOS API
 mos_feof  0x0E: mos_feof  - What is the MOS API
 mos_fgetc  0x0C: mos_fgetc  - What is the MOS API"
check "  Enter opens the one chosen" "$(dump "$t" chosen | head -1)" "0x0C: mos_fgetc"

# --- search ---
t=$(scripted "ref mos -s sprite frame" "dump:results
enter
dump:first")
first=$(dump "$t" results | head -1 | sed 's/^ //; s/  - .*//')
check "ref -s searches the text, titles first" "$first" "VDU 23, 27, 5: Clear frames in current sprite"
check "  Enter opens the first" "$(dump "$t" first | head -1)" "VDU 23, 27, 5: Clear frames in current sprite"
t=$(scripted "ref mos transparency" "dump:results")
check "a lookup that finds no key searches the text instead" \
      "$(dump "$t" results | tail -1 | sed 's/ *$//')" " Search: transparency"
check "  and lists what it found" "$(dump "$t" results | sed '$d' | grep -c '^ [A-Z]')" 3

# --- the contents, and find as you type ---
t=$(scripted "ref mos" "dump:contents
down
down
down
down
down
down
down
down
enter
dump:api
c:t
dump:back
c:/
text:mos_fo
dump:find
enter
dump:found")
check "ref mos shows its contents, by the nav" \
      "$(dump "$t" contents | head -6 | sed 's/ *$//')" \
      "- Welcome & getting started
  + Start here
  + FAQ
  + Theory of operation
  + Programming
- MOS"
check "  Enter on a page opens it" "$(dump "$t" api | head -1)" "What is the MOS API"
check "  t goes back to the contents, at that page" "$(grep -c '^toc row 8 ' <<< "$t")" 2
check "  / finds as you type: keys first" \
      "$(dump "$t" find | sed -n 2p | sed 's/ *$//')" " mos_fopen  0x0A: mos_fopen  - What is the MOS API"
check "  Enter opens it" "$(dump "$t" found | head -1)" "0x0A: mos_fopen"

t=$(scripted "ref mos -k mos_fopen" "c:t
dump:deep")
check "t from a topic deep in a closed page opens its way to it" \
      "$(dump "$t" deep | grep -c '0x0A: mos_fopen')" 1

# --- Backspace goes back to where a topic was opened from ---
t=$(scripted "ref mos" "down
down
down
down
down
down
down
down
enter
back
dump:start")
check "Backspace from a topic opened from the contents goes back to them" \
      "$(grep -A1 '^back to contents$' <<< "$t" | grep -c '^toc row 8 ')" 1
check "  the start page, as it was" "$(dump "$t" start | head -1 | sed 's/ *$//')" "- Welcome & getting started"
t=$(scripted "ref mos mos_f" "down
down
enter
back")
check "  from a list, to the list, on the row chosen" \
      "$(grep -A1 '^back to list$' <<< "$t" | grep -c '^pick row 2 of 9 ')" 1
t=$(scripted "ref mos" "c:/
text:mos_fo
down
enter
back")
check "  from find, to find, with what was typed" \
      "$(grep -A1 '^back to find$' <<< "$t" | grep -c "^pick row 1 of [0-9]* top 0 query 'mos_fo'$")" 1

# --- help ---
t=$(scripted "ref mos -k cd" "c:?
esc")
check "? shows the keys" "$(dump "$t" help | sed -n 3p)" "Reading a topic: the cursor"

# --- under hub, as ade's F1 runs it ---
if [ -f "$HUB/build/hub.bin" ] && [ -f "$HUB/build/lib/agondev/libhub.a" ]; then
    make -s -C "$ROOT/test/agon/hubf1" HUB="$HUB" > /dev/null 2>&1
    sd=$(emu_card "$SCRIPTED")
    cp "$HUB/build/hub.bin" "$sd/mos/"
    cp "$ROOT/test/agon/hubf1/bin/hubf1.bin" "$HUB/build/test/fail.bin" "$sd/bin/"
    printf 'esc\n' > "$sd/ref.keys"
    printf 'Set Hub$NoPause 1\r\nhubf1 -c fail\r\nhubf1 vdu 23 27 3\r\nhubf1 xyzzy\r\n' > "$sd/script.txt"
    printf 'hub -f /script.txt\r\n' > "$sd/autoexec.txt"
    out=$(emu_run "$sd" 120)
    t=$(tr -d '\r' < "$sd/ref.out")
    rm -rf "$sd"
    results=$(grep -a '^f1 back' <<< "$out" | grep -oE 'result [0-9]+' | tr '\n' ',')
    check "under hub: the control fails 19, ref shows a topic 0, and finds nothing 100" \
          "$results" "result 19,result 0,result 100,"
    check "  ref ran as the user program, on vdu 23 27 3" "$(grep -c '^view topic 502 ' <<< "$t")" 1
else
    echo "SKIP  hub: no hub build at $HUB"
fi

# --- on the real VDP: what ref drew, read back ---
# vdp_screen <transcript> <name>: what the VDP showed at vdp:<name>.
vdp_screen() {
    awk -v want="== vdp $2" '$0 == want { p = 1; next } /^== |^key |^exit |^lit / { p = 0 } p' <<< "$1"
}
# same <transcript> <name> <margin> <width>: does the VDP show, inside the
# margins, what ref meant to draw? The header, the top row, is not in the
# dump (the transcript names it); the status line, the last row, is left out:
# the VDP cannot read back its last row (nor its last column).
same() {
    local drawn read
    drawn=$(dump "$1" "$2" | sed '$d' | sed 's/ *$//')
    read=$(vdp_screen "$1" "$2" | sed '1d;$d' | cut -c$(($3 + 1))-$(($3 + $4)) | sed 's/ *$//')
    [ "$(md5sum <<< "$drawn")" = "$(md5sum <<< "$read")" ] && echo yes || echo no
}
# margins_blank <transcript> <name> <margin> <width>: nothing in the margins.
margins_blank() {
    vdp_screen "$1" "$2" | sed '1d;$d' | awk -v m="$3" -v w="$4" '
        { left = substr($0, 1, m); right = substr($0, m + w + 1)
          sub(/\.$/, "", right)            # the last column: unreadable
          if (left ~ /[^ ]/ || right ~ /[^ ]/) bad = 1 }
        END { print bad ? "no" : "yes" }'
}
if [ -x "$EMU/fab-agon-emulator" ]; then
    for mode in 3 8 0; do
        if [ "$mode" = 8 ]; then margin=1; width=38; else margin=2; width=76; fi
        sd=$(emu_card "$SCRIPTED")
        printf 'VDU 22 %s\r\nref mos -k vdu 23 27 3\r\n' "$mode" > "$sd/autoexec.txt"
        printf 'tab\ndump:screen\nvdp:screen\nesc\n' > "$sd/ref.keys"
        emu_run "$sd" 300 full > /dev/null
        t=$(tr -d '\r' < "$sd/ref.out" 2>/dev/null)
        rm -rf "$sd"
        check "mode $mode: the VDP shows what ref drew, nothing wrapped or scrolled" \
              "$(same "$t" screen "$margin" "$width")" yes
        check "  and the topic is there" "$(has "$(vdp_screen "$t" screen)" 'Draw current')" yes
        check "  laid out to the screen's width less a margin each side" \
              "$(dump "$t" screen | sed '$d' | awk '{ if (length > w) w = length } END { print w }')" "$width"
        # From column 0; its text ends a space short of the last column,
        # which is painted in its colour rather than printed in.
        check "  and the status line edge to edge" \
              "$(dump "$t" screen | tail -1 | awk '{ print length }')" "$((width + 2 * margin - 2))"
        check "  the margins are blank" "$(margins_blank "$t" screen "$margin" "$width")" yes
    done

    # The status line, edge to edge on the real VDP: its first cell and its
    # last -- the bottom right, painted rather than printed in -- are in
    # its colour; a text row's edges, in the margins, are not.
    sd=$(emu_card "$SCRIPTED")
    printf 'VDU 22 3\r\nref mos -k vdu 23 27 3\r\n' > "$sd/autoexec.txt"
    printf '%s\n' pixel:0,29 pixel:79,29 pixel:0,5 pixel:79,5 pixel:0,0 pixel:79,0 vdp:top esc \
        > "$sd/ref.keys"
    emu_run "$sd" 300 full > /dev/null
    t=$(tr -d '\r' < "$sd/ref.out" 2>/dev/null)
    rm -rf "$sd"
    check "mode 3: the status line runs from the first column to the last" \
          "$(grep '^pixel' <<< "$t" | head -4 | tr '\n' ' ')" \
          "pixel 0,29 aaaaaa pixel 79,29 aaaaaa pixel 0,5 000000 pixel 79,5 000000 "
    check "  and so does the header" "$(grep '^pixel' <<< "$t" | tail -2 | tr '\n' ' ')" \
          "pixel 0,0 aaaaaa pixel 79,0 aaaaaa "
    check "  which is the wiki's title, centred" \
          "$(vdp_screen "$t" top | head -1 | sed 's/\.$//; s/ *$//')" \
          "                     Community Agon Platform documentation"

    # Scrolling a line or two moves the rows on the VDP rather than redrawing
    # them: the screen must still be what ref meant, in a topic, the
    # contents and a list.
    sd=$(emu_card "$SCRIPTED")
    printf 'VDU 22 3\r\nref mos -k VDU 23,0,&A0,1;6\r\n' > "$sd/autoexec.txt"
    { for i in $(seq 27); do echo down; done; } > "$sd/ref.keys"
    printf '%s\n' down down down dump:down vdp:down up dump:up vdp:up \
        c:t pgdn down down dump:toc vdp:toc esc \
        c:/ text:mos dump:find down vdp:find0 dump:find0 \
        pgdn down down dump:list vdp:list esc esc >> "$sd/ref.keys"
    emu_run "$sd" 300 full > /dev/null
    t=$(tr -d '\r' < "$sd/ref.out" 2>/dev/null)
    rm -rf "$sd"
    check "Down at the bottom of the screen scrolls the rows rather than redrawing them" \
          "$(grep -c 'scrolled 1$' <<< "$t")" 3
    check "  and the VDP shows the topic three lines on" "$(same "$t" down 2 76)" yes
    check "  and Up, the cursor still on the screen, moves it and scrolls nothing" \
          "$(grep -A1 '^key up$' <<< "$t" | grep -c 'scrolled')" 0
    check "  as drawn" "$(same "$t" up 2 76)" yes
    check "the contents, moved past the page's end, as drawn" "$(same "$t" toc 2 76)" yes
    check "find's list, its cursor moved, as drawn" "$(same "$t" find0 2 76)" yes
    check "  and scrolled past its end, as drawn" "$(same "$t" list 2 76)" yes
else
    echo "SKIP  real VDP: no full emulator"
fi

# --- memory: the heap's peak, measured by zap's zmalloc shim ---
make -s -C "$ROOT" agon-heap > /dev/null 2>&1
sd=$(emu_card "$ROOT/bin/heap/ref.bin")
printf 'ref mos -k vdu 23 27 3\r\n' > "$sd/autoexec.txt"
# A topic, the contents, the largest topic through find, then a search.
printf 'c:t\ndown\nright\nc:/\ntext:vdu variables\nenter\nend\nc:/\ntext:sprite\nenter\nesc\n' \
    > "$sd/ref.keys"
out=$(emu_run "$sd" 60)
t=$(tr -d '\r' < "$sd/ref.out")
rm -rf "$sd"
grep -aA4 '^peak' <<< "$out" | sed 's/^/HEAP  /'
peak=$(grep -a '^peak' <<< "$out" | awk '{ print $2 }')
check "the session reached the largest topic" "$(grep -c '^view topic 660 ' <<< "$t")" 2
check "the heap's peak is under 100 KB" "$([ "${peak:-999999}" -lt 102400 ] && echo yes)" yes

# --- speed, in the machine ---
sd=$(emu_card "$RELEASE")
printf 'ref mos -T\r\n' > "$sd/autoexec.txt"
out=$(emu_run "$sd" 120 cli paced)
rm -rf "$sd"
grep -aE '^(open|lookup|search|keys|read|view) ' <<< "$out" | sed 's/^/TIME  /'
cs() { grep -a "^$1 " <<< "$out" | awk '{ print int($2) }'; }
check "a lookup takes under 5 cs" "$([ "$(cs lookup)" -lt 5 ] && echo yes)" yes
check "a search takes under 10 cs" "$([ "$(cs search)" -lt 10 ] && echo yes)" yes
check "the largest topic opens in under 20 cs" "$([ "$(cs view)" -lt 20 ] && echo yes)" yes

exit $status
