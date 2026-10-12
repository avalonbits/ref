"""Topics rendered into ref's markup: the byte stream the viewer lays out.

A topic's text is a run of blocks. Each block is

    kind|flags  indent  hang  content...  END

kind (low 4 bits):
    PARA  text wrapped to the screen: its first line at column `indent`, the
          rest at `hang` (a list item's text hangs after its bullet)
    HEAD  a heading; `indent` is its level, 1-4
    PRE   lines ended by '\\n', laid out as they are and never wrapped -- the
          viewer scrolls them sideways; `hang` is 0 for code, 1 for a table
    RULE  a line across the screen (no content)
flags:
    SPACE a blank line before the block
    NOTE  inside a note (an admonition, or a quote): drawn with a bar

Inside PARA, HEAD and PRE content, besides text:
    EM_ON EM_OFF     emphasis (strong and em alike)
    CODE_ON CODE_OFF inline code
    LINK lo hi       a link to topic lo + 256*hi, up to LINK_END
    BREAK            a line break inside a paragraph

Control bytes are all below 0x20. Text is the Agon's character set: ASCII,
and 0x80-0xFF as the VDP's system font draws them (Windows-1252, nearly):
characters it lacks are replaced here, so the viewer prints bytes as they
come. Tables are laid out here, because laying out a column needs the whole
table: as PRE blocks of ruled columns, cells wrapped within their columns
to fit TABLE_WIDTH. Links inside cells stay links.
"""

import re

from . import markdown as md

END = 0x00
PARA, HEAD, PRE, RULE = 1, 2, 3, 4
SPACE, NOTE = 0x40, 0x80
EM_ON, EM_OFF, CODE_ON, CODE_OFF = 0x10, 0x11, 0x12, 0x13
LINK, LINK_END = 0x14, 0x15
BREAK = 0x0B

TABLE_WIDTH = 76         # 80 columns less the viewer's margins
PRE_CODE, PRE_TABLE = 0, 1

# Characters the builder replaces, and characters the font has above 0x7F.
REPLACE = {
    '‘': "'", '’': "'", '“': '"', '”': '"',
    '–': '-', '—': '--', '…': '...', '•': '*',
    '⇐': '\x8f', '⇨': '\x90', '⇧': '\x8d', '⇓': 'v',
    '←': '\x8f', '→': '\x90', '↑': '\x8d', '↓': 'v',
    ' ': ' ', '\t': '    ',
}


def agon_bytes(text):
    """text in the Agon's character set."""
    out = bytearray()
    for ch in text:
        ch = REPLACE.get(ch, ch)
        for c in ch:
            o = ord(c)
            if 0x20 <= o < 0x7f or o == 0x0a:
                out.append(o)                   # newlines only reach PRE
            elif o in (0x8d, 0x8f, 0x90):
                out.append(o)                   # arrows, placed above
            elif 0xa0 <= o <= 0xff:
                out.append(o)                   # Latin-1: the font has them all
            else:
                try:
                    b = c.encode('cp1252')
                except UnicodeEncodeError:
                    b = b'?'
                out.extend(b if b and b[0] >= 0x20 else b'?')

    return bytes(out)


def _one_space(s):
    """Runs of spaces as one: the layout counts a run as one column."""
    return re.sub(r'[ \t]{2,}', ' ', s)


