# Running ref in fab-agon-emulator, the way zap's and hub's harnesses do:
# a release build of the emulator run from its own directory, a card seeded
# with MOS's own files, the commands in autoexec.txt -- never typed into
# stdin -- stdin held open with a fifo, both output streams captured, and
# the emulator stopped as soon as MOS prints its prompt after them.
#
# Source this; it defines:
#   emu_card              a fresh card, with ref.bin and mos.dat installed;
#                         prints its directory
#   emu_run <card> <secs> [cli|full]
#                         boot it, run autoexec.txt, print the console
EMU=${AGON_EMU:-$HOME/fab-agon-emulator-1.2.4}
MOS=${MOS:-$EMU/firmware/mos_platform.bin}     # MOS 3.0.2
ROOT=${ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}
SCRATCH=${SCRATCH:-$(mktemp -d)}

# emu_card [ref.bin] [mos.dat]
emu_card() {
    local sd bin=${1:-$ROOT/bin/ref.bin} dat=${2:-$ROOT/out/mos.dat}
    sd=$(mktemp -d -p "$SCRATCH")
    cp -r "$EMU/sdcard/mos" "$sd/"
    cp "$EMU/sdcard/MOS.bin" "$EMU/sdcard/firmware.bin" "$sd/"
    mkdir -p "$sd/bin" "$sd/config/ref/docs"
    cp "$bin" "$sd/bin/ref.bin"
    cp "$dat" "$sd/config/ref/docs/mos.dat"
    echo "$sd"
}

# emu_run <card> <seconds> [cli|full] [paced]: the console, \r removed. The
# commands in autoexec.txt run, then MOS prints its prompt and the emulator
# stops -- or, on the full emulator, which shows nothing on the console, once
# the transcript /ref.out says ref has exited. Flat out (-u) unless paced:
# timings are taken paced, at the Agon's own speed.
emu_run() {
    local sd=$1 limit=$2 which=${3:-cli} speed=${4:--u} fifo cap hold pid prog
    [ "$speed" = paced ] && speed=""
    fifo=$(mktemp -u -p "$SCRATCH"); cap=$(mktemp -p "$SCRATCH")
    mkfifo "$fifo"
    tail -f /dev/null > "$fifo" & hold=$!
    if [ "$which" = full ]; then
        prog=(env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./fab-agon-emulator)
    else
        prog=(./agon-cli-emulator)
    fi
    (cd "$EMU" && exec timeout "$limit" "${prog[@]}" $speed --sdcard "$sd" --mos "$MOS" \
        < "$fifo" > "$cap" 2>&1) &
    pid=$!
    # MOS's prompt after the last command: autoexec is done.
    local waited=0
    while kill -0 "$pid" 2>/dev/null; do
        if { [ "$which" = full ] && grep -q '^exit ' "$sd/ref.out" 2>/dev/null; } \
            || tr -d '\r' < "$cap" | tail -c 400 | grep -q '/ \*[[:space:]]*$'; then
            sleep 0.3
            kill "$pid" 2>/dev/null
            break
        fi
        sleep 0.2
        waited=$((waited + 1))
    done
    wait "$pid" 2>/dev/null
    kill "$hold" 2>/dev/null; wait "$hold" 2>/dev/null
    rm -f "$fifo"
    tr -d '\r' < "$cap"
    rm -f "$cap"
}
