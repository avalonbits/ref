# How ref works

ref reads documents: one file each, in the format specified in
[FORMAT.md](FORMAT.md), installed as `/config/ref/docs/<name>.dat`. ref has
two halves. A **builder**, in Python on a PC or Mac, compiles agon-docs
into one, `mos.dat`. A **viewer**, in C on the Agon, reads documents with
seeks and shows them. Between them is the format, and on the Agon side a
portable **reader core** that knows it. Any other tool can write the format
too.

    agon-docs (Markdown, at the commit in SOURCE)
        |
        |  build/refbuild      parse, split into topics, resolve links,
        v                      extract keys, render, compress, index
    mos.dat (about 470 KB)
        |
        |  src/core            open, decompress, look up, search, lay out
        v
    src/viewer -> the screen

The Agon has 512 KB and the wiki is 700 KB of text, so nothing is loaded
whole: a document is read a topic, a key block or a word block at a time.

## The builder

`python3 -m refbuild` ([`__main__.py`](../build/refbuild/__main__.py)) runs
these steps in order.

**The source.** [`source.py`](../build/refbuild/source.py) reads
[`SOURCE`](../SOURCE), fetches agon-docs at that commit once into
`build/cache`, and reads `mkdocs.yml`'s `nav` (the wiki's menu) and
`site_name` (its title).

**Parsing.** [`markdown.py`](../build/refbuild/markdown.py) parses each
page into a small document model: headings, paragraphs, lists, code,
tables, notes (admonitions), quotes and rules, with inline code, emphasis,
links and images. It covers the Markdown the wiki uses, which is
Python-Markdown's plus MkDocs' extensions. Every block keeps its line
number, so errors can say where they are.

**Topics and links.** [`book.py`](../build/refbuild/book.py) splits pages
into topics at headings. A topic holds only its own text, up to the next
heading of any level. Its descendants are numbered straight after it, so
"a heading and everything under it" is a topic and the topics that follow
it. Each topic records its parent and children, and the nav's sections,
pages and headings form the contents tree. Anchors are worked out as
Python-Markdown's table of contents makes them, and every internal link
becomes a topic number. A link that cannot be resolved stops the build,
naming its page and line; one the wiki itself gets wrong is fixed in
[`build/rules.toml`](../build/rules.toml), which says why.

**Keys.** [`keys.py`](../build/refbuild/keys.py) turns headings into
lookup keys using the rules in `rules.toml`, page by page. A rule can take
a VDU sequence from a heading, a star command from a heading's code, a
range of PLOT codes, or apply a regular expression to a heading, a code
block or the body text. Every heading also gives the words of its title.
A heading that no rule keys, on a page that isn't marked as prose, is a
*gap*. The known gaps are listed in [`build/gaps.txt`](../build/gaps.txt),
and a new one fails a test, so a page added to the wiki can't go
unindexed unnoticed.

**Normalising.** Keys and what the user types pass through the same
function, [`normalize.py`](../build/refbuild/normalize.py), whose
docstring is the specification. It lowercases, reads numbers in every form
MOS accepts and writes them in decimal (hex after `rst`), puts `vdu` in
front of a leading number, and applies *skips*. A skip turns the parameter
a command family names before its number into `_`: the buffer ID of
`VDU 23, 0, &A0` and the channel of `VDU 23, 0, &85`. That way
`VDU 23,0,&A0,1;6` and `vdu 23 0 160 1 0 6` both become
`vdu 23 0 160 _ 6`. Every key normalises to itself.

**Rendering.** [`render.py`](../build/refbuild/render.py) turns each topic
into ref's markup, a byte stream the viewer lays out at any width:

    block:   kind|flags  indent  hang  content...  END
    kinds:   PARA (wrapped), HEAD (a heading), PRE (code and tables, never
             wrapped), RULE
    flags:   SPACE (a blank line before), NOTE (drawn with a bar)
    inline:  EM on/off, CODE on/off, LINK lo hi ... LINK_END, BREAK

Control bytes are all below 0x20. The text is in the Agon's character set
(its system font is Windows-1252 above 0x7F, nearly), and anything the font
lacks is replaced. Tables are laid out here, as ruled columns 76 wide with
cells wrapped inside them, because a column's width needs the whole table.
A topic shown on its own, without descendants that would pass 16 KB, ends
with links to its children.

