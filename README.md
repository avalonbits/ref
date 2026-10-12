# ref

ref reads documentation on the Agon. Each document is one file on the SD
card, and ref reads it, follows its links and searches it. The first is
`mos`: the community Agon documentation,
[agon-docs](https://github.com/AgonPlatform/agon-docs), the whole wiki in
one file. ref runs on the Agon Light, Light 2, Origins and Console8, under
MOS 3.0.2.

## Installing

Copy ref, and each document, to the SD card:

    /bin/ref.bin
    /bin/md2ref.bin                 to write documents (see below)
    /config/ref/docs/mos.dat
    /config/ref/docs/<name>.dat     any other document

There is no release to download yet. mos.dat is the wiki's own text, and
agon-docs has no license, so until its maintainers settle one, build
mos.dat yourself for your own card (see [Building](#building)).

A document's name is its file's, without `.dat`: one to eight lower-case
letters and digits. ref also finds documents in the current directory,
where a program can keep its own: run there, `ref <name>` opens
`<name>.dat` in it before one of the same name in `/config/ref/docs`, and
`ref` lists both, the current directory's marked `.`. Any project can ship its documentation as a document:
write it in Markdown and compile it with md2ref, or write the format,
specified in [docs/FORMAT.md](docs/FORMAT.md), with any tool.

## Looking things up

    ref                     the documents here and installed, to choose one
    ref <doc>               a document's contents, organised as its source is
    ref <doc> <query>       look it up; search the text if nothing matches
    ref <doc> -k <query>    look it up, and nothing else
    ref <doc> -s <words>    search the text
    ref -p ...              print instead of showing: what was found, or,
                            alone, the documents with their titles
    ref -f <file> ...       a document by its path instead of its name
    ref -v [doc]            ref's version, and a document's source

In `mos`, a lookup knows:

| Ask for | Like |
|---|---|
| VDU sequences, numbers written any way MOS reads them | `ref mos vdu 23 27 3`, `ref mos 23,27,&21` |
| MOS API calls, by name or number | `ref mos mos_fopen`, `ref mos mos 0x0a` |
| RST calls | `ref mos rst 08h`, `ref mos rst 8` |
| C functions | `ref mos f_printf` |
| star commands and their aliases | `ref mos cd`, `ref mos dir` |
| system variables | `ref mos run$path`, `ref mos hotkey$1`, `ref mos sysvar_scrmode` |
| PLOT codes | `ref mos plot 85`, `ref mos vdu 25 85` |
| BBC BASIC keywords | `ref mos gcol` |
| any word of a heading | `ref mos copper` |

Case doesn't matter, and the parameters a command names before its number
can be anything: `ref mos vdu 23 0 &a0 7; 6` finds buffered command 6 whichever
buffer it names. A lookup that finds one topic opens it; more than one, a
list to choose from.

ref returns 0 when it showed something and 100 when nothing matched (or
there is no such document), so a script can tell. ade's F1 runs
`ref mos -k <word>` through hub this way.

## Reading

The screen has the document's title across the top, the text in the middle, and
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
| `t` | the contents (Esc there, with no topic open, goes back to the documents) |
| `/`, F3 | search: each key typed narrows the list, matching keys first |
| `?`, F1 | the keys |
| Esc, `q` | quit |

In the contents, the arrows move, Right and Left open and close a section,
and Enter opens a topic.

ref uses the screen mode and font already in use; under hub, the prompt's.

## Writing a document

md2ref compiles Markdown pages into a document, on the Agon or on a PC:

    md2ref -o <name>.dat [-t <title>] [-w] <dir>
    md2ref -o <name>.dat [-t <title>] [-w] <page.md>...

`-o` is the document to write, and the rest is the Markdown to read: a
directory, whose `.md` files are read with `index.md` or `README.md` first
and the rest by name, or pages in the order given. `-t` sets the title
(else the first page's first heading); `-w` makes a link that leads
nowhere a warning instead of an error.

    md2ref -o /config/ref/docs/myprog.dat docs      install it for everyone
    md2ref -o ref-doc/myprog.dat docs               keep it with the program

ade compiles a program's Markdown, wherever it is, into the program's
`ref-doc/` directory; `ref myprog`, run in `ref-doc/`, reads it.

- **Topics** are the headings, nested by level. A page that opens with its
  only `#` heading is that topic; otherwise the page is a topic titled
  from its file name, with its headings under it. The document's title is
  the first page's first heading, or `-t`'s.
- **Keys**: a heading is found by each code span in it, by the VDU
  sequence it names, and by the words of its title. `` ## `strtol`,
  `strtoul` `` makes `ref <doc> strtoul` go straight to it. The search
  covers every word of every topic.
- **Links** between pages, `[text](page.md#anchor)`, are followed in ref.
  An anchor is a heading's, made as MkDocs makes them, or one set with
  `{#id}` on a heading, paragraph or table cell. Links to the web stay as
  their text and URL.
- **Markdown**: headings (`#` and underlined), paragraphs, emphasis, code
  spans, fenced and indented code, lists, nested, block quotes, tables,
  rules, footnotes, MkDocs notes (`!!!`, `???`), and `<br>`, `<b>`, `<i>`,
  `<em>` and `<kbd>`. Other HTML shows as it is written.

md2ref stops at the first page with an error -- a link that leads nowhere,
a code block never closed -- and writes nothing. Errors read
`file:line: error: ...`, and md2ref returns 100, so an editor can go to the
line. `-w` makes a link that leads nowhere a warning.

On the Agon, md2ref reads the pages twice, one at a time, and holds only
what it is making, compressed. agon-docs' 105 KB of top-level pages
compile in 245 KB of memory, and each of its pages compiles on its own,
the largest 96 KB; its 193 KB `mos/` folder, which has an 89 KB page, is
more than fits.

## Building

mos.dat is built on a PC or Mac with Python 3.11 or later, from agon-docs
at the commit pinned in [`SOURCE`](SOURCE). ref.bin is built with
[agondev](https://github.com/AgonPlatform/agondev).

    make dat            fetch agon-docs and write out/mos.dat
    make agon           bin/ref.bin
    make agon-md2ref    bin/md2ref/md2ref.bin
    make host           md2ref for the PC: out/host/md2ref
    make test           the builder's and md2ref's tests, and the reader
                        core on the PC
    make emu            ref in fab-agon-emulator
    test/core.sh        the reader core on the Agon, built by agondev and acc
    test/md2ref.sh      md2ref on the Agon, against the PC

When the wiki changes, move the commit in `SOURCE` and run `make dat`. The
builder stops on a link it cannot resolve and lists any heading its rules,
in [`build/rules.toml`](build/rules.toml), no longer turn into a key.
ref.bin only needs rebuilding if the file format changes.

[docs/DESIGN.md](docs/DESIGN.md) explains how ref works.
