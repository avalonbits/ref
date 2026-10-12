# ref: the Agon documentation, on the Agon

This is the briefing for building **ref**: the community Agon documentation,
compiled into one file on the SD card and read with a viewer that runs on the
Agon, with fast search over its APIs, VDU commands, star commands and topics.
Read all of it before writing code.

## What it is

- **A builder** that runs on a PC or Mac. It fetches
  [AgonPlatform/agon-docs](https://github.com/AgonPlatform/agon-docs) at a
  pinned commit and compiles it into `ref.dat`: the wiki's pages split into
  topics, its navigation tree, the links between topics, a keyword index and
  a full-text index, each topic compressed on its own.
- **A viewer, `ref`**, a program for the Agon: a contents tree organised as
  the wiki is, topics with links you can follow (back and forward),
  search-as-you-type, and direct lookup from the command line.
- **Updating** is rebuilding: when the wiki changes, move the pinned commit,
  run the builder, release a new `ref.dat`. The viewer doesn't change unless
  the format does.

Two uses:

- **On its own**, from the prompt: `ref` opens the contents, `ref mos_fopen`
  or `ref vdu 23 27 3` opens the matching topic, `ref -s sprite` searches the
  text.
- **From ade** (the user's IDE, in `~/code/ade`): F1 on the word under the
  cursor runs `ref -k <word>` through hub as a user program, so the user reads
  the topic and comes back to their code when they quit. ade needs nothing but
  the command line; see "Working with ade and hub".

The name follows the user's three-letter convention (aed, zap, acc, hub,
ade, dig).

## Before shipping anything: the license

agon-docs has **no license file**. Building `ref.dat` for the user's own card
is one thing; putting the wiki's text into a release zip, or onto other
people's cards, is redistribution. Before the first public release, the user
asks the AgonPlatform maintainers for a license or explicit permission; the
builder and the viewer are the user's own code either way. Do not publish a
`ref.dat` until the user says this is settled.

## The source, as it stands

At commit `f9806bd` (15 July 2026) agon-docs is a MkDocs site: 32 Markdown
pages, 637 KB of text, and seven images (font map, hexload diagram, GPIO
pinouts), which ref replaces with a note. 31 pages are in the nav; the
32nd, `Projects.md`, is outside it but linked from the start page, and MkDocs
builds it all the same, so ref keeps it in a last contents section of its
own ("Pages outside the menu"). Its navigation is in `mkdocs.yml`:

    Welcome & getting started: Start here, FAQ, Theory of operation, Programming
    MOS: Overview, Command line, API, C functions, Executable format, Modules,
         System Variables, Argument Substitution, Keyboard Input
    VDP: Overview, Main Commands, Screen Modes, PLOT Commands, System Commands,
         Audio API, Bitmaps and Sprites API, Buffered Commands API, Context
         Management API, Copper Effects API, Font Management API, Tile Engine,
         VDP Variables
    BBC Basic
    GPIO
    Guides: Updating Firmware
    Additional resources: External documentation, Third party projects

The largest pages are the Buffered Commands API (96 KB), the MOS API (89 KB),
the Audio API, the VDU commands and the system commands (about 40 KB each).

The Markdown uses MkDocs extensions: `{#anchor}` ids on headings,
admonitions (`!!! note`), footnotes, collapsible details, code blocks, and
tables (the system commands page alone has about 80 table rows). Read
`markdown_extensions` in `mkdocs.yml` and handle each one the pages use.

What the builder found (step 1): the pages use admonitions (`note`,
`danger`), footnotes (Theory of operation only), front matter and `[TOC]`
(FAQ), attr_list ids on headings *and on table cells* (`{#last-colour}` in
the VDP variables tables, which other pages link to), one block quote, and
`<br>`. No details, tabs, mermaid, maths, snippets or emoji. One internal
link is wrong in the wiki itself (`#VDP-pflags-vdpflags` on the MOS API page,
whose heading's anchor is lower case -- broken on the site too); it is fixed
in `build/rules.toml`, which says why, and an unused fix stops the build.

**The headings are the index.** Most entries are already headings, with the
thing being described in backticks:

| Page | Headings look like |
|---|---|
| VDU commands | ``## `VDU 4`: Write text at text cursor {#vdu-4}`` |
| system commands | ``### `VDU 23, 0, &89, 3, cursorId;`: Select a mouse cursor`` |
| bitmaps | ``### `VDU 23, 27, 4, n`: Select sprite n`` |
| MOS API | ``### `0x00`: mos_getkey``, ``### `RST 18h`: Output a stream ...`` |
| C functions | ``### 0x05 - `int f_printf (FIL* fp, const TCHAR* str, ...);` `` |
| star commands | ``## `CD` `` |
| sysvars | ``### System Path variables: `Run$Path` and `Moslet$Path` `` |
| PLOT | `## Filled triangles (PLOT codes &50-&57)` |
| BBC BASIC | `### MODE`, `### GCOL` |
| buffered commands | `## Command 6: Conditionally call a buffer {#command-6}` |
| audio | `### Command 5, 0: Load sample` |

Two pages name their entries relative to a command introduced at the top:
"Command 6" on the buffered commands page is `VDU 23, 0, &A0, bufferId;
6, ...`, and "Command 5, 0" on the audio page is `VDU 23, 0, &85, channel,
5, 0, ...`. The builder needs a **rules file**, per page, saying how a
heading becomes keys (see "The keyword index").

## The design

### The builder (PC or Mac)

In Python, in this repository (`build/`), with no network access beyond
fetching agon-docs at the pinned commit (kept in one file, `SOURCE`, with the
commit and the date). Steps:

1. Read `mkdocs.yml`'s `nav` for the tree, then each page.
2. Parse the Markdown, with the extensions the pages use, into a small
   document model: headings, paragraphs, lists, code blocks, tables,
   admonitions, links, inline code and emphasis.
3. **Split into topics** at headings: a topic is a heading and what follows it
   up to the next heading of the same or a higher level. Each page's top is a
   topic too. Every topic keeps its page, its parent topic and its children,
   so the contents tree goes from page to section to entry.
   *Decided:* each topic stores only its own text, up to the next heading of
   any level. Its descendants are numbered straight after it, so "the heading
   and what follows it" is the topic followed by its descendants, and the
   viewer joins them when it shows a topic. Storing every ancestor's copy
   would put each page in the file three or four times.
   A page that opens with its only level-1 heading uses that heading as its
   top topic; otherwise (BBC BASIC, Font API, External documentation) the top
   topic is the text before the first heading, titled from the nav.
4. **Resolve links** -- to other pages, to `#anchors`, to headings -- into
   topic numbers. A link the builder can't resolve is an error, reported with
   its page and line, never a silent dead link. External links stay as text
   with their URL.
5. **Render each topic** into ref's own markup (below), and compress it.
6. **Build the indexes** (below), and write `ref.dat`.
7. **Report**: topics, keys, words, the file's size, and every heading that no
   rule turned into a key, so a new page in the wiki shows up as a gap rather
   than going unindexed.

### ref's markup

A topic is stored as a byte stream the viewer can lay out at any width,
because the screen width depends on the mode and font:

- plain text in runs, with control bytes for: a heading of level 1-4;
  paragraph and line breaks; list items with their depth; emphasis; inline
  code; the start and end of a link, carrying its target topic; a code block
  (laid out as is, never wrapped -- scrolled sideways instead); a note
  (admonition).
- **Tables are laid out by the builder**, as fixed-width text with ruled
  columns, because laying them out needs the whole table at once. A table
  wider than 78 columns wraps its cells within their columns; check how this
  reads in 80 columns before settling it, since the system commands and VDP
  variables pages lean on tables.
- Characters outside the Agon's font are replaced by the builder (quotes,
  dashes, arrows, the `§§` markers on the VDU page, which are footnote marks
  and become `*`).
  *Decided:* the VDP's system font is Windows-1252 above 0x7F, nearly, with
  `§` at 0xA7 -- so the `§` markers are kept as they are: each count of them
  means a different VDP version, which `*` would lose. Arrows go to the
  font's own (0x8D, 0x8F, 0x90); it has no down arrow, so that one is `v`.

*As built (step 3)* -- `build/refbuild/render.py` specifies the markup and
`build/refbuild/layout.py` the layout, which the C reproduces line for line.
Blocks are `kind|flags indent hang content END`; runs of spaces are one space
(the wiki puts two after a full stop); tables are ruled columns laid out to
76 columns (80 less the viewer's margins), cells wrapped within them, links inside cells kept. A topic
shown without its descendants -- because together they pass 16 KB --
ends with "In this section:" and a link to each child. Each line the core
lays out records the style and link open where it starts, so the viewer can
draw any line alone.

### `ref.dat`

One file, read with seeks, never loaded whole: the Agon has 512 KB and the
docs are 800 KB.

| Section | Holds |
|---|---|
| header | magic, format version, the agon-docs commit and date, the offsets of the sections below |
| contents | the tree: for each node its title, its topic, its first child and next sibling |
| topics | for each topic: its title, page, parent, and the offset and lengths of its compressed text |
| text | each topic's markup, compressed on its own, so reading a topic is one seek and one decompress |
| keys | the keyword index, sorted |
| key directory | every 64th key with its offset, small enough to keep in memory |
| words | the full-text index: each word, sorted, with its list of topics |
| word directory | as for the keys |

**Compression**: a simple LZ77 format with byte-aligned tokens (LZ4's block
format, or one designed here), chosen so the eZ80 decompresses it fast and the
code is small. Measure the ratio on the real pages; expect the file around
300-400 KB. A topic's text is usually a few KB; the biggest topics set the
viewer's buffer size, which the builder reports.

*Measured (step 3):* LZ4's block format. Topics compressed one by one came
to 424 KB from 698 KB -- most topics are small, and LZ4 finds little inside
one. An 8 KB dictionary of strings common to many topics, which every
topic's matches may reach into, brings them to 337 KB; ref.dat is 473 KB in
all (`build/refbuild/refdat.py` documents every byte). The view buffer is
23.4 KB (VDP Variables, one topic of tables); the largest compressed topic
3.6 KB. The full-text index's stop words travel in ref.dat with the
normalising settings, so the C has no list of its own, and a search reads
at most eight query words.

*Tested (step 3):* `make test` builds the core for the PC (`out/host/reftool`,
with the address and undefined-behaviour sanitisers) and holds it against the
builder: the shared normalising cases, every key the builder wrote, 280
lookups and 170 searches compared answer for answer, every topic's view byte
for byte, and every topic laid out at 79 and 39 columns line for line, with
each line's style. The core also compiles under acc, and its stack frames
stay under 128 bytes (`test/frames.sh`, as in aed and ade). Found on the way:
`rst 10h` became the key `rst 16`, which a lookup then read as hex -- so
after `rst` numbers are written back in hex, and every key normalises to
itself (a test).

### The keyword index

Each key is a normalised string, with the topic it opens and the text to show
for it in a list of matches. Where they come from:

- **VDU sequences**, from the backticked headings: the numbers before the first
  parameter that is a name (`n`, `bufferId;`, `cursorId;`), each read as MOS
  does -- decimal, `&hex`, `0xhex`, `hex h` -- and written in decimal, with
  `vdu` in front. So ``VDU 23, 27, &21, bitmapId; 0;`` gives `vdu 23 27 33`.
  The buffered commands and audio pages add their command's prefix, from the
  rules file.
- **MOS API calls** by name and by number: `mos_fopen` and `mos 0x0a`.
- **RST calls**: `rst 08h`, also as `rst 8`.
- **C functions** by name: `f_printf`, `setvarval`.
- **Star commands** by name, and their documented abbreviations.
- **System variables** by name (`run$path`), and sysvars from the API's tables
  (`sysvar_scrmode`) where the rules file says a table's first column is a
  name. (At f9806bd the API lists them as EQUs in a code block, so the rule
  is a regex over code lines.)
- **PLOT codes**: each code in a heading's range, so `plot 85` finds the filled
  triangles.
- **BBC BASIC keywords** from that page's headings.
- **Every heading's title**, as words, for the rest.

**Lookups are normalised the same way**, so `vdu 23,27,&21`, `VDU 23 27 33`,
`23;27;0x21` and `vdu 23 27 33 5 0` all reach `vdu 23 27 33`: when there is no
exact match, the longest key the query starts with wins, then the keys that
start with the query are offered as a list. Case never matters.

*As built (step 2)* -- `build/refbuild/normalize.py` is the specification,
and `build/tests/normalize.json` the cases the C must pass too:

- **Skips** stand in for the parameter a command family names before its
  command number: the buffer ID of `VDU 23, 0, &A0` and the channel of
  `VDU 23, 0, &85`. Whatever is written there becomes `_`, so
  `VDU 23,0,&A0,1;6`, `vdu 23 0 160 1 0 6` (the ID as two bytes) and the
  docs' `bufferId; 6` all give `vdu 23 0 160 _ 6`. The rules file declares
  the skips, and ref.dat carries them so the viewer normalises the same way.
- Bare numbers after `rst` are hex (`rst 10` is RST 10h); everywhere else
  they are decimal, as MOS reads them.
- A query that starts with a number is a VDU sequence (`23 27 3`).
- A key up to a `$` counts as a prefix: `hotkey$12` finds `Hotkey$<n>`.
  `vdu` alone never does, or every unknown sequence would open the `VDU` star
  command.
- Numbers are never title words: `0x16: mos_uclose` must not answer `vdu 22`.
- Star command abbreviations (`DEL.`) need nothing of their own: the dot is
  stripped, and the keys that start with `del` are offered.
- Gaps: the 113 headings at f9806bd that no rule keys -- prose sections such
  as "Advice on file handling" -- are recorded in `build/gaps.txt`; the
  builder lists any heading missing from it as a new gap, and a test fails
  on one. A page with no rules at all is nothing but gaps.

### The full-text index

Every word of three letters or more in every topic, lowercased, with the
commonest English words left out, each with the topics it appears in (as
deltas, in variable-length bytes). A search finds the words that start with
what was typed, merges their topics, and ranks them: a topic whose title has
the word first, then by how many of the query's words a topic has. It is
read from the card a block at a time, so a search costs a handful of seeks,
however big the docs.

### The viewer (Agon)

C, built with agondev; also with acc, which has to keep working (the reader
core at least).

- **The reader core** -- `ref.dat` access, decompression, key and word
  lookup, the layout of markup into lines at a given width -- is portable C
  with no Agon calls, behind a small API. That keeps it testable on the PC
  (below), and lets it become a library later if ade wants to show docs in
  its own window (option 3 when this was planned, deliberately left for
  later).
- **The screen**: an 80-column text mode by default, whatever the mode and
  font in use say the width is. Show the contents tree on the left, or full
  width, depending on the width (to decide, see the open questions).
  Keys: arrows, PgUp and PgDn, Enter on a link, Backspace or Alt+Left for
  back, Alt+Right for forward, `/` or F3 to search, `t` for the contents, Esc
  to quit. A status line shows where you are in the tree.
- **Search as you type**: each key typed narrows the list, keys first, then
  words; Enter opens the selected topic.
- **The command line**:

      ref                         the contents
      ref <query>                 a key lookup, then a search if nothing matches
      ref -k <query>              the key lookup only: what ade's F1 runs
      ref -s <words>              the full-text search
      ref -f <file>               another ref.dat (default /lib/ref/ref.dat)

  A lookup that finds one topic opens it; several, a list to choose from;
  none, a message, and a search.
- **Memory**: the user's rules for agondev C apply (`~/.claude/CLAUDE.md`):
  keep stack frames under 128 bytes, large buffers static, and measure peak
  heap with zap's `zmalloc` shim rather than guessing.

*As built (step 4)* -- `src/viewer`, on the core:

- **The screen**: the mode and font in use, whatever they are; text is laid
  out inside a margin of two columns each side (one in a 40-column mode),
  so nothing is ever written in the last column and the VDP never wraps or
  scrolls under ref. Tables are laid out to 76 columns to fit. Moving a
  line or three scrolls the text rows on the VDP -- a text viewport above
  the status line, `VDU 23,7,0,dir,0`, then `VDU 26` -- and draws only the
  rows that came into view; longer jumps redraw, as a run of scrolls costs
  more than one repaint. The contents and lists likewise redraw only the
  two rows a cursor move changes. The top row is a
  header, edge to edge: the wiki's own title (mkdocs.yml's `site_name`,
  "Community Agon Platform documentation"), which ref.dat now carries --
  flag bit 0 and a title offset in the header's spare bytes, so the format
  stays version 1 and a file without it still opens. The last row is a
  status line, edge to edge: the topic's place in the tree, and how far
  down. Its last cell, the bottom right, cannot be printed in without the
  VDP scrolling, so it is painted instead: a one-cell text viewport
  cleared in the status colours (`VDU 28`, `VDU 12`, `VDU 26`). Each row is
  drawn whole and sent in one RST 18h call. Colours by role (text, emphasis,
  code, link, the selected link, headings, notes, status) mapped for 16, 4
  and 2 colour modes. *Decided* (an open question): the contents replace the
  topic rather than sitting beside it -- at 80 columns a tree beside the
  text leaves neither enough room. `t` goes to the contents at the topic's
  own node, Esc comes back.
- **Keys**: as planned, and a hypertext browser's, with a cursor: the
  arrows move it by line and character and the screen follows; Ctrl+Up/Down
  put it on the previous or next heading (brought to the top of the
  screen), Ctrl+Left/Right (or Shift+Tab/Tab) on the previous or next link.
  The link or heading under the cursor is lit whole -- every line of it,
  when it wraps, as the FAQ's long questions do in its list;
  elsewhere the VDP's text cursor shows. Enter follows the link under the
  cursor, and the history keeps the cursor, so Backspace returns to the
  link that was followed -- and, from a topic opened from the contents, a
  list or find, to that view as it was left (the history holds a mark
  for it), so the start page is always a Backspace away. (Each row starts in the text colour: a row that
  ended lit once lit the next row's padding, a second cyan bar that
  scrolling then left behind.) Ctrl+F finds words as aed does -- a prompt on the bottom row
  holding the last search, Enter to find, ignoring case, within a line,
  Ctrl+N / Ctrl+P for the next and previous match, wrapping, "Not found"
  until a key -- but from the top of the screen on, and with spaces
  allowed in the prompt (aed's prompt drops them). Left/Right scroll code
  and tables sideways, `n`/`p` for the next and previous topic,
  `u` for the section a topic is in, `?` or F1 for help. Keys come from
  agondev's event queue, so each carries its modifiers as they were.
- **The command line**, as planned, plus `-p` (print what was found instead
  of showing it -- scripts, and testing the release build) and `-v` (ref's
  version and the agon-docs commit its ref.dat holds), and `-T`, which times
  the core in the machine. Results: 0 when something was shown, 100 when
  nothing matched or ref.dat cannot be read, with a message saying which.
- **Measured** (`ref -T`, MOS's own clock, the emulator paced): opening
  ref.dat 14 ms, a lookup 6-14 ms, a search 38 ms, the keys from a prefix
  24 ms, the largest topic (VDU Variables, 23 KB of tables) read and laid out
  182 ms -- 110 ms without the compiler workaround below. Layout was 290 ms
  at first: a second pass per line to follow styles, and pointers the
  compiler kept on the stack. One pass, with the plain-text runs crossed by
  two small leaf functions, fixed both.
- **Memory**: 35 KB of code, 77 KB of statics, and a heap that peaks at
  85 KB (zmalloc, a session through a topic, the contents, the largest topic
  and a search): under 200 KB of the 448 KB a program has.

**A compiler bug** (found in step 4): agondev's clang 15.0.7 at -Oz can
compile `n = *p++` so that the load happens after the increment -- the
register coalescer rematerialises the load at the point where the pointer
has already moved. The reader core read its normalising settings off by
one: skips never applied on the Agon, while every PC test passed. Building
with `-mllvm -twoaddr-reschedule=false` (agon.mk) avoids it; a four-line
reproducer is `n = *p++; *out = p; return n;`, which compiles to
`inc hl; ld c,(hl)`. `test/core.sh` is what caught it, and is the control:
without the flag it fails. The settings are also walked by index now. Worth
reporting to AgonPlatform/agon-llvm-project.

### Where things go on the card

    /bin/ref.bin
    /lib/ref/ref.dat

## Working with ade and hub

hub (`~/code/devel`, released as avalonbits/hub) is a resident shell that
runs programs on behalf of others and brings them back. ade's F1 saves its
session, pushes `ref -k <word>` with `HUB_USER_PROGRAM` (ref gets the
prompt's screen) and its own continuation, and returns; ref runs, the user
reads and quits, and ade comes back. That is all ade needs from ref: a stable
`-k` and a result -- 0 when a topic was shown, 100 when nothing matched,
never 1, 4 or 5, which MOS reports as its own errors. Tell the ade session
(it reads `~/code/ade`) the command line once it works.

ref must also work without hub, as a plain program.

## Plan

| Step | Scope | Done when |
|---|---|---|
| 1. the builder's model | fetch, parse the pages and the nav, split into topics, resolve links, report what can't be | every page parses, every internal link resolves, and the report lists topics and headings |
| 2. keys | the rules file; key extraction and normalisation; the gap report | `vdu 23 27 33`, `mos_fopen`, `cd`, `run$path`, `plot 85`, `gcol` and 50 more all reach the right topic, in host-side tests |
| 3. `ref.dat` | markup, compression, the indexes, the file; the reader core in C, tested on the host against the builder's own answers | the host build of the core finds every key the builder wrote, and decompresses every topic back to what the builder rendered |
| 4. the viewer | the screen, links, history, contents, search as you type, the command line | in the emulator: `ref -k vdu 23 27 3` opens "Draw current bitmap", links work, a search finds topics, timings measured |
| 5. ade | tell ade the command line; it wires F1 | F1 in ade opens the topic and comes back |
| 6. release | once the license is settled: a release with `ref.bin`, `ref.dat` and the builder, in the user's release page format | -- |

*Status (11 October 2026):* steps 1-4 done, each in its own commit. Step 5:
the ade session (ade-99) has been sent the command line and the result
contract; wiring F1 is ade's. Step 6 waits on the license: `mkrelease.sh`
builds the two zips locally (`ref-<version>.zip` for the card,
`ref-builder-<version>.zip`), and refuses to run until
`REF_LICENSE_SETTLED=1` says the license is settled. Nothing is published.

## Testing

- **The builder** has host tests: given pages, these topics, these keys, these
  links; and a test over the whole of agon-docs at the pinned commit that every
  link resolves and the expected keys are there.
- **The reader core** builds on the PC too, and is tested there against the
  builder's own output: every key it wrote is found; every topic decompresses
  to exactly what was rendered; searches return what the builder's own search
  returns.
- **The viewer** is tested in fab-agon-emulator, driven as the user's other
  projects drive it -- never by typing into the emulator's stdin: commands in
  `autoexec.txt`, stdin held open with a fifo, both output streams captured,
  stopped at the MOS prompt. hub's `test/run.sh` is a working harness to copy.
  Keys for the interactive parts come from a scripted-input build (ade's and
  mc's tests do this: `-DSCRIPT` swaps the keyboard for a file on the card).
  The CLI emulator's VDP draws nothing but logs the VDU it doesn't handle;
  anything that needs the screen drawn is checked on the full emulator with
  `SDL_VIDEODRIVER=dummy`, as hub's `test/screen.sh` does.
- **Speed** is measured in the machine, from MOS's own clock, never from the
  wall clock outside: a key lookup, a search, opening the largest topic.
- Every safeguard has a control: break it, and its test fails.

*As built:*

- `make test` -- the builder (parser, model, keys, normalising, render,
  layout, LZ4, the file), the core on the PC against the builder, the stack
  frames, and the core compiling under acc. About 150 tests, 10 seconds.
- `make emu` (`test/run.sh`) -- in the emulator, MOS 3.0.2, about 45 checks
  in under a minute: the release build's `-p`, `-v` and exit codes (each
  under MOS's `Try`, since a failing line stops autoexec); the scripted build
  through a topic, its links and history, scrolling, lists, search, the
  contents, find as you type and help; ade's F1 under hub, through a
  stand-in client (`test/agon/hubf1`) that pushes `ref -k <word>` as a user
  program and prints the result hub hands back (and hub's own `fail`, as the
  control); on the full emulator, in modes 3, 8 and 0, the screen read back
  from the VDP itself (`VDU 23,0,&83`, retried in each role's colours, since
  the VDP matches in the current text colour) against what ref meant to
  draw, the margins blank, and the same after region scrolling in a topic,
  the contents and a list; the heap's peak; and the timings, paced.
- `test/core.sh` -- the core on the Agon: `test/host/reftool.c`, built by
  agondev and by acc, runs every mode in the emulator with its inputs and
  outputs as files on the card, and must answer byte for byte as the PC
  build does. Slow (about 15 minutes), so not in `make emu`; run it after
  touching the core or changing compilers.
- The VDP cannot read back its own last column or last row, so the screen
  check compares the rest; a wrap or scroll would shift every row, which it
  would see.

## Environment

| Tool | Where |
|---|---|
| agondev | `~/agondev` |
| acc (read only) | `~/code/acc` |
| zap, with the `zmalloc` shim | `~/code/zap` |
| fab-agon-emulator 1.2.4, MOS 3.0.2 | `~/fab-agon-emulator-1.2.4` |
| hub (and its harnesses) | `~/code/devel` |
| ade | `~/code/ade` |
| agon-docs | fetched by the builder at the pinned commit |

## Working rules

- The user's global instructions (`~/.claude/CLAUDE.md`) apply: commit after
  each change that passes its test, every change with a test that bites, the
  commit message format, **no AI attribution anywhere**, the emulator rules.
- No remote yet: commit on `main`. Once the user creates the GitHub
  repository, every change goes through a PR (branch, PR, review, squash
  merge), as in the user's other projects.
- Keep this briefing current as decisions are made.

## Open questions

- The license of agon-docs (blocks publishing).
- Which mode and font the viewer uses. *For now*: whatever is in use; ade
  hands ref the prompt's screen through hub. The contents replace the topic.
- How much of the BBC BASIC page to index: its headings only cover what the
  Agon changes; the rest of BBC BASIC is external documentation.
- Whether `ref.dat` should carry more than agon-docs later -- zap's, acc's,
  hub's own docs -- as further books in the same contents tree.