**Layout.** [`layout.py`](../build/refbuild/layout.py) lays markup out into
lines. The C must produce exactly the same lines, and the tests compare
them.

**Full text.** [`fulltext.py`](../build/refbuild/fulltext.py) indexes
every word of three letters or more, minus stop words, with the topics it
appears in and whether it is in their titles. It also holds the reference
search: topics are ranked by query words in their title, then by query
words they contain, then in book order.

**The file.** [`refdat.py`](../build/refbuild/refdat.py) compresses each
topic as an LZ4 block ([`lz4.py`](../build/refbuild/lz4.py)) whose matches
may reach into an 8 KB dictionary of strings common to many topics, and
writes the file. [FORMAT.md](FORMAT.md) specifies every byte; in short:

| Section | Holds |
|---|---|
| header | magic, version, the agon-docs commit and date, the wiki's title, every section's offset, the buffer sizes the viewer needs |
| nodes | the contents tree, in preorder |
| topics | each topic's title, compressed text, parent and span (how many topics after it are shown with it) |
| titles | every title once |
| text | each topic, compressed on its own |
| keys, key directory | the keys, sorted, front-coded in blocks of 64; every block's first key, kept in memory |
| words, word directory | the full-text index, laid out the same way |
| normalising settings | hex-after words, skips and stop words, so the viewer normalises exactly as the builder did |
| dictionary | the shared compression dictionary |

The build is deterministic: the same source gives the same bytes.
[`report.py`](../build/refbuild/report.py) writes `out/report.txt`, which
lists the contents, the keys, the gaps and the sizes.

## The reader core

[`src/core`](../src/core) is portable C (stdio, string and stdlib only),
and knows one document at a time.
It builds for the Agon with agondev and with acc, and on a PC for the
tests. [`refcore.h`](../src/core/refcore.h) is its API.

- [`refcore.c`](../src/core/refcore.c) opens a document. It reads the header,
  checks the sections fit together, and loads the small tables (topics,
  directories, settings, dictionary). It decompresses a topic and the
  topics shown with it into one view buffer, and its LZ4 decoder lets
  matches reach back into the dictionary.
- [`normalize.c`](../src/core/normalize.c) is `normalize.py` in C, step for
  step, configured from the file's settings.
- [`keys.c`](../src/core/keys.c) does lookups: an exact key, else the
  longest key the query starts with (a whole token at a time, or a name up
  to its `$`), else every key that starts with the query. A binary search
  of the in-memory directory finds the block; a buffered reader scans it.
- [`search.c`](../src/core/search.c) is the full-text search, ranked as
  `fulltext.py` ranks.
- [`layout.c`](../src/core/layout.c) lays a topic out in one pass. Each
  line records the style and link open where it starts, so the viewer can
  draw any line on its own. Runs of plain text are crossed by two small
  leaf functions that the compiler keeps in registers, which is what makes
  the largest topic quick to show.

Buffers are allocated by `ref_open` and sized from the header. Large ones
are static or on the heap: agondev's stack frames are kept under 128 bytes,
which [`test/frames.sh`](../test/frames.sh) enforces.

## The viewer

[`src/viewer`](../src/viewer), built by agondev
([`agon.mk`](../agon.mk)). [`ui.h`](../src/viewer/ui.h) lists the parts.

- [`main.c`](../src/viewer/main.c) parses the command line: options
  anywhere, the first other word the document, the rest the query. It
  opens the document, looks the query up (searching if no key matches,
  unless `-k`) and then prints, times or shows. Plain `ref` browses
  instead. It returns 0 when something was shown and 100 when nothing
  was, never 1, 4 or 5, which MOS reports as errors of its own.
- [`shelf.c`](../src/viewer/shelf.c) finds the documents in
  `/config/ref/docs`, reading the directory entry by entry, with each
  document's title from its header, and shows them to choose from. Esc
  from a document's contents, with no topic open, comes back here; the
  views are reset before the next document opens.
- [`nav.c`](../src/viewer/nav.c) moves between the views (a topic, the
  contents, a list, search) until the user quits. Each view returns where
  to go next.
- [`doc.c`](../src/viewer/doc.c) holds the topic laid out into lines and
  answers questions about it: the link at a position (with every line it
  wraps onto), a heading's extent, the next link or heading, and find. It
  knows nothing about the screen.
