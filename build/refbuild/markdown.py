"""The Markdown agon-docs is written in, parsed into a small document model.

agon-docs is a MkDocs site, so its Markdown is Python-Markdown's with the
extensions in mkdocs.yml. This parser covers what the pages use: ATX headings
with attr_list ids, paragraphs, nested lists, fenced and indented code,
tables, admonitions (and pymdownx.details, which looks the same with ???),
block quotes, footnotes, rules, front matter and [TOC], and inline code,
emphasis, links, images, autolinks and the odd HTML tag.

It is more forgiving than Python-Markdown in two places, both where the
authors' intent is plain from the source: a list or a table may follow a
paragraph without a blank line, and a nested list may be indented by less
than four spaces.

Every block carries the line it starts on, so errors can say where.
"""

import re
from dataclasses import dataclass, field


# --- the model ----------------------------------------------------------------

# Inline nodes are tuples, small and easy to compare in tests:
#   ('text', str)
#   ('code', str)
#   ('em', [inline...])        ('strong', [inline...])
#   ('link', href, [inline...])
#   ('img', alt, src)
#   ('br',)
#   ('fnref', label)


@dataclass
class Heading:
    level: int
    inlines: list
    id: str | None      # from {#id}; the slug is worked out per page later
    line: int


@dataclass
class Para:
    inlines: list
    line: int
    anchors: list = field(default_factory=list)     # {#id}s set on it


@dataclass
class ListBlock:
    ordered: bool
    start: int
    items: list         # each item is a list of blocks
    line: int


@dataclass
class Code:
    lang: str
    text: str
    line: int


@dataclass
class Table:
    header: list        # cells, each a list of inlines
    aligns: list        # 'l', 'c', 'r' or None per column
    rows: list          # rows of cells
    line: int
    anchors: list = field(default_factory=list)     # {#id}s set on cells


@dataclass
class Note:
    kind: str           # note, danger, ... (or 'details' for ???)
    title: str | None
    blocks: list
    line: int


@dataclass
class Quote:
    blocks: list
    line: int


@dataclass
class Rule:
    line: int


@dataclass
class Page:
    blocks: list
    footnotes: dict = field(default_factory=dict)   # label -> blocks


class ParseError(Exception):
    def __init__(self, msg, line):
        super().__init__(msg)
        self.line = line


# --- block parsing -----------------------------------------------------------------

FENCE = re.compile(r'^(\s*)(`{3,}|~{3,})\s*([^`\s]*)[^`]*$')
HEADING = re.compile(r'^(#{1,6})(?:[ \t]+(.*?))?[ \t]*$')
ATTR = re.compile(r'\s*\{\s*([^}]*)\}\s*$')
ADMONITION = re.compile(r'^(!!!|\?\?\?\+?)\s+([\w-]+)(?:\s+"(.*)")?\s*$')
LIST_ITEM = re.compile(r'^( *)([-*+]|\d+[.)])( +|$)(.*)$')
RULE = re.compile(r'^ {0,3}([-*_])( *\1){2,} *$')
TABLE_SEP = re.compile(r'^\s*\|?\s*:?-+:?\s*(\|\s*:?-+:?\s*)*\|?\s*$')
FOOTNOTE_DEF = re.compile(r'^\[\^([^\]]+)\]:\s?(.*)$')
LINKREF_DEF = re.compile(r'^ {0,3}\[([^\]^][^\]]*)\]:\s*(\S+)(?:\s+["\'(].*["\')])?\s*$')
QUOTE = re.compile(r'^ {0,3}> ?(.*)$')
SETEXT = re.compile(r'^ {0,3}(=+|-+) *$')


def expand_tabs(s):
    """Tabs to the next multiple of four, as Python-Markdown's tab_length."""
    if '\t' not in s:
        return s
    out = []
    col = 0
    for ch in s:
        if ch == '\t':
            n = 4 - col % 4
            out.append(' ' * n)
            col += n
        else:
            out.append(ch)
            col += 1
    return ''.join(out)


def indent_of(s):
    return len(s) - len(s.lstrip(' '))


def is_blank(s):
    return not s.strip()


