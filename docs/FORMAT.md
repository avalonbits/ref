# The ref document format, version 1

A ref document is one file that holds a whole book: its pages split into
topics, the contents tree, the links between topics, a keyword index and a
full-text index. A reader reads it with seeks and never loads it whole, so a
book can be larger than the Agon's memory.

ref reads any file in this format. ref's own builder (`build/`) writes
agon-docs as `mos.dat`; another project can write its own documentation in
the same format with any tool.

## Where documents live

A document is installed as

    /config/ref/docs/<name>.dat

and `ref <name>` opens it. The name is the file's name without `.dat`: one
to eight lower-case letters and digits, so it also fits MOS's short file
names. `ref` alone lists the documents in that directory.

## Conventions

- Integers are little-endian: u8, u16, u32.
- Offsets are from the start of the file unless a section says otherwise.
- A *string* is a length byte, then that many bytes, at most 255.
- **Text** is in the Agon's character set: printable ASCII (0x20-0x7E),
  and 0xA0-0xFF and a few code points in 0x80-0x9F as the VDP's system
  font draws them (Windows-1252, nearly: 0x8D up arrow, 0x8F left arrow,
  0x90 right arrow). 0x7F and control characters never appear in text.
- `NONE` is 0xFFFF.
- A *varint* is seven bits a byte, low bits first, with the top bit set on
  every byte but the last.

## Layout

    header         128 bytes
    nodes          the contents tree
    topics         one record per topic
    titles         every title, once
    text           every topic's markup, compressed
    keys           the keyword index
    key directory
    words          the full-text index
    word directory
    settings       how keys and queries are normalised
    dictionary     the compression dictionary

The sections are in this order. Each ends where the next begins, and the
dictionary ends at the end of the file. A reader should check this, and
reject a file whose sections overlap or overrun.

## Header

| Offset | Size | Field |
|---|---|---|
| 0 | 8 | magic: `AGONREF` and 0x1A |
| 8 | 2 | format version: 1 |
| 10 | 2 | flags: bit 0, the book has a title (offset 104); other bits 0 |
| 12 | 12 | the source's revision, ASCII, NUL-padded (ref's builder writes the first 12 hex digits of the agon-docs commit) |
| 24 | 12 | the source's date, `YYYY-MM-DD`, NUL-padded |
| 36 | 2 | topics |
| 38 | 2 | contents nodes |
| 40 | 2 | key blocks |
| 42 | 2 | word blocks |
| 44 | 4 | keys |
| 48 | 4 | words |
| 52 | 4 | nodes offset |
| 56 | 4 | topics offset |
| 60 | 4 | titles offset |
| 64 | 4 | text offset |
| 68 | 4 | keys offset |
| 72 | 4 | key directory offset |
| 76 | 4 | words offset |
| 80 | 4 | word directory offset |
| 84 | 4 | settings offset |
| 88 | 4 | dictionary offset |
| 92 | 2 | dictionary length |
| 94 | 2 | view size: the most bytes of markup any topic shows, with the topics shown with it |
| 96 | 2 | the largest compressed topic, in bytes |
| 98 | 2 | 0 |
| 100 | 4 | file size |
| 104 | 4 | the book's title, an offset into titles, when flag bit 0 is set |
| 108 | 20 | 0 |

A reader allocates its buffers from the view size and the largest
compressed topic.

## Nodes

The contents tree, 10 bytes a node, in preorder: a node's first child is
the node after it. Node 0 is the first at the top level.

| Size | Field |
|---|---|
| 4 | title: an offset into titles |
| 2 | the topic it opens, or NONE for a section heading that opens none |
| 2 | first child, or NONE |
| 2 | next sibling, or NONE |

## Topics

16 bytes a topic, in book order. A topic is a heading and the text under it
up to the next heading of any level. Its descendants (the headings nested
under it, and theirs) are numbered straight after it.

| Size | Field |
|---|---|
| 4 | title: an offset into titles |
| 4 | text: an offset from the text section's start |
| 2 | compressed length |
| 2 | length, uncompressed |
| 2 | parent topic, or NONE |
| 2 | span: how many topics after this one are shown with it |

When a topic is shown, a reader shows its own text followed by that of the
next `span` topics. A writer sets the span to the number of descendants
when they fit in the view, and to 0 otherwise. A topic shown without its
descendants should end with links to its children.

## Titles

Strings, each title once. Nodes, topics and the header refer to them by
offset.

## Text

Each topic's markup, compressed on its own as an LZ4 block (LZ4's block
format: a token byte with the literal count in its high nibble and the
match length less 4 in its low; more length bytes while 255; the literals;
a 2-byte offset; more match-length bytes while 255; the last sequence
literals only). A match may reach back past the start of the topic into
the dictionary, which, as far as offsets go, sits just before it. A
document with no dictionary has length 0 there.

