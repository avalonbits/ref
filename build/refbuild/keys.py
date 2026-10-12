"""The keyword index: keys from headings, by the rules in build/rules.toml.

Each key is a normalised string and the topic it opens. A page's rules say
how its headings become keys:

    vdu = true          a VDU sequence in a heading's code, or in the code a
                        topic opens with, gives `vdu 23 27 3` and the like
    heading_code = true each code span in a heading is a key (`CD`, `Run$Path`)
    plot = true         `PLOT codes &50-&57` in a heading gives plot 80 ...
                        plot 87, and vdu 25 80 ... vdu 25 87
    [[page.heading]]    a regex over the heading's text, and keys made from
                        its groups: {1}, {2} ...
    [[page.code]]       a regex over the lines of a topic's code blocks; its
                        first group is a key (sysvar_time)
    [[page.body]]       a regex over a topic's own text; each code span in
                        its first group is a key (a star command's aliases)
    prose = true        no entries: the headings' words only, and no gaps

and [[key]] adds keys to a page's top or to a heading, by anchor.

Every heading also gives the words of its title. A heading on a page that
is not prose, which no rule gave a key, is a gap: the report lists them, so
a page new to the wiki shows up rather than going unindexed.
"""

import re
from dataclasses import dataclass, field

from . import markdown as md
from .book import _all_blocks
from .normalize import normalize, number, vdu_key

# The commonest English words, left out of title words and the full-text
# index alike.
STOP_WORDS = frozenset('''
a about above after again all also am an and any are as at be because been
before being below between both but by can could did do does doing down during
each few for from further had has have having he her here hers him his how i
if in into is it its itself just me more most my no nor not now of off on once
only or other our out over own same she should so some such than that the
their them then there these they this those through to too under until up very
was we were what when where which while who whom why will with would you your
'''.split())


@dataclass
class Key:
    key: str
    topic: int


@dataclass
class Index:
    keys: list                      # Key, sorted by (key, topic), no duplicates
    gaps: list                      # (page, line, title) of headings with no key
    unused_rules: list = field(default_factory=list)


def title_words(text):
    words = []
    for w in re.findall(r'[a-z0-9$_]+', text.lower()):
        # Numbers are not words, in any base: `0x16` would be `vdu 22`.
        if len(w) >= 2 and number(w) is None and w not in STOP_WORDS and w.strip('_$'):
            words.append(w)

    return words


def _code_spans(inlines):
    out = []
    for nd in inlines:
        if nd[0] == 'code':
            out.append(nd[1])
        elif nd[0] in ('em', 'strong'):
            out.extend(_code_spans(nd[1]))
        elif nd[0] in ('link', 'ref', 'ext'):
            out.extend(_code_spans(nd[2]))

    return out


def _marked(inlines):
    """Inlines as text with code in backticks, for body rules to match."""
    out = []
    for nd in inlines:
        if nd[0] == 'text':
            out.append(nd[1])
        elif nd[0] == 'code':
            out.append('`' + nd[1] + '`')
        elif nd[0] in ('em', 'strong'):
            out.append(_marked(nd[1]))
        elif nd[0] in ('link', 'ref', 'ext'):
            out.append(_marked(nd[2]))
        elif nd[0] == 'br':
            out.append(' ')

    return ''.join(out)


def plot_codes(text):
    """The codes in `&48-&4F, &58-5F` (hex, as the PLOT page writes them)."""
    codes = []
    for a, b in re.findall(r'&?([0-9A-Fa-f]{1,2})\s*-\s*&?([0-9A-Fa-f]{1,2})', text):
        codes.extend(range(int(a, 16), int(b, 16) + 1))

    return codes


def build(book, rules):
    """The keys every topic's heading gives by its page's rules, and its
    title's words; the gaps; and the rules that matched nothing."""
    cfg = rules.normalize
    by_rule = {}                    # topic -> keys its page's rules gave
    by_words = {}                   # topic -> keys from its title's words
    used = set()                    # (page, kind, index) of rules that matched

    def add(to, num, text):
        # Normalised here, so a rule can say `VDU 23, 0, &A0` and match what
        # the user types.
        k = normalize(text, cfg)
        if k:
            to.setdefault(num, set()).add(k)

    for t in book.topics:
        for w in title_words(t.title):
            add(by_words, t.num, w)
        pr = rules.pages.get(t.page)
        if pr is None:
            continue
        for text in _rule_keys(t, pr, cfg, used):
            add(by_rule, t.num, text)

    for k in rules.keys:
        num = _key_topic(book, k)
        if num is None:
            raise ValueError(f'[[key]] {k}: no such page or anchor')
        for text in k['keys']:
            add(by_rule, num, text)

    pairs = {(k, num) for src in (by_rule, by_words) for num, ks in src.items() for k in ks}

    return Index([Key(k, n) for k, n in sorted(pairs)],
                 _gaps(book, rules, by_rule), _unused(book, rules, used))


