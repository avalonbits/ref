# ref

ref puts the community Agon documentation,
[agon-docs](https://github.com/AgonPlatform/agon-docs), on the Agon: the
whole wiki in one file on the SD card, and a viewer that reads it with
seeks, follows its links and searches it as you type. It looks up APIs, VDU
sequences, star commands and system variables however you write them:
`vdu 23,27,&21`, `VDU 23 27 33` and `23;27;0x21` all find the same command.

It runs on the Agon Light, Light 2, Origins and Console8, on MOS 3.0.2.

## Using it

    ref                     the contents, organised as the wiki is
    ref <query>             look it up; search the text if no key matches
    ref -k <query>          look it up, and nothing else
    ref -s <words>          search the text
    ref -p <query>          print what was found, instead of showing it
    ref -f <file>           read another ref.dat

Lookups know:

| Ask for | Like |
|---|---|
| VDU sequences, written any way MOS reads numbers | `ref vdu 23 27 3`, `ref 23,27,&21`, `ref vdu 23,0,&85,1,5,0` |
| MOS API calls, by name or number | `ref mos_fopen`, `ref mos 0x0a` |
| RST calls | `ref rst 08h`, `ref rst 8` |
| C functions | `ref f_printf` |
| star commands and their aliases | `ref cd`, `ref dir` |
| system variables | `ref run$path`, `ref hotkey$1`, `ref sysvar_scrmode` |
| PLOT codes | `ref plot 85`, `ref vdu 25 85` |
| BBC BASIC keywords | `ref gcol` |
| any word of a heading | `ref copper` |

A buffered command's buffer ID and an audio command's channel can be
anything: `vdu 23 0 &a0 7; 6` finds command 6 whichever buffer it names.
A lookup that finds one topic opens it; several, a list to choose from.

Reading a topic:

A cursor moves through the text, as in a text-mode browser; the link or
heading under it is lit, and anywhere else the text cursor shows where it is.

| Key | Does |
|---|---|
| arrows | move the cursor; the screen follows it |
| PgUp, PgDn, Home, End | by the page, or to either end (Space and `b` page too) |
| Ctrl+Up, Ctrl+Down | the cursor to the previous or next heading |
| Ctrl+Left, Ctrl+Right (or Shift+Tab, Tab) | the cursor to the previous or next link |
| Enter | follow the link under the cursor |
| Ctrl+F | find words, from the top of the screen on, as aed finds |
| Ctrl+N, Ctrl+P | the next or previous match |
| Backspace, Alt+Left / Alt+Right | back, with the cursor on the link you followed / forward |
| `n`, `p` | the next or previous topic |
| `u` | the section the topic is in |
| `t` | the contents |
| `/`, F3 | find: each key typed narrows the list, keys first |
| `?`, F1 | the keys |
| Esc, `q` | quit |

ref returns 0 when it showed something and 100 when nothing matched, so a
script -- or an IDE such as ade, whose F1 runs `ref -k <word>` through hub --
can tell.

## Installing

ref.dat is the wiki's own text, and agon-docs has no license yet, so there
is no release to download until its maintainers settle one: build ref.dat
yourself (below), for your own card. Then copy `ref.bin` to `/bin` and
`ref.dat` to `/lib/ref` on the SD card:

    /bin/ref.bin
    /lib/ref/ref.dat

## Building

ref.dat is built on a PC or Mac from agon-docs at the commit pinned in
`SOURCE`; ref.bin with [agondev](https://github.com/AgonPlatform/agondev).

    make dat            build/ fetches agon-docs and writes out/ref.dat
    make agon           bin/ref.bin
    make test           the builder's tests, and the reader core on the PC
                        against the builder's own answers
    make emu            ref in fab-agon-emulator: test/run.sh
    test/core.sh        the reader core on the Agon, built by agondev and
                        by acc, against the PC

When the wiki changes, move the commit in `SOURCE` and run `make dat`; the
builder reports any heading its rules (`build/rules.toml`) no longer turn
into a key, and any link it cannot resolve. The viewer only changes when the
format does.

[BRIEFING.md](BRIEFING.md) describes the design: the builder, the file, the
viewer and how they are tested.
