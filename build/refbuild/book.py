"""The wiki as a book: pages split into topics, the contents tree, and links
resolved to topic numbers.

A topic is a heading and the blocks under it up to the next heading of any
level -- its own text. Its children are the headings nested under it, so the
text of a heading "and what follows it up to the next heading of the same or
a higher level" is the topic followed by its descendants, which are numbered
straight after it. Storing each topic's own text once, rather than every
ancestor's copy of it, is what keeps ref.dat small; the viewer puts a topic
and its descendants back together when it shows one.

Each page's top is a topic too. When a page opens with its only level-1
heading, that heading is the page's topic; otherwise the page topic is
whatever comes before the first heading, titled from the nav, and every
heading on the page sits under it.
"""

import os
import posixpath
import re
import unicodedata
from dataclasses import dataclass, field

from . import markdown as md
from .source import read_nav


@dataclass
class Topic:
    num: int
    title: str
    page: str               # path under docs/, e.g. 'mos/API.md'
    level: int              # 0 for a page's top, else its heading's level
    line: int
    parent: int | None
    children: list = field(default_factory=list)
    blocks: list = field(default_factory=list)
    heading: md.Heading | None = None
    anchor: str | None = None
    footnotes: list = field(default_factory=list)   # (number, blocks)


@dataclass
class Node:
    """A node of the contents tree: a nav section, a page, or a heading."""
    title: str
    topic: int | None
    children: list = field(default_factory=list)


@dataclass
class LinkError:
    page: str
    line: int
    href: str
    why: str

    def __str__(self):
        return f'{self.page}:{self.line}: cannot resolve link {self.href!r}: {self.why}'


class BookError(Exception):
    def __init__(self, errors):
        super().__init__('\n'.join(str(e) for e in errors))
        self.errors = errors


@dataclass
class Book:
    topics: list
    contents: list          # top-level Nodes
    pages: dict             # page path -> its top topic number
    anchors: dict           # page path -> {anchor: topic number}
    images: list            # (page, line, src) skipped with a note
    external: int = 0       # links left as text with their URL
    internal: int = 0       # links resolved to topics


OTHER_PAGES = 'Pages outside the menu'

EXTERNAL = re.compile(r'^(?:[a-z][a-z0-9+.-]*:|//)', re.I)


def slugify(text):
    """Python-Markdown's toc slug, which the wiki's #anchors are written for."""
    s = unicodedata.normalize('NFKD', text).encode('ascii', 'ignore').decode('ascii')
    s = re.sub(r'[^\w\s-]', '', s).strip().lower()

    return re.sub(r'[-\s]+', '-', s)


def build(docs_dir, link_fixes=None):
    """Reads mkdocs.yml and the pages it names; the Book, or BookError.

    link_fixes maps (page, href) to the href to use instead, for links the
    wiki gets wrong at the pinned commit. Each is reported, so a fix that is
    no longer needed shows up.
    """
    with open(os.path.join(docs_dir, 'mkdocs.yml'), encoding='utf-8') as f:
        nav = read_nav(f.read())
    root = os.path.join(docs_dir, 'docs')

    topics = []
    pages = {}
    anchors = {}
    sources = {}
    images = []

    def add_page(title, path):
        with open(os.path.join(root, path), encoding='utf-8') as f:
            text = f.read()
        sources[path] = text.replace('\r\n', '\n').split('\n')
        page = md.parse(text)
        pages[path] = _split(page, path, title, topics, anchors)

        return pages[path]

    def walk(items):
        out = []
        for it in items:
            if it.file:
                top = add_page(it.title, it.file)
                node = Node(it.title, top, _subtree(topics, top))
            else:
                node = Node(it.title, None, walk(it.children))
            out.append(node)

        return out

    contents = walk(nav)

    # MkDocs builds every page under docs/, in the nav or not, and the wiki
    # links to some that are not: they go in a section of their own, last.
    named = set(pages)
    others = []
    for dirpath, _, files in os.walk(root):
        for f in files:
            path = os.path.relpath(os.path.join(dirpath, f), root).replace(os.sep, '/')
            if f.endswith('.md') and path not in named:
                others.append(path)
    if others:
        section = Node(OTHER_PAGES, None, [])
        for path in sorted(others):
            top = add_page(_title_from_path(path), path)
            section.children.append(Node(topics[top].title, top, _subtree(topics, top)))
        contents.append(section)
    book = Book(topics, contents, pages, anchors, images)
    errors = _resolve(book, sources, link_fixes or {})
    if errors:
        raise BookError(errors)

    return book


