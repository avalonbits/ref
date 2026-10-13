# ref

ref reads documentation on the Agon. Each document is one file on the SD
card, and ref reads it, follows its links and searches it. The first is
`mos`: the community Agon documentation,
[agon-docs](https://github.com/AgonPlatform/agon-docs), the whole wiki in
one file. ref runs on the Agon Light, Light 2, Origins and Console8, under
MOS 3.0.2.

## Installing

Download `ref-0.4.1.zip` from the
[latest release](https://github.com/avalonbits/ref/releases/latest) and
unzip it at the root of the SD card. Everything lands where ref looks for
it:

    /bin/ref.bin
    /bin/md2ref.bin                 to write documents (see below)
    /config/ref/docs/mos.dat        agon-docs, at the commit in SOURCE
    /config/ref/docs/ref.dat        ref's own book: `ref ref`
    /config/ref/docs/VERSION        ref's version, and which agon-docs
    /config/ref/docs/<name>.dat     any other document

`ref-builder-0.4.1.zip` is the builder, to make mos.dat again on a PC or
Mac (see [Building](#building)).

A document's name is its file's, without `.dat`: one to eight lower-case
letters and digits. ref also finds documents in the current directory,
where a program can keep its own: run there, `ref <name>` opens
`<name>.dat` in it before one of the same name in `/config/ref/docs`, and
`ref` lists both, the current directory's marked `.`. Any project can ship its documentation as a document:
write it in Markdown and compile it with md2ref, or write the format,
specified in [docs/FORMAT.md](docs/FORMAT.md), with any tool.

ref has a book of its own, `ref`: its keys, its options and md2ref's, and
the Markdown md2ref reads. `ref ref`, or `ref ref <word>`.

## Looking things up

    ref                     the documents here and installed, to choose one
    ref <doc>               a document's contents, organised as its source is
    ref <doc> <query>       look it up; search the text if nothing matches
    ref <doc> -k <query>    look it up, and nothing else
    ref <doc> -s <words>    search the text
    ref -p ...              print instead of showing: what was found, or,
                            alone, the documents with their titles
    ref -f <file> ...       a document by its path instead of its name; a
                            <doc> with a / or ending .dat is a path too
    ref <a>,<b>,... <query> the first of the documents with an answer;
                            ones that are missing are passed over
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
and Enter opens a topic. Ctrl+F finds words in the titles there too,
opening the section a match is in; Ctrl+N and Ctrl+P go to the next and
the one before.

ref uses the screen mode and font already in use; under hub, the prompt's.

## Writing a document

md2ref compiles Markdown pages into a document, on the Agon or on a PC:

    md2ref <name> [<dir> | <page.md>... | @list]
    md2ref -o <file.dat> <dir> | <page.md>... | @list

`md2ref myprog` writes `ref-doc/myprog.dat`, making `ref-doc` if it is not
there, from the `.md` files in the current directory. Name a directory or
pages after it to read those instead: a directory's `.md` files are read
with `index.md` or `README.md` first and the rest by name, pages in the
order given. The name is the one ref will know it by: one to eight
lower-case letters and digits. `-o` writes the document anywhere else,
such as `/config/ref/docs/myprog.dat` to install it for everyone.

`@list` reads the pages' paths from a file, one a line, for more than a
command line holds. `-t` sets the title (else the first page's first
heading), `-w` makes a link that leads nowhere a warning instead of an
error, and `-e <file>` writes the errors and warnings to that file as well
as the console -- the file is made even when there are none. `-f` puts
every word in the search's index, not only the names (below), and `-z`
compresses each topic: both make md2ref slower on the Agon, `-z` by about
two fifths, for a document about 30% smaller.

ade compiles a program's Markdown, wherever it is, into the program's
`ref-doc/`; `ref myprog`, run in `ref-doc/`, reads it, and
`ref ref-doc/myprog.dat,mos -k <word>` asks it first and mos after.

- **Topics** are the headings, nested by level. A page that opens with its
  only `#` heading is that topic; otherwise the page is a topic titled
  from its file name, with its headings under it. The document's title is
  the first page's first heading, or `-t`'s.
- **A menu**: a directory with an `mkdocs.yml` beside its pages is read as
  MkDocs reads it, and as ref's builder makes mos.dat. Its `nav` gives the
  contents: sections, which open no topic, and the pages under them, in
  its order, each titled by the nav where the nav titles it. Pages the nav
  leaves out come last, under "Pages outside the menu", and `site_name` is
  the title unless `-t` gives one. Links name a page by its file, in any
  section:

      site_name: Agon C library
      nav:
        - Start: index.md
        - Input and output:
            - stdio.md
            - wchar.md
        - Strings:
            - string.md
- **Keys**: a heading is found by each code span in it, by the VDU
  sequence it names, and by the words of its title. `` ## `strtol`,
  `strtoul` `` makes `ref <doc> strtoul` go straight to it. The search
  covers the names: each topic's title and headings, and what is in a
  code span, a link's label or an emphasis -- in a table too. With `-f` it
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

md2ref reads each page once, through a small window, as acc and zap read
source, and writes each topic as soon as it is read: memory holds the
outline -- titles, anchors, keys -- and one topic, however long the pages.
All of agon-docs, 637 KB of Markdown, compiles on the Agon in about 21
seconds and 220 KB of memory. While it works it keeps four or five scratch
files beside the document (`<name>.dat~t`, `~w`, `~k`, `~l`, and `~r` when
the words outgrow memory), and removes them when it is done. One block
-- a code block, a table, a list -- can be up to 32 KB.

## Building

mos.dat is built on a PC or Mac with Python 3.11 or later, from agon-docs
at the commit pinned in [`SOURCE`](SOURCE). ref.bin is built with
[agondev](https://github.com/AgonPlatform/agondev).

    make dat            fetch agon-docs and write out/mos.dat
    make book           ref's own book, docs/ref, as out/ref.dat
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

## License

ref, md2ref and the builder are free software, under the GNU General
Public License, version 3 only (GPL-3.0-only): see [LICENSE](LICENSE). A
document is its source's: mos.dat is agon-docs' text, under agon-docs'
terms, and what md2ref makes from your Markdown is yours.