- [`view.c`](../src/viewer/view.c) is a topic on the screen: the cursor,
  what it lights, drawing, the keys and the history. The history holds the
  cursor's place, and also a mark for the view a topic was opened from, so
  Backspace goes back there. After a move it redraws as little as it can
  (see the screen below).
- [`toc.c`](../src/viewer/toc.c) is the contents tree,
  [`pick.c`](../src/viewer/pick.c) the lists and search-as-you-type,
  [`help.c`](../src/viewer/help.c) the help screen,
  [`batch.c`](../src/viewer/batch.c) `-p` and `-T`.
- [`input.c`](../src/viewer/input.c) reads key events from agondev's queue,
  each with the modifiers as they were when the key went down, and maps
  every spelling of a key to what it means.
- [`screen.c`](../src/viewer/screen.c) draws.

**The screen.** The top row is a header with the document's title, then the
text rows inside a margin each side, then a status line. Each row is drawn
whole and sent in one RST 18h call. Colours are chosen by role and mapped
for 16-, 4- and 2-colour modes.

- **The last column.** Nothing is ever printed in the last column, so the
  VDP never wraps or scrolls. The header and status line still reach it:
  that cell is painted as a one-cell text viewport cleared in the bar's
  colours (`VDU 28`, `VDU 12`, `VDU 26`).
- **Scrolling.** A move of up to three lines scrolls the text rows on the
  VDP, inside a viewport above the status line (`VDU 23,7`), and draws
  only the rows that came into view.
- **Colour between rows.** Each row starts in the text colour, so a row
  that ends lit doesn't light the next one's padding.

**The scripted build.** Built with `REF_SCRIPT`, ref reads its keys from
`/ref.keys` on the card and writes a transcript to `/ref.out`: what each
view did, screen dumps, the VDP's own screen read back, and cell colours.
That is all in [`script.c`](../src/viewer/script.c). The release build
reaches it only through two macros, `SCRIPTED(...)` and `LOG(...)`, which
compile to nothing there.

## Testing

- `make test` runs the builder's tests ([`build/tests`](../build/tests)):
  the parser, the topic model, keys, normalising, rendering, layout, LZ4
  and the file. It includes a test over the whole wiki that every link
  resolves, 70-odd lookups reach their topics, and the build is
  reproducible. It then builds the core for the PC
  ([`test/host/reftool.c`](../test/host/reftool.c)) with the address and
  undefined-behaviour sanitisers, and holds it against the builder: every
  key, hundreds of lookups and searches, every topic byte for byte, and
  every layout line for line at two widths. Normalising cases shared by
  both sides are in
  [`build/tests/normalize.json`](../build/tests/normalize.json).
- `make emu` ([`test/run.sh`](../test/run.sh)) runs ref in
  fab-agon-emulator on MOS 3.0.2, driven as the harness in
  [`test/emu.sh`](../test/emu.sh) describes. The command goes in
  `autoexec.txt` and stdin is held open by a fifo, never typed into. The
  checks cover the release build's printing and exit codes, every view
  through the scripted build, ade's F1 under hub through a stand-in client
  ([`test/agon/hubf1`](../test/agon/hubf1)), the screen read back from the
  real VDP on the full emulator in three modes, the heap's peak (measured
  with zap's zmalloc shim), and the timings, taken from MOS's own clock.
- [`test/core.sh`](../test/core.sh) builds the PC test tool for the Agon
  with agondev and with acc. It runs every mode in the emulator and
  requires the same answers as the PC, byte for byte.

Every check has been confirmed to fail when the line it covers is broken.

## Things to know

- **A compiler bug.** agondev's clang 15.0.7 can compile `n = *p++` so
  that the load happens after the increment. [`agon.mk`](../agon.mk)
  builds with `-mllvm -twoaddr-reschedule=false`, which avoids it.
  `test/core.sh` catches it if it returns.
- **Memory.** About 40 KB of code and 75 KB of statics, and a heap that
  peaks at about 85 KB (`make agon-heap` measures it): well inside the
  448 KB a program has.
- **Speed**, in the emulator at the Agon's own speed: a lookup takes
  6-14 ms, a search about 40 ms, and opening the largest topic (24 KB of
  tables) under 200 ms. `ref -T` measures them on the machine.
- **The license.** agon-docs has none yet, so mos.dat can't be published.
  [`mkrelease.sh`](../mkrelease.sh) refuses to build a release until
  `REF_LICENSE_SETTLED=1` says the license is settled.