class Renderer:
    def __init__(self):
        self.out = bytearray()

    def block(self, kind, indent, hang, content=b'', space=False, note=False):
        k = kind | (SPACE if space else 0) | (NOTE if note else 0)
        self.out += bytes([k, min(indent, 255), min(hang, 255)]) + content + bytes([END])

    # --- inlines ---------------------------------------------------------------

    def inlines(self, nodes):
        b = bytearray()
        for nd in nodes:
            t = nd[0]
            if t == 'text':
                b += agon_bytes(_one_space(nd[1]))
            elif t == 'code':
                b += bytes([CODE_ON]) + agon_bytes(_one_space(nd[1])) + bytes([CODE_OFF])
            elif t in ('em', 'strong'):
                b += bytes([EM_ON]) + self.inlines(nd[1]) + bytes([EM_OFF])
            elif t == 'ref':
                n = nd[1]
                b += bytes([LINK, n & 0xff, n >> 8]) + self.inlines(nd[2]) + bytes([LINK_END])
            elif t == 'ext':
                text = md.plain(nd[2])
                b += self.inlines(nd[2])
                if text != nd[1] and not nd[1].startswith('mailto:'):
                    b += agon_bytes(' <' + nd[1] + '>')
            elif t == 'br':
                b += bytes([BREAK])
            elif t == 'link':
                b += self.inlines(nd[2])        # unresolved: never reaches here
            elif t == 'img':
                b += agon_bytes(f'[Image: {nd[1]}]')
            elif t == 'fnref':
                b += agon_bytes(f'[{nd[1]}]')

        return bytes(b)

    # --- blocks ----------------------------------------------------------------

    def blocks(self, blocks, indent=0, note=False, first_space=True):
        for k, b in enumerate(blocks):
            space = first_space or k > 0
            self.one(b, indent, note, space)

    def one(self, b, indent, note, space):
        if isinstance(b, md.Para):
            self.block(PARA, indent, indent, self.inlines(b.inlines), space, note)
        elif isinstance(b, md.Heading):
            self.block(HEAD, min(max(b.level, 1), 4), 0, self.inlines(b.inlines), space, note)
        elif isinstance(b, md.Code):
            text = b.text.rstrip('\n')
            self.block(PRE, indent, PRE_CODE, agon_bytes(text) + b'\n', space, note)
        elif isinstance(b, md.Table):
            self.block(PRE, indent, PRE_TABLE, table(b, self), space, note)
        elif isinstance(b, md.ListBlock):
            self.list(b, indent, note, space)
        elif isinstance(b, md.Note):
            title = b.title if b.title is not None else b.kind.capitalize()
            if title:
                self.block(PARA, indent + 2, indent + 2,
                           bytes([EM_ON]) + agon_bytes(title) + bytes([EM_OFF]), space, True)
                self.blocks(b.blocks, indent + 2, True, first_space=False)
            else:
                self.blocks(b.blocks, indent + 2, True, first_space=space)
        elif isinstance(b, md.Quote):
            self.blocks(b.blocks, indent + 2, True, first_space=space)
        elif isinstance(b, md.Rule):
            self.block(RULE, indent, indent, b'', space, note)

    def list(self, lst, indent, note, space):
        n = lst.start
        # Loose when an item has more than its text and nested lists: a
        # second paragraph, code. Tight lists stay tight when they nest.
        loose = any(len([b for b in it if not isinstance(b, md.ListBlock)]) > 1
                    for it in lst.items)
        for k, item in enumerate(lst.items):
            marker = f'{n}. ' if lst.ordered else '- '
            n += 1
            hang = indent + len(marker)
            item_space = space if k == 0 else loose
            if item and isinstance(item[0], md.Para):
                self.block(PARA, indent, hang, agon_bytes(marker) + self.inlines(item[0].inlines),
                           item_space, note)
                rest = item[1:]
            else:
                self.block(PARA, indent, hang, agon_bytes(marker.rstrip()), item_space, note)
                rest = item
            for j, b in enumerate(rest):
                # A list nested straight under its item's text is tight.
                tight = isinstance(b, md.ListBlock) and j == 0 and not loose
                self.one(b, hang, note, not tight)


# --- tables ------------------------------------------------------------------------
#
# A cell is laid out as styled characters -- (byte, em, code, link) -- so that
# wrapping it within its column can close and reopen styles and links at
# every line end, and a link split over lines is still a link on each.

def _styled(nodes, em=False, code=False, link=None):
    out = []
    for nd in nodes:
        t = nd[0]
        if t == 'text':
            out += [(c, em, code, link) for c in agon_bytes(nd[1])]
        elif t == 'code':
            out += [(c, em, True, link) for c in agon_bytes(nd[1])]
        elif t in ('em', 'strong'):
            out += _styled(nd[1], True, code, link)
        elif t == 'ref':
            out += _styled(nd[2], em, code, nd[1])
        elif t == 'ext':
            out += _styled(nd[2], em, code, link)
        elif t == 'br':
            out.append((ord('\n'), em, code, link))
        elif t == 'img':
            out += [(c, em, code, link) for c in agon_bytes(f'[Image: {nd[1]}]')]

    return out


def _wrap(chars, width):
    """Lines of styled characters, broken at spaces to fit width."""
    lines = []
    for para in _split(chars, ord('\n')):
        words = _split(para, ord(' '))
        cur = []
        for w in words:
            if not w:
                continue
            while len(w) > width:
                if cur:
                    lines.append(cur)
                    cur = []
                lines.append(w[:width])
                w = w[width:]
            if not w:
                continue
            if cur and len(cur) + 1 + len(w) > width:
                lines.append(cur)
                cur = []
            if cur:
                # The space between two words of one link is part of it, in
                # its style; any other space is plain.
                prev = cur[-1]
                if prev[3] == w[0][3]:
                    cur.append((ord(' '),) + prev[1:])
                else:
                    cur.append((ord(' '), False, False, None))
            cur += w
        lines.append(cur)

    return lines or [[]]


def _split(chars, sep):
    parts = [[]]
    for c in chars:
        if c[0] == sep:
            parts.append([])
        else:
            parts[-1].append(c)

    return parts