def _title_from_path(path):
    return posixpath.splitext(posixpath.basename(path))[0].replace('-', ' ')


def _subtree(topics, num):
    return [Node(topics[c].title, c, _subtree(topics, c)) for c in topics[num].children]


def _split(page, path, nav_title, topics, anchors):
    blocks = page.blocks
    h1s = [b for b in blocks if isinstance(b, md.Heading) and b.level == 1]
    first = blocks[0] if blocks else None

    top_num = len(topics)
    if isinstance(first, md.Heading) and first.level == 1 and len(h1s) == 1:
        top = Topic(top_num, md.plain(first.inlines).strip(), path, 1, first.line, None,
                    heading=first)
        rest = blocks[1:]
    else:
        top = Topic(top_num, nav_title, path, 0, first.line if first else 1, None)
        rest = blocks
    topics.append(top)

    # Anchors: explicit {#id}s first, then each heading's slug, made unique
    # as Python-Markdown's toc makes them, with _1, _2 ...
    used = {b.id for b in blocks if isinstance(b, md.Heading) and b.id}
    used.update(a for b in _all_blocks(blocks) for a in getattr(b, 'anchors', []))
    page_anchors = {}

    def anchor_for(h):
        if h.id:
            return h.id
        base = slugify(md.plain(h.inlines))
        a = base
        n = 1
        while a in used:
            a = f'{base}_{n}'
            n += 1
        used.add(a)

        return a

    if top.heading:
        top.anchor = anchor_for(top.heading)
        page_anchors[top.anchor] = top_num

    stack = [top]
    for b in rest:
        if isinstance(b, md.Heading):
            while len(stack) > 1 and stack[-1].level >= b.level:
                stack.pop()
            parent = stack[-1]
            t = Topic(len(topics), md.plain(b.inlines).strip(), path, b.level, b.line,
                      parent.num, heading=b)
            t.anchor = anchor_for(b)
            page_anchors.setdefault(t.anchor, t.num)
            parent.children.append(t.num)
            topics.append(t)
            stack.append(t)
        else:
            stack[-1].blocks.append(b)

    # Anchors set on a paragraph or a table cell lead to the topic it is in.
    for num in range(top_num, len(topics)):
        for b in _all_blocks(topics[num].blocks):
            for a in getattr(b, 'anchors', []):
                page_anchors.setdefault(a, num)

    # Footnotes, numbered in the order they are first referred to, go with
    # the topic that first refers to them.
    seen = {}
    for num in range(top_num, len(topics)):
        t = topics[num]
        for label in _fnrefs(t.blocks):
            if label not in seen and label in page.footnotes:
                seen[label] = len(seen) + 1
                t.footnotes.append((seen[label], page.footnotes[label]))
    for num in range(top_num, len(topics)):
        topics[num].blocks = _number_fnrefs(topics[num].blocks, seen)
        topics[num].footnotes = [(n, _number_fnrefs(bl, seen))
                                 for n, bl in topics[num].footnotes]

    anchors[path] = page_anchors

    return top_num


# --- walking the model ------------------------------------------------------------

def _all_blocks(blocks):
    for b in blocks:
        yield b
        if isinstance(b, md.ListBlock):
            for item in b.items:
                yield from _all_blocks(item)
        elif isinstance(b, (md.Note, md.Quote)):
            yield from _all_blocks(b.blocks)


def blocks_inlines(blocks):
    """Every list of inlines in blocks, nested blocks included."""
    for b in blocks:
        if isinstance(b, (md.Para, md.Heading)):
            yield b, b.inlines
        elif isinstance(b, md.ListBlock):
            for item in b.items:
                yield from blocks_inlines(item)
        elif isinstance(b, md.Table):
            for c in b.header:
                yield b, c
            for row in b.rows:
                for c in row:
                    yield b, c
        elif isinstance(b, (md.Note, md.Quote)):
            yield from blocks_inlines(b.blocks)


