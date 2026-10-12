# ref

ref puts the community Agon documentation,
[agon-docs](https://github.com/AgonPlatform/agon-docs), on the Agon: the
whole wiki in one file on the SD card, and a viewer to read it, follow its
links and search it. It runs on the Agon Light, Light 2, Origins and
Console8, under MOS 3.0.2.

## Installing

Copy two files to the SD card:

    /bin/ref.bin
    /lib/ref/ref.dat

There is no release to download yet. ref.dat is the wiki's own text, and
agon-docs has no license, so until its maintainers settle one, build
ref.dat yourself for your own card (see [Building](#building)).

## Looking things up

    ref                     the contents, organised as the wiki is
    ref <query>             look it up; search the text if nothing matches
    ref -k <query>          look it up, and nothing else
    ref -s <words>          search the text
    ref -p <query>          print what was found instead of showing it
    ref -f <file>           read another ref.dat
    ref -v                  ref's version, and the agon-docs commit it holds

A lookup knows:

| Ask for | Like |
|---|---|
| VDU sequences, numbers written any way MOS reads them | `ref vdu 23 27 3`, `ref 23,27,&21`, `ref vdu 23,0,&85,1,5,0` |
| MOS API calls, by name or number | `ref mos_fopen`, `ref mos 0x0a` |
| RST calls | `ref rst 08h`, `ref rst 8` |
| C functions | `ref f_printf` |
| star commands and their aliases | `ref cd`, `ref dir` |
| system variables | `ref run$path`, `ref hotkey$1`, `ref sysvar_scrmode` |
| PLOT codes | `ref plot 85`, `ref vdu 25 85` |
| BBC BASIC keywords | `ref gcol` |
| any word of a heading | `ref copper` |

Case doesn't matter, and the parameters a command names before its number
can be anything: `vdu 23 0 &a0 7; 6` finds buffered command 6 whichever
buffer it names. A lookup that finds one topic opens it; more than one, a
list to choose from.

ref returns 0 when it showed something and 100 when nothing matched, so a
script can tell. ade's F1 runs `ref -k <word>` through hub this way.

## Reading

The screen has the wiki's title across the top, the text in the middle, and
a status line at the bottom saying where the topic sits in the wiki and how
far down you are. Links are green and headings yellow.

A cursor moves through the text, as in a text-mode web browser. The link or
heading under it is lit; anywhere else the text cursor shows where it is.

| Key | Does |
|---|---|
| arrows | move the cursor; the screen follows it |
| PgUp, PgDn, Home, End | a page up or down, or to either end (Space and `b` page too) |
| Ctrl+Up, Ctrl+Down | the cursor to the previous or next heading |
| Ctrl+Left, Ctrl+Right | the cursor to the previous or next link (Shift+Tab and Tab too) |
| Enter | follow the link under the cursor |
| Backspace, Alt+Left | back: to the link you followed, or to the contents, list or search you came from |
| Alt+Right | forward |
| Ctrl+F | find words, from the top of the screen down |
| Ctrl+N, Ctrl+P | the next or previous match |
| `n`, `p` | the next or previous topic in the wiki |
| `u` | the section the topic is in |
| `t` | the contents |
| `/`, F3 | search: each key typed narrows the list, matching keys first |
| `?`, F1 | the keys |
| Esc, `q` | quit |

In the contents, the arrows move, Right and Left open and close a section,
and Enter opens a topic.

ref uses the screen mode and font already in use; under hub, the prompt's.

## Building

ref.dat is built on a PC or Mac with Python 3.11 or later, from agon-docs
at the commit pinned in [`SOURCE`](SOURCE). ref.bin is built with
[agondev](https://github.com/AgonPlatform/agondev).

    make dat            fetch agon-docs and write out/ref.dat
    make agon           bin/ref.bin
    make test           the builder's tests, and the reader core on the PC
    make emu            ref in fab-agon-emulator
    test/core.sh        the reader core on the Agon, built by agondev and acc

When the wiki changes, move the commit in `SOURCE` and run `make dat`. The
builder stops on a link it cannot resolve and lists any heading its rules,
in [`build/rules.toml`](build/rules.toml), no longer turn into a key.
ref.bin only needs rebuilding if the file format changes.

[docs/DESIGN.md](docs/DESIGN.md) explains how ref works.