def next_text(lines, i):
    """The first line from i that is not blank, or len(lines)."""
    while i < len(lines) and is_blank(lines[i][1]):
        i += 1

    return i


def is_atx_heading(s):
    """Is s a `#` heading -- `#` alone, or `#`s and a space and text?"""
    if indent_of(s) >= 4:
        return None
    t = s.lstrip(' ')
    m = HEADING.match(t)

    return m if m and (m.group(2) is not None or t.strip() == m.group(1)) else None


def parse(text):
    """Parses a page's Markdown into a Page."""
    raw = text.replace('\r\n', '\n').replace('\r', '\n').split('\n')
    lines = [(i + 1, expand_tabs(s.rstrip('\n'))) for i, s in enumerate(raw)]

    # Front matter: a --- block at the very top.
    if lines and lines[0][1].strip() == '---':
        for k in range(1, len(lines)):
            if lines[k][1].strip() == '---':
                lines = lines[k + 1:]
                break

    p = _BlockParser()
    blocks = p.blocks(lines)

    return Page(blocks, p.footnotes)


class _BlockParser:
    def __init__(self):
        self.footnotes = {}
        self.refs = {}

    def blocks(self, lines):
        # Link reference definitions are collected first: they may be used
        # above the line that defines them.
        kept = []
        for ln, s in lines:
            m = LINKREF_DEF.match(s)
            if m and not FOOTNOTE_DEF.match(s.strip()):
                self.refs[m.group(1).lower()] = m.group(2)
            else:
                kept.append((ln, s))

        return self._blocks(kept)

    def _blocks(self, lines):
        out = []
        i = 0
        n = len(lines)
        while i < n:
            ln, s = lines[i]
            if is_blank(s):
                i += 1
                continue

            stripped = s.strip()

            if stripped == '[TOC]':
                i += 1
                continue

            m = FENCE.match(s)
            if m:
                i = self._fence(lines, i, m, out)
                continue

            m = is_atx_heading(s)
            if m:
                out.append(self._heading(m, ln))
                i += 1
                continue

            m = ADMONITION.match(s) if indent_of(s) < 4 else None
            if m:
                i = self._admonition(lines, i, m, out)
                continue

            # A setext heading: a line of text underlined with = or -.
            if i + 1 < len(lines) and indent_of(s) < 4 and SETEXT.match(lines[i + 1][1]) \
                    and not LIST_ITEM.match(s) and not QUOTE.match(s) and '|' not in s:
                level = 1 if lines[i + 1][1].strip()[0] == '=' else 2
                body, hid = s.strip(), None
                a = ATTR.search(body)
                if a and '#' in a.group(1):
                    body = body[:a.start()].rstrip()
                    hid = a.group(1).split('#')[1].split()[0]
                out.append(Heading(level, self.inlines(body), hid, ln))
                i += 2
                continue

            m = FOOTNOTE_DEF.match(s) if indent_of(s) < 4 else None
            if m:
                i = self._footnote(lines, i, m)
                continue

            if RULE.match(s):
                out.append(Rule(ln))
                i += 1
                continue

            if QUOTE.match(s):
                i = self._quote(lines, i, out)
                continue

            if self._table_start(lines, i):
                i = self._table(lines, i, out)
                continue

            # A bullet is a list at any indent: nobody writes "- " to start
            # a code block.
            if LIST_ITEM.match(s):
                i = self._list(lines, i, out)
                continue

            if indent_of(s) >= 4:
                i = self._indented_code(lines, i, out)
                continue

            i = self._para(lines, i, out)

        return out

    # A paragraph ends at a blank line, or where another block starts.
    def _starts_block(self, lines, i):
        ln, s = lines[i]
        if is_blank(s):
            return True
        if i + 1 < len(lines) and SETEXT.match(lines[i + 1][1]) and indent_of(s) < 4:
            return True
        if is_atx_heading(s):
            return True
        if indent_of(s) < 4:
            if FENCE.match(s) or ADMONITION.match(s) or QUOTE.match(s):
                return True
            m = LIST_ITEM.match(s)
            if m and m.group(4).strip():
                return True
            if self._table_start(lines, i):
                return True
        return False

    def _para(self, lines, i, out):
        ln = lines[i][0]
        text = [lines[i][1].strip()]
        i += 1
        while i < len(lines) and not self._starts_block(lines, i):
            text.append(lines[i][1].strip())
            i += 1
        body, ids = strip_attrs(_join_lines(lines[i - len(text):i]))
        out.append(Para(self.inlines(body), ln, ids))

        return i

    def _heading(self, m, ln):
        level = len(m.group(1))
        body = (m.group(2) or '').rstrip()
        body = re.sub(r'\s+#+\s*$', '', body)
        hid = None
        a = ATTR.search(body)
        if a:
            body = body[:a.start()].rstrip()
            for part in a.group(1).split():
                if part.startswith('#'):
                    hid = part[1:]

        return Heading(level, self.inlines(body), hid, ln)

    def _fence(self, lines, i, m, out):
        ln = lines[i][0]
        pad = len(m.group(1))
        mark = m.group(2)
        lang = m.group(3) or ''
        body = []
        i += 1
        while i < len(lines):
            s = lines[i][1]
            t = s.strip()
            if t.startswith(mark[0] * len(mark)) and set(t) == {mark[0]}:
                i += 1
                break
            body.append(s[pad:] if indent_of(s) >= pad else s.lstrip(' '))
            i += 1
        else:
            raise ParseError('code block never closed', ln)
        out.append(Code(lang, '\n'.join(body), ln))

        return i

    def _indented_code(self, lines, i, out):
        ln = lines[i][0]
        body = []
        while i < len(lines):
            s = lines[i][1]
            if is_blank(s):
                # Blank lines inside the block are kept, trailing ones not.
                j = next_text(lines, i)
                if j < len(lines) and indent_of(lines[j][1]) >= 4:
                    body.extend([''] * (j - i))
                    i = j
                    continue
                break
            if indent_of(s) < 4:
                break
            body.append(s[4:])
            i += 1
        out.append(Code('', '\n'.join(body), ln))

        return i

    def _indented_body(self, lines, i, width):
        """The lines from i indented by at least width (or blank), dedented."""
        body = []
        while i < len(lines):
            s = lines[i][1]
            if is_blank(s):
                j = next_text(lines, i)
                if j < len(lines) and indent_of(lines[j][1]) >= width:
                    body.extend((lines[k][0], '') for k in range(i, j))
                    i = j
                    continue
                break
            if indent_of(s) < width:
                break
            body.append((lines[i][0], s[width:]))
            i += 1

        return body, i

    def _admonition(self, lines, i, m, out):
        ln = lines[i][0]
        kind = m.group(2).lower()
        if m.group(1).startswith('???'):
            title = m.group(3) or kind.capitalize()
            kind = 'details'
        else:
            title = m.group(3)
        body, i = self._indented_body(lines, i + 1, 4)
        out.append(Note(kind, title, self._blocks(body), ln))

        return i

    def _footnote(self, lines, i, m):
        label = m.group(1)
        first = [(lines[i][0], m.group(2))]
        body, i = self._indented_body(lines, i + 1, 4)
        self.footnotes[label] = self._blocks(first + body)

        return i

    def _quote(self, lines, i, out):
        ln = lines[i][0]
        body = []
        while i < len(lines):
            m = QUOTE.match(lines[i][1])
            if m:
                body.append((lines[i][0], m.group(1)))
            elif body and not is_blank(lines[i][1]) and not self._starts_block(lines, i):
                body.append(lines[i])         # a lazy continuation line
            else:
                break
            i += 1
        out.append(Quote(self._blocks(body), ln))

        return i

    def _table_start(self, lines, i):
        if i + 1 >= len(lines):
            return False
        head, sep = lines[i][1], lines[i + 1][1]
        if '|' not in head or '|' not in sep and '-' not in sep:
            return False
        if not TABLE_SEP.match(sep) or '-' not in sep:
            return False
        # A separator row with no pipes only counts under a head with some,
        # or it is a setext heading or a rule.
        if '|' not in sep and head.count('|') < 1:
            return False

        return len(split_row(head)) == len(split_row(sep))

    def _table(self, lines, i, out):
        ln = lines[i][0]
        header = split_row(lines[i][1])
        aligns = []
        for c in split_row(lines[i + 1][1]):
            c = c.strip()
            if c.startswith(':') and c.endswith(':'):
                aligns.append('c')
            elif c.endswith(':'):
                aligns.append('r')
            elif c.startswith(':'):
                aligns.append('l')
            else:
                aligns.append(None)
        rows = []
        ids = []
        i += 2
        while i < len(lines) and not is_blank(lines[i][1]) and '|' in lines[i][1]:
            cells = split_row(lines[i][1])
            cells = (cells + [''] * len(header))[:len(header)]
            row = []
            for c in cells:
                c, more = strip_attrs(c.strip())
                ids.extend(more)
                row.append(self.inlines(c))
            rows.append(row)
            i += 1
        out.append(Table([self.inlines(c.strip()) for c in header], aligns, rows, ln, ids))

        return i

    def _list(self, lines, i, out):
        """A list: items at one indent, each with whatever is indented under
        it -- by two spaces or more, not Python-Markdown's four, since the
        wiki's authors nest by two as often as by four."""
        ln = lines[i][0]
        first = LIST_ITEM.match(lines[i][1])
        base = len(first.group(1))
        ordered = first.group(2)[0].isdigit()
        start = int(first.group(2)[:-1]) if ordered else 1

        def item_here(s):
            """An item of this list: a bullet at its indent (or one more)."""
            m = LIST_ITEM.match(s)
            if m and base <= len(m.group(1)) < base + 2 and m.group(2)[0].isdigit() == ordered:
                return m

            return None

        items = []
        while i < len(lines):
            m = item_here(lines[i][1])
            if not m:
                break
            # The item's content column: where its text starts.
            content = len(m.group(1)) + len(m.group(2)) + max(1, len(m.group(3)))
            body = [(lines[i][0], m.group(4))]
            i += 1
            while i < len(lines):
                s = lines[i][1]
                if is_blank(s):
                    # Blank lines, then more indented under the item: still it.
                    j = next_text(lines, i)
                    if j < len(lines) and indent_of(lines[j][1]) > base \
                            and not (LIST_ITEM.match(lines[j][1])
                                     and indent_of(lines[j][1]) < base + 2):
                        body.extend((lines[k][0], '') for k in range(i, j))
                        i = j
                        continue
                    break
                ind = indent_of(s)
                if LIST_ITEM.match(s) and ind < base + 2:
                    break                       # the next item, or a list after
                if ind > base:
                    # Nested content: dedented to the item's content column,
                    # or as far as it goes when it is indented less.
                    body.append((lines[i][0], s[min(ind, content):]))
                    i += 1
                    continue
                # A lazy continuation of the item's paragraph.
                if not self._starts_block(lines, i) and not is_blank(body[-1][1]):
                    body.append((lines[i][0], s.strip()))
                    i += 1
                    continue
                break
            items.append(self._blocks(body))
            # A blank line, then the next item at the same indent: same list.
            j = next_text(lines, i)
            if j < len(lines):
                mm = LIST_ITEM.match(lines[j][1])
                if mm and len(mm.group(1)) == base and mm.group(2)[0].isdigit() == ordered:
                    i = j
                    continue
            break
        out.append(ListBlock(ordered, start, items, ln))

        return i

    # --- inline parsing -----------------------------------------------------------

    def inlines(self, text):
        return Inline(self.refs).parse(text)


