# The document format

A document is one file, specified byte by byte in
[docs/FORMAT.md](https://github.com/avalonbits/ref/blob/main/docs/FORMAT.md),
which any tool can write. In short:

| Section | Holds |
|---|---|
| header | the format's magic and version, the document's title, every section's offset, and the buffer sizes ref needs |
| nodes | the contents tree, in preorder: sections, pages, headings |
| topics | each topic's title, text, parent, and how many after it are shown with it |
| titles | every title once |
| text | each topic's markup, an LZ4 block |
| keys, key directory | the keys, sorted, front-coded in blocks of 64 |
| words, word directory | the search's index, laid out the same way |
| normalising settings | how keys and queries are normalised, so that ref reads them as they were made |
| dictionary | strings that any topic's LZ4 may refer to |

## Markup

A topic's text is ref's markup: blocks -- a paragraph, a heading, code or
a table, a rule -- each with its indent and whether it is in a note, and
inside them emphasis, code, links and line breaks, as bytes below 0x20.
ref lays it out at any width.
