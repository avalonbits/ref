# Using md2ref

md2ref compiles Markdown pages into a document for ref, on the Agon or on
a PC, the same document byte for byte on both. Its options are in
[md2ref's options](md2ref-options.md), the Markdown it reads in
[The Markdown md2ref reads](markdown.md).

    md2ref <name> [<dir> | <page.md>... | @list]
    md2ref -o <file.dat> <dir> | <page.md>... | @list

## `ref-doc`: a program's own

`md2ref myprog` writes `ref-doc/myprog.dat`, making `ref-doc` if it is not
there, from the `.md` files in the current directory. `ref myprog`, run in
`ref-doc`, reads it. The name is the one ref knows the document by: one
to eight lower-case letters and digits. ade compiles a program's Markdown
this way, into the program's `ref-doc`.

## The pages

Name a directory, or pages, after the name to read those instead. A
directory's `.md` files are read with `index.md` or `README.md` first and
the rest by name; pages named are read in the order given. A directory
with an `mkdocs.yml` is read by its menu: see
[Sections, from `mkdocs.yml`](nav.md).

## Errors

md2ref stops at the first page with an error -- a link that leads nowhere,
a code block never closed -- and writes nothing. Errors read
`file:line: error: ...`, and md2ref returns 100, so an editor can go to the
line.

## How it works

md2ref reads each page once, through a small window, and writes each topic
as soon as it is read: memory holds the outline -- titles, anchors, keys
-- and one topic, however long the pages. All of agon-docs, 637 KB of
Markdown, compiles on the Agon in about 21 seconds and 220 KB of memory.
While it works it keeps four or five scratch files beside the document
(`<name>.dat~t`, `~w`, `~k`, `~l`, and `~r` when the words outgrow
memory), and removes them when it is done. One block -- a code block, a
table, a list -- can be up to 32 KB. It ends with `Done in N.NN seconds`.