def _join_lines(lines):
    """A paragraph's lines joined, keeping Markdown's hard breaks (two spaces)."""
    parts = []
    for k, (_, s) in enumerate(lines):
        t = s.strip()
        if k < len(lines) - 1 and s.endswith('  '):
            t += '<br>'
        parts.append(t)

    return '\n'.join(parts)


def strip_attrs(text):
    """text without a trailing attr_list, and the ids it set."""
    a = ATTR.search(text)
    if not a or not re.search(r'(^|\s|:)#[\w-]+', a.group(1)):
        return text, []
    ids = [p[1:] for p in a.group(1).replace(':', ' ').split() if p.startswith('#')]

    return text[:a.start()].rstrip(), ids


def split_row(s):
    s = s.strip()
    if s.startswith('|'):
        s = s[1:]
    if s.endswith('|') and not s.endswith('\\|'):
        s = s[:-1]
    cells = []
    cur = []
    k = 0
    in_code = 0
    while k < len(s):
        ch = s[k]
        if ch == '\\' and k + 1 < len(s) and s[k + 1] == '|':
            cur.append('|')
            k += 2
            continue
        if ch == '`':
            # Code, whose pipes are not cell breaks, only when the run is
            # closed later in the row: a lone backtick is just a backtick.
            run = len(s[k:]) - len(s[k:].lstrip('`'))
            if in_code == 0 and _find_run(s, '`', run, k + run) >= 0:
                in_code = run
            elif in_code == run:
                in_code = 0
            cur.append('`' * run)
            k += run
            continue
        if ch == '|' and not in_code:
            cells.append(''.join(cur))
            cur = []
        else:
            cur.append(ch)
        k += 1
    cells.append(''.join(cur))

    return cells