def map_blocks(blocks, fn):
    """blocks with every list of inlines replaced by fn(block, inlines)."""
    out = []
    for b in blocks:
        if isinstance(b, md.Para):
            out.append(md.Para(fn(b, b.inlines), b.line, b.anchors))
        elif isinstance(b, md.Heading):
            out.append(md.Heading(b.level, fn(b, b.inlines), b.id, b.line))
        elif isinstance(b, md.ListBlock):
            out.append(md.ListBlock(b.ordered, b.start,
                                    [map_blocks(it, fn) for it in b.items], b.line))
        elif isinstance(b, md.Table):
            out.append(md.Table([fn(b, c) for c in b.header], b.aligns,
                                [[fn(b, c) for c in row] for row in b.rows], b.line,
                                b.anchors))
        elif isinstance(b, md.Note):
            out.append(md.Note(b.kind, b.title, map_blocks(b.blocks, fn), b.line))
        elif isinstance(b, md.Quote):
            out.append(md.Quote(map_blocks(b.blocks, fn), b.line))
        else:
            out.append(b)

    return out


def map_inlines(nodes, fn):
    """nodes with fn applied to each, children first; fn returns a list."""
    out = []
    for nd in nodes:
        if nd[0] in ('em', 'strong'):
            nd = (nd[0], map_inlines(nd[1], fn))
        elif nd[0] in ('link', 'ref', 'ext'):
            nd = (nd[0], nd[1], map_inlines(nd[2], fn))
        out.extend(fn(nd))

    return out


def _walk_inlines(nodes):
    for nd in nodes:
        yield nd
        if nd[0] in ('em', 'strong'):
            yield from _walk_inlines(nd[1])
        elif nd[0] in ('link', 'ref', 'ext'):
            yield from _walk_inlines(nd[2])


def _fnrefs(blocks):
    for _, inl in blocks_inlines(blocks):
        for nd in _walk_inlines(inl):
            if nd[0] == 'fnref':
                yield nd[1]


def _number_fnrefs(blocks, numbers):
    def fix(nd):
        if nd[0] == 'fnref':
            return [('text', f'[{numbers.get(nd[1], "?")}]')]

        return [nd]

    return map_blocks(blocks, lambda b, inl: map_inlines(inl, fix))


# --- links -------------------------------------------------------------------------

def _resolve(book, sources, fixes):
    errors = []
    used_fixes = set()

    def where(page, line, href):
        # The block's line, or the first line from it that has the link.
        src = sources[page]
        for k in range(max(line - 1, 0), len(src)):
            if '(' + href in src[k] or '<' + href in src[k]:
                return k + 1

        return line

    def resolve(t, block):
        def fix(nd):
            if nd[0] == 'img':
                book.images.append((t.page, where(t.page, block.line, nd[2]), nd[2]))
                name = nd[1] or posixpath.basename(nd[2])

                return [('text', f'[Image: {name} -- see the online docs]')]
            if nd[0] != 'link':
                return [nd]
            href = nd[1]
            key = (t.page, href)
            if key in fixes:
                used_fixes.add(key)
                href = fixes[key]
            if EXTERNAL.match(href):
                book.external += 1

                return [('ext', href, nd[2])]
            num, why = _target(book, t.page, href)
            if num is None:
                errors.append(LinkError(t.page, where(t.page, block.line, nd[1]), nd[1], why))

                return [('text', md.plain(nd[2]))]
            book.internal += 1

            return [('ref', num, nd[2])]

        return fix

    for t in book.topics:
        t.blocks = map_blocks(t.blocks, lambda b, inl: map_inlines(inl, resolve(t, b)))
        if t.heading:
            t.heading = map_blocks([t.heading],
                                   lambda b, inl: map_inlines(inl, resolve(t, b)))[0]
        t.footnotes = [(n, map_blocks(bl, lambda b, inl: map_inlines(inl, resolve(t, b))))
                       for n, bl in t.footnotes]

    for key in sorted(set(fixes) - used_fixes):
        errors.append(LinkError(key[0], 0, key[1], 'a fix for a link the page no longer has'))

    return errors


def _target(book, page, href):
    path, _, frag = href.partition('#')
    if path:
        target = posixpath.normpath(posixpath.join(posixpath.dirname(page), path))
        if target.endswith('/') or not target.endswith('.md'):
            if target + '.md' in book.pages:
                target += '.md'
            elif posixpath.join(target, 'index.md') in book.pages:
                target = posixpath.join(target, 'index.md')
    else:
        target = page
    if target not in book.pages:
        return None, f'no page {target} in the nav'
    if not frag:
        return book.pages[target], None
    num = book.anchors[target].get(frag)
    if num is None:
        return None, f'no heading #{frag} on {target}'

    return num, None
