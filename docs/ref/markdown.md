# The Markdown md2ref reads

How md2ref is run is in [Using md2ref](md2ref.md).

## Topics

A topic is a heading and the text under it, up to the next heading of any
level; headings nest by level. A page that opens with its only `#`
heading is that topic. Otherwise the page is a topic of its own, titled
from its file name (or its menu, [Sections, from `mkdocs.yml`](nav.md)),
with its headings under it.

## Keys

A heading is found by each code span in it, by the VDU sequence it names,
and by the words of its title. `` ## `strtol`, `strtoul` `` makes
`ref <doc> strtoul` go straight to it, and `## VDU 23, 0, &A0` to
`ref <doc> vdu 23 0 160`.

## Links

Links between pages, `[text](page.md#anchor)`, are followed in ref, and
name a page by its file in any directory or section. An anchor is a
heading's, made as MkDocs makes them, or one set with `{#id}` on a
heading, a paragraph or a table's cell. Links to the web stay as their
text and URL.

## What it reads

Headings (`#` and underlined), paragraphs, emphasis, code spans, fenced
and indented code, lists, nested, block quotes, tables, rules, footnotes,
MkDocs notes (`!!!`, `???`), and `<br>`, `<b>`, `<i>`, `<em>` and `<kbd>`.
Other HTML shows as it is written.

## Tables

A table is laid out as ruled columns 76 wide, its cells wrapped inside
them, a cell's styles and links kept on every line of it. When any body row
wraps, the rows are ruled apart.

## A topic's size

A topic's text may be up to 64 KB. A topic that would show more than 16 KB
with what is under it is shown on its own, and ends with links to its
children.