def _rule_keys(t, pr, cfg, used):
    """The keys topic t's page rules give it, as written (not normalised)."""
    out = []
    heading = t.heading.inlines if t.heading else []
    if pr.vdu:
        out += _vdu_keys(t, heading, cfg)
    if pr.heading_code:
        out += _code_spans(heading)
    if pr.plot:
        m = re.search(r'PLOT codes (.*?)\)', t.title)
        if m:
            for code in plot_codes(m.group(1)):
                out += [f'plot {code}', f'vdu 25 {code}']
    for i, r in enumerate(pr.heading):
        m = re.search(r['match'], t.title)
        if m:
            used.add((t.page, 'heading', i))
            out += [_fill(tpl, m) for tpl in r['keys']]
    for i, r in enumerate(pr.code):
        for b in _all_blocks(t.blocks):
            if isinstance(b, md.Code):
                for line in b.text.split('\n'):
                    m = re.search(r['match'], line)
                    if m:
                        used.add((t.page, 'code', i))
                        out.append(m.group(1))
    for i, r in enumerate(pr.body):
        for b in _all_blocks(t.blocks):
            if isinstance(b, md.Para):
                for m in re.finditer(r['match'], _marked(b.inlines)):
                    used.add((t.page, 'body', i))
                    out += re.findall(r'`([^`]+)`', m.group(1))

    return out


def _vdu_keys(t, heading, cfg):
    """The VDU sequences a heading names, in its code -- or, a heading that
    is a sequence itself (`VDU 23, 27: Bitmaps ...`), in its text -- or else
    in the code the topic opens with."""
    codes = _code_spans(heading)
    if not codes and t.title.upper().startswith('VDU '):
        codes = [t.title]
    keys = [k for k in (vdu_key(c, cfg) for c in codes) if k]
    if not keys and t.blocks and isinstance(t.blocks[0], md.Para):
        keys = [k for k in (vdu_key(c, cfg) for c in _code_spans(t.blocks[0].inlines)[:1]) if k]

    return keys


def _gaps(book, rules, by_rule):
    """Headings on a page with rules (not prose) that no rule gave a key."""
    gaps = []
    for t in book.topics:
        pr = rules.pages.get(t.page)
        if t.num not in by_rule and not (pr and pr.prose):
            gaps.append((t.page, t.line, t.title))

    return gaps


def _unused(book, rules, used):
    """Rules for pages the wiki no longer has, and rules that match nothing."""
    unused = []
    for page, pr in rules.pages.items():
        if page not in book.pages:
            unused.append(f'[[page]] {page}: not in the wiki')
        for kind in ('heading', 'code', 'body'):
            for i, r in enumerate(getattr(pr, kind)):
                if (page, kind, i) not in used:
                    unused.append(f'[[page.{kind}]] on {page} matches nothing: {r["match"]}')

    return unused


def _fill(tpl, m):
    return re.sub(r'\{(\d+)\}', lambda g: m.group(int(g.group(1))) or '', tpl)


def _key_topic(book, k):
    page = k['page']
    if page not in book.pages:
        return None
    if 'anchor' in k:
        return book.anchors[page].get(k['anchor'])

    return book.pages[page]


# --- lookup ------------------------------------------------------------------------
#
# What the reader core does with a query, here so the builder can be tested
# against it and the C can be tested against the builder.

def prefixes(q):
    """The keys a query may start with, longest first: its leading tokens,
    and a name up to its `$` -- `hotkey$12` is `hotkey$`. Never `vdu` alone,
    which every sequence starts with."""
    toks = q.split(' ')
    out = []
    last = toks[-1]
    d = last.find('$')
    if 0 <= d < len(last) - 1:
        out.append(' '.join(toks[:-1] + [last[:d + 1]]))
    for n in range(len(toks) - 1, 0, -1):
        out.append(' '.join(toks[:n]))

    return [p for p in out if p != 'vdu']


def lookup(keys, query, cfg):
    """('exact', [topics]) | ('prefix', key, [topics]) | ('list', [(key, topic)])
    | ('none',)

    An exact match first; then the longest key the query starts with, a whole
    token at a time (`vdu 23 27 33 5 0` finds `vdu 23 27 33`); then every key
    that starts with the query (`mos_f` lists mos_fopen, mos_fclose ...).
    """
    q = normalize(query, cfg)
    if not q:
        return ('none',)
    by_key = {}
    for k in keys:
        by_key.setdefault(k.key, []).append(k.topic)
    if q in by_key:
        return ('exact', by_key[q])
    for p in prefixes(q):
        if p in by_key:
            return ('prefix', p, by_key[p])
    hits = [(k.key, k.topic) for k in keys if k.key.startswith(q)]
    if hits:
        return ('list', hits)

    return ('none',)
