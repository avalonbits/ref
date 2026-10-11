"""Markup laid out into screen lines: the reference the reader core's layout
is tested against, line for line.

    lines(markup, width) -> [Line]

- A block with SPACE starts with a blank line, unless it is the first thing.
- PARA and HEAD wrap at spaces: the first line starts at `indent`, the rest
  at `hang` (HEAD: at 0). A run of spaces is one break; spaces at a break
  vanish. A word longer than the room left on a fresh line is cut at the
  edge. BREAK starts a new line at `hang`. Controls take no room.
- PRE: each line as it is, at `indent`, however long.
- RULE: dashes from `indent` to the edge.
- NOTE blocks have `| ` drawn two columns left of their indent.

text() gives each line as the plain characters a screen would show, which
is what the tests compare.
"""

from dataclasses import dataclass

from .render import (BREAK, CODE_OFF, CODE_ON, EM_OFF, EM_ON, END, HEAD, LINK, LINK_END,
                     NOTE, PARA, PRE, RULE, SPACE)


@dataclass
class Line:
    kind: int           # PARA, HEAD, PRE, RULE, or 0 for a blank line
    x: int              # the column the text starts at
    start: int          # offset of its first byte in the markup
    end: int            # offset just past its last byte
    note: bool
    level: int = 0      # a heading's level


SP = 0x20


def _skip(markup, k):
    """The offset after the item at k: a link's start takes three bytes."""
    return k + 3 if markup[k] == LINK else k + 1


def lines(markup, width):
    out = []
    k = 0
    n = len(markup)
    while k < n:
        flags = markup[k]
        kind = flags & 0x0f
        a, b = markup[k + 1], markup[k + 2]
        note = bool(flags & NOTE)
        body = k + 3
        e = body
        while markup[e] != END:
            e = _skip(markup, e)
        if flags & SPACE and out:
            out.append(Line(0, 0, body, body, False))
        if kind == PRE:
            s = body
            j = body
            while j < e:
                if markup[j] == ord('\n'):
                    out.append(Line(PRE, a, s, j, note))
                    s = j + 1
                    j += 1
                else:
                    j = _skip(markup, j)
            if s < e:
                out.append(Line(PRE, a, s, e, note))
        elif kind == RULE:
            out.append(Line(RULE, a, body, body, note))
        else:
            indent, hang = (0, 0) if kind == HEAD else (a, b)
            level = a if kind == HEAD else 0
            _wrap(markup, body, e, indent, hang, width, kind, note, level, out)
        k = e + 1

    return out


def _wrap(markup, s, e, indent, hang, width, kind, note, level, out):
    x = indent
    j = s
    while True:
        while j < e and markup[j] == SP:
            j += 1
        start = j
        room = max(width - x, 1)
        col = 0
        brk = nxt = -1          # where to end the line at its last space, and resume
        while j < e:
            c = markup[j]
            if c == BREAK:
                break
            if c == SP:
                brk = j
                while j < e and markup[j] == SP:
                    j += 1
                nxt = j
                col += 1
                continue
            if c < 0x20:
                j = _skip(markup, j)
                continue
            if col >= room:
                break
            col += 1
            j += 1
        if j < e and markup[j] == BREAK:
            out.append(Line(kind, x, start, j, note, level))
            j += 1
        elif j >= e:
            out.append(Line(kind, x, start, j, note, level))
            break
        elif brk >= 0:
            out.append(Line(kind, x, start, brk, note, level))
            j = nxt
        else:
            out.append(Line(kind, x, start, j, note, level))   # a word too wide: cut
        x = hang


EM, CODE = 1, 2
NO_LINK = 0xFFFF


def states(markup, lns):
    """The style (EM | CODE) and link in force where each line starts: none
    at a block's start, then whatever its controls before the line set."""
    out = []
    starts = _block_starts(markup)
    for ln in lns:
        style, link = 0, NO_LINK
        k = max(b for b in starts if b <= ln.start) if ln.kind else ln.start
        while k < ln.start:
            c = markup[k]
            if c == EM_ON:
                style |= EM
            elif c == EM_OFF:
                style &= ~EM
            elif c == CODE_ON:
                style |= CODE
            elif c == CODE_OFF:
                style &= ~CODE
            elif c == LINK:
                link = markup[k + 1] | markup[k + 2] << 8
            elif c == LINK_END:
                link = NO_LINK
            k = _skip(markup, k)
        out.append((style, link))

    return out


def _block_starts(markup):
    """Where each block's content starts."""
    out = []
    k = 0
    while k < len(markup):
        out.append(k + 3)
        e = k + 3
        while markup[e] != END:
            e = _skip(markup, e)
        k = e + 1

    return out


def visible(markup, start, end):
    """The characters a line shows, controls dropped."""
    b = bytearray()
    k = start
    while k < end:
        c = markup[k]
        if c >= 0x20:
            b.append(c)
            k += 1
        else:
            k = _skip(markup, k)

    return bytes(b)


def text(markup, width):
    """Each line as a screen would show it, bars and dashes included."""
    out = []
    for ln in lines(markup, width):
        if ln.kind == 0:
            out.append(b'')
            continue
        pre = bytearray(b' ' * ln.x)
        if ln.note and ln.x >= 2:
            pre[ln.x - 2] = ord('|')
        if ln.kind == RULE:
            out.append(bytes(pre) + b'-' * max(width - ln.x, 0))
        else:
            out.append(bytes(pre) + visible(markup, ln.start, ln.end).rstrip(b' '))

    return out


def links(markup, ln):
    """The links a line holds, as (target, first column, last column + 1)."""
    out = []
    k = ln.start
    col = ln.x
    target = None
    first = col
    # A link that started on an earlier line is still open at this one's
    # start: find it by scanning the block from its beginning.
    while k < ln.end:
        c = markup[k]
        if c == LINK:
            target = markup[k + 1] | markup[k + 2] << 8
            first = col
            k += 3
            continue
        if c == LINK_END:
            if target is not None:
                out.append((target, first, col))
            target = None
            k += 1
            continue
        if c >= 0x20:
            col += 1
        k += 1
    if target is not None:
        out.append((target, first, col))

    return out