### Markup

A topic's markup is a run of blocks:

    kind|flags  indent  hang  content...  0x00

| Kind (low 4 bits) | Is | indent | hang |
|---|---|---|---|
| 1 PARA | text, wrapped to the width | first line's column | following lines' column |
| 2 HEAD | a heading, wrapped | its level, 1-4 | 0 |
| 3 PRE | lines ended by 0x0A, laid out as they are, never wrapped | column | 0 code, 1 table |
| 4 RULE | a line across, no content | column | column |

| Flag | Means |
|---|---|
| 0x40 SPACE | a blank line before the block (dropped at the very top) |
| 0x80 NOTE | the block is inside a note, drawn with a bar two columns left of its indent |

Inside the content, besides text:

| Byte | Means |
|---|---|
| 0x10, 0x11 | emphasis on, off |
| 0x12, 0x13 | inline code on, off |
| 0x14 lo hi | a link to topic lo + 256 × hi, up to 0x15 |
| 0x15 | the link's end |
| 0x0B | a line break inside a paragraph |

A link's two topic bytes can be any value, 0x00 included, so a reader
scanning for a block's end must step over them. Every style and link is
closed inside the block that opens it. A run of spaces is one space.

A PARA or HEAD wraps at spaces. A word wider than the line is cut at its
edge, and the controls take no room. ref's builder lays tables out itself,
as PRE blocks of ruled columns no wider than 76, so a reader needs no table
layout of its own.

## Keys

The keyword index: what `ref <name> <query>` looks up. Keys are sorted by
their bytes, then by topic, in blocks of 64:

| Size | Field |
|---|---|
| 1 | shared: bytes in common with the key before (0 for a block's first) |
| 1 | the length of the rest |
| n | the rest |
| 2 | the topic |

The same key may lead to several topics. The **key directory** holds, for
each block, its offset from the keys section's start (u32) and its first
key (a string), so a reader can keep the directory in memory, binary-search
it for a block, and scan just that block.

Every key is *normalised* (see below), and a reader normalises a query the
same way before looking it up. ref looks up an exact key first. Failing
that, it takes the longest key the query starts with, a whole word at a
time or a name up to its `$`, but never `vdu` alone. Failing that, it lists
every key that starts with the query.

### Normalising

Keys and queries are normalised with the settings section:

1. Lower case, ASCII letters only.
2. Split into tokens at spaces, tabs and commas. A semicolon also ends a
   token and marks it *wide*; a semicolon after a space marks the token
   before.
3. From each token, drop an inner placeholder (`<...>` after its first
   character), then strip `( ) [ ] { } < > " ' `` ` `` : * . ! ? |` from both
   ends. Tokens left empty are dropped.
4. Numbers are written in decimal: `12`, `&C`, `0xC`, `0Ch` (a digit
   first), and `16_C` (a base 2-36, an underscore, digits in that base).
   After a token listed in the settings' hex-after words, bare digits are
   hex, and the number is written in lower-case hex instead. A value above
   0xFFFFFF is not a number.
5. If the first token is a number, `vdu` goes before it.
6. The longest skip whose prefix the tokens start with applies: each of its
   units after the prefix becomes `_`. A unit of width 2 takes two tokens
   when both are numbers and the first is not wide; any other unit takes
   one token.
7. Trailing `_` tokens are dropped.
8. The tokens are joined with single spaces.

A writer must make every key normalise to itself.

## Words

The full-text index, in blocks of 64 words, sorted and front-coded as keys
are. After each word come its topics: a count (varint), then for each topic
`varint(gap << 1 | in_title)`. The gap is the topic number less the one
before it (for the first, the topic number itself), and `in_title` is 1
when the word is in the topic's title. The **word directory** is laid out
as the key directory is.

ref's builder indexes every word of three letters or more that is not a
stop word, where a word is a run of `a-z 0-9 _ $`. A query is split into
words the same way, and words of two letters or more count, up to eight. A
search finds the indexed words that start with each query word and ranks
the topics: by query words in their title, then by query words they
contain, then in book order.

## Settings

| Field | |
|---|---|
| u8 count, then strings | hex-after words (ref's: `rst`) |
| u8 count, then for each skip | its prefix (a string, normalised), a u8 count of units, and the units (u8 each, 1 or 2) |
| u8 count, then strings | stop words, sorted |

## Versions

A reader rejects a file whose magic is wrong or whose version it does not
know. Within version 1, new fields may only use the reserved bytes and
flag bits, whose zero value must mean "absent", so an older reader can
still read a newer file.