def _emit(chars):
    """Styled characters as markup, every style closed at the end."""
    b = bytearray()
    em = code = False
    link = None
    for ch, e, c, ln in chars:
        if ln != link:
            if code:
                b.append(CODE_OFF)
                code = False
            if em:
                b.append(EM_OFF)
                em = False
            if link is not None:
                b.append(LINK_END)
            if ln is not None:
                b += bytes([LINK, ln & 0xff, ln >> 8])
            link = ln
        if e != em:
            b.append(EM_ON if e else EM_OFF)
            em = e
        if c != code:
            b.append(CODE_ON if c else CODE_OFF)
            code = c
        b.append(ch)
    if code:
        b.append(CODE_OFF)
    if em:
        b.append(EM_OFF)
    if link is not None:
        b.append(LINK_END)

    return bytes(b)


def column_widths(natural, minimum, width):
    """Widths for columns of these natural and minimum widths, to fit width
    with ' | ' between them: the widest column gives way first, down to its
    minimum (its longest word, or 10)."""
    w = list(natural)
    room = width - 3 * (len(w) - 1)
    while sum(w) > room:
        k = max(range(len(w)), key=lambda i: (w[i] - minimum[i] > 0, w[i]))
        if w[k] <= minimum[k]:
            break                               # as narrow as it goes: scrolls
        w[k] -= 1

    return w


def table(t, renderer, width=TABLE_WIDTH):
    rows = [t.header] + t.rows
    cells = [[_styled(c) for c in row] for row in rows]
    ncol = len(t.header)
    natural = [max(max((len(l) for l in _split(cells[r][c], ord('\n'))), default=0)
                   for r in range(len(rows))) for c in range(ncol)]
    natural = [max(n, 1) for n in natural]
    minimum = []
    for c in range(ncol):
        longest = 1
        for r in range(len(rows)):
            for w in _split(cells[r][c], ord(' ')):
                longest = max(longest, len(w))
        minimum.append(min(natural[c], max(min(longest, 20), 10)))
    widths = column_widths(natural, minimum, width)

    laid = [[_wrap(cells[r][c], widths[c]) for c in range(ncol)] for r in range(len(rows))]
    wrapped = any(len(cell) > 1 for row in laid[1:] for cell in row)
    rule = '-+-'.join('-' * w for w in widths).encode()

    out = bytearray()

    def row_lines(row, bold):
        height = max(len(cell) for cell in row)
        for k in range(height):
            parts = []
            for c, cell in enumerate(row):
                seg = cell[k] if k < len(cell) else []
                if bold:
                    seg = [(ch, True, cd, ln) for ch, _, cd, ln in seg]
                pad = b' ' * (widths[c] - len(seg))
                parts.append(_emit(seg) + (pad if c < ncol - 1 else b''))
            out.extend(b' | '.join(parts).rstrip(b' ') + b'\n')

    row_lines(laid[0], True)
    out.extend(rule + b'\n')
    for k, row in enumerate(laid[1:]):
        if wrapped and k > 0:
            out.extend(rule + b'\n')
        row_lines(row, False)

    return bytes(out)


# --- topics ------------------------------------------------------------------------

def topic(book, t, menu=False):
    """A topic's own markup: its heading, its text and footnotes, and, when it
    is shown without its descendants, a list of links to its children."""
    r = Renderer()
    head = t.heading.inlines if t.heading else [('text', t.title)]
    level = t.level if t.level >= 1 else 1
    # SPACE, so a joined descendant starts after a blank line; the layout
    # drops a blank line at the very top.
    r.block(HEAD, min(level, 4), 0, r.inlines(head), True)
    r.blocks(t.blocks)
    if t.footnotes:
        r.block(RULE, 0, 0, b'', True)
        for n, blocks in t.footnotes:
            if blocks and isinstance(blocks[0], md.Para):
                r.block(PARA, 0, 4, agon_bytes(f'[{n}] ') + r.inlines(blocks[0].inlines), True)
                r.blocks(blocks[1:], 4)
    if menu and t.children:
        r.block(PARA, 0, 0, bytes([EM_ON]) + b'In this section:' + bytes([EM_OFF]), True)
        for k, c in enumerate(t.children):
            ct = book.topics[c]
            r.block(PARA, 2, 4, b'- ' + bytes([LINK, c & 0xff, c >> 8]) + agon_bytes(ct.title)
                    + bytes([LINK_END]), k == 0)

    return bytes(r.out)


def plain_text(markup):
    """The text of markup, controls dropped: what the viewer shows, unwrapped.
    For tests, and for the full-text index."""
    out = bytearray()
    k = 0
    n = len(markup)
    while k < n:
        kind = markup[k] & 0x0f
        k += 3
        while k < n and markup[k] != END:
            c = markup[k]
            if c == LINK:
                k += 3
                continue
            if c == BREAK:
                out.append(ord(' '))
            elif c >= 0x20 or c == ord('\n'):
                out.append(c)
            k += 1
        k += 1
        out.append(ord('\n'))
        if kind == RULE:
            out += b'----\n'

    return bytes(out)