# --- inlines ----------------------------------------------------------------------

ESCAPABLE = set('\\`*_{}[]()#+-.!|<>~"\'$&%=:;,/@^')
HTML_TAGS = {'br', 'b', 'i', 'em', 'strong', 'code', 'sup', 'sub', 'span', 'div',
             'a', 'p', 'u', 'kbd', 'small', 'mark', 'center', 'font', 'tt', 'ins', 'del'}
TAG = re.compile(r'</?([A-Za-z][A-Za-z0-9]*)\b[^>]*?/?>')
AUTOLINK = re.compile(r'<((?:https?|ftp|mailto):[^\s>]+)>')


class Inline:
    def __init__(self, refs):
        self.refs = refs

    def parse(self, text):
        nodes = self._parse(text)

        return _merge(nodes)

    def _parse(self, s):
        out = []
        buf = []
        k = 0
        n = len(s)

        def flush():
            if buf:
                out.append(('text', ''.join(buf)))
                buf.clear()

        while k < n:
            ch = s[k]
            if ch == '\\' and k + 1 < n and s[k + 1] in ESCAPABLE:
                buf.append(s[k + 1])
                k += 2
                continue
            if ch == '\\' and k + 1 < n and s[k + 1] == '\n':
                flush()
                out.append(('br',))
                k += 2
                continue
            if ch == '`':
                run = len(s[k:]) - len(s[k:].lstrip('`'))
                end = _find_run(s, '`', run, k + run)
                if end >= 0:
                    flush()
                    code = s[k + run:end].replace('\n', ' ')
                    if len(code) > 2 and code[0] == ' ' and code[-1] == ' ' and code.strip():
                        code = code[1:-1]
                    out.append(('code', code.strip() if code.strip() else code))
                    k = end + run
                    continue
                buf.append('`' * run)
                k += run
                continue
            if ch == '<':
                m = AUTOLINK.match(s, k)
                if m:
                    flush()
                    out.append(('link', m.group(1), [('text', m.group(1))]))
                    k = m.end()
                    continue
                m = TAG.match(s, k)
                if m and m.group(1).lower() in HTML_TAGS:
                    flush()
                    if m.group(1).lower() == 'br':
                        out.append(('br',))
                    k = m.end()
                    continue
            if ch == '!' and k + 1 < n and s[k + 1] == '[':
                r = self._link(s, k + 1)
                if r:
                    label, href, end = r
                    flush()
                    out.append(('img', plain(self._parse(label)), href))
                    k = end
                    continue
            if ch == '[':
                if s.startswith('[^', k):
                    e = s.find(']', k)
                    if e > 0:
                        flush()
                        out.append(('fnref', s[k + 2:e]))
                        k = e + 1
                        continue
                r = self._link(s, k)
                if r:
                    label, href, end = r
                    flush()
                    out.append(('link', href, self._parse(label)))
                    k = end
                    continue
            if ch in '*_':
                r = self._emphasis(s, k)
                if r:
                    node, end = r
                    flush()
                    out.append(node)
                    k = end
                    continue
                run = len(s[k:]) - len(s[k:].lstrip(ch))
                buf.append(ch * run)
                k += run
                continue
            if ch == '\n':
                buf.append(' ')
                k += 1
                continue
            buf.append(ch)
            k += 1
        flush()

        return out

    def _link(self, s, k):
        """[label](href "title") or [label][ref] at s[k] == '['."""
        depth = 0
        j = k
        while j < len(s):
            c = s[j]
            if c == '\\':
                j += 2
                continue
            if c == '`':
                run = len(s[j:]) - len(s[j:].lstrip('`'))
                e = _find_run(s, '`', run, j + run)
                if e >= 0:
                    j = e + run
                    continue
            if c == '[':
                depth += 1
            elif c == ']':
                depth -= 1
                if depth == 0:
                    break
            j += 1
        else:
            return None
        label = s[k + 1:j]
        rest = j + 1
        if rest < len(s) and s[rest] == '(':
            depth = 0
            e = rest
            while e < len(s):
                if s[e] == '(':
                    depth += 1
                elif s[e] == ')':
                    depth -= 1
                    if depth == 0:
                        break
                e += 1
            else:
                return None
            inner = s[rest + 1:e].strip()
            m = re.match(r'^<?([^\s>]*)>?(?:\s+["\'(].*["\')])?$', inner, re.S)
            href = m.group(1) if m else inner

            return label, href, e + 1
        if rest < len(s) and s[rest] == '[':
            e = s.find(']', rest)
            if e > 0:
                ref = s[rest + 1:e].lower() or label.lower()
                if ref in self.refs:
                    return label, self.refs[ref], e + 1
        if label.lower() in self.refs:
            return label, self.refs[label.lower()], rest

        return None

    def _emphasis(self, s, k):
        ch = s[k]
        run = len(s[k:]) - len(s[k:].lstrip(ch))
        before = s[k - 1] if k > 0 else ' '
        after = s[k + run] if k + run < len(s) else ' '
        if after.isspace():
            return None
        if ch == '_' and before.isalnum():
            return None                         # intraword: mos_getkey
        # A run of one, two or three opens em, strong or both, closed by a
        # run as long; anything else is text.
        if run > 3:
            return None
        e = self._close(s, ch, run, k + run)
        if e < 0 or not s[k + run:e].strip():
            return None
        kids = self._parse(s[k + run:e])
        if run == 3:
            node = ('strong', [('em', kids)])
        elif run == 2:
            node = ('strong', kids)
        else:
            node = ('em', kids)

        return node, e + run

    def _close(self, s, ch, width, start):
        j = start
        while j < len(s):
            c = s[j]
            if c == '\\':
                j += 2
                continue
            if c == '`':
                run = len(s[j:]) - len(s[j:].lstrip('`'))
                e = _find_run(s, '`', run, j + run)
                if e >= 0:
                    j = e + run
                    continue
                j += run
                continue
            if c == ch:
                run = len(s[j:]) - len(s[j:].lstrip(ch))
                prev = s[j - 1]
                nxt = s[j + run] if j + run < len(s) else ' '
                if run >= width and not prev.isspace() \
                        and not (ch == '_' and nxt.isalnum()):
                    if run == width:
                        return j
                    # A longer run closes an inner one first, then this.
                    return j + run - width
                j += run
                continue
            j += 1

        return -1


def _find_run(s, ch, run, start):
    """The next run of exactly `run` copies of ch from start, or -1."""
    j = start
    while True:
        j = s.find(ch * run, j)
        if j < 0:
            return -1
        e = j + run
        if (j == 0 or s[j - 1] != ch) and (e >= len(s) or s[e] != ch):
            return j
        while j < len(s) and s[j] == ch:
            j += 1


def _merge(nodes):
    out = []
    for nd in nodes:
        if nd[0] == 'text' and out and out[-1][0] == 'text':
            out[-1] = ('text', out[-1][1] + nd[1])
        else:
            out.append(nd)

    return out


def plain(nodes):
    """The text of inlines, as a reader sees it: code without its backticks."""
    out = []
    for nd in nodes:
        t = nd[0]
        if t in ('text', 'code'):
            out.append(nd[1])
        elif t in ('em', 'strong'):
            out.append(plain(nd[1]))
        elif t == 'link':
            out.append(plain(nd[2]))
        elif t == 'img':
            out.append(nd[1])
        elif t == 'br':
            out.append(' ')

    return ''.join(out)
