"""A ref document: the book in one file, read with seeks, never loaded
whole. docs/FORMAT.md is the specification of every byte this writes;
RefDat at the end reads it back, for the tests.

ref's builder writes agon-docs as mos.dat. The header's revision and date
are the agon-docs commit's, and its title the wiki's own (mkdocs.yml's
site_name).
"""

import struct
from dataclasses import dataclass

from . import fulltext, lz4, render
from .keys import STOP_WORDS

MAGIC = b'AGONREF\x1a'
VERSION = 1
HEADER = 128
NONE = 0xFFFF
KEY_BLOCK = 64
WORD_BLOCK = 64
DICT_SIZE = 8192
JOIN_LIMIT = 16384          # the most text a topic shows with its descendants joined


def varint(n):
    out = bytearray()
    while True:
        b = n & 0x7F
        n >>= 7
        if n:
            out.append(b | 0x80)
        else:
            out.append(b)

            return bytes(out)


def train_dictionary(texts, size=DICT_SIZE, shingle=12, step=3):
    """Strings common to many topics, for every topic's matches to reach into.

    Counts each 12-byte run once per topic it appears in, and takes the
    commonest, most common last: matches are cheapest close to the text,
    and LZ4's offsets count back from it.
    """
    from collections import Counter
    cnt = Counter()
    for t in texts:
        cnt.update({t[i:i + shingle] for i in range(0, max(len(t) - shingle, 0), step)})
    picks = []
    have = bytearray()
    # Ties broken by the bytes themselves: a set's order follows Python's
    # hash seed, which changes every run, and ref.dat must not.
    for s, c in sorted(cnt.items(), key=lambda kv: (-kv[1], kv[0])):
        if c < 3 or len(have) >= size:
            break
        if s in have:
            continue
        picks.append(s)
        have += s
    data = b''.join(reversed(picks))

    return data[-size:]


@dataclass
class Built:
    data: bytes
    texts: list         # each topic's markup, uncompressed
    spans: list
    view_size: int
    max_packed: int
    dictionary: bytes
    index: dict         # the full-text index
    sizes: dict         # section -> bytes


def spans_for(book, own):
    """span for each topic: all its descendants if the whole fits."""
    n = len(book.topics)
    desc = [0] * n
    total = [0] * n
    for num in range(n - 1, -1, -1):
        t = book.topics[num]
        total[num] = own[num] + sum(total[c] for c in t.children)
        desc[num] = sum(desc[c] + 1 for c in t.children)

    return [desc[k] if total[k] <= JOIN_LIMIT else 0 for k in range(n)]


def build(book, keys_index, rules, src):
    """ref.dat for the book, its keys and the rules' normalising settings."""
    texts, spans, view = _render(book)
    dictionary = train_dictionary(texts)
    packed = [lz4.compress(t, dictionary) for t in texts]

    titles = _Titles()
    topic_titles = [titles.add(t.title) for t in book.topics]
    book_title = titles.add(book.title) if book.title else 0
    nodes, nnodes = _contents(book, titles)
    topics, text = _topics(book, texts, packed, spans, topic_titles)
    keys, keydir, nkb, nkeys = _keys(keys_index)
    index = fulltext.build([render.plain_text(m) for m in texts],
                           [render.agon_bytes(t.title) for t in book.topics])
    words, worddir, nwb, nwords = _words(index)

    sections = [('nodes', nodes), ('topics', topics), ('titles', bytes(titles.data)),
                ('text', text), ('keys', keys), ('keydir', keydir), ('words', words),
                ('worddir', worddir), ('norm', _norm(rules)), ('dict', dictionary)]
    offs = {}
    body = bytearray()
    for name, data in sections:
        offs[name] = HEADER + len(body)
        body += data

    max_packed = max(len(p) for p in packed)
    head = struct.pack('<8sHH12s12sHHHHII' + 'I' * 10 + 'HHHHI',
                       MAGIC, VERSION, 1 if book.title else 0,
                       src.commit[:12].encode(), src.date.encode(),
                       len(texts), nnodes, nkb, nwb, nkeys, nwords,
                       offs['nodes'], offs['topics'], offs['titles'], offs['text'],
                       offs['keys'], offs['keydir'], offs['words'], offs['worddir'],
                       offs['norm'], offs['dict'],
                       len(dictionary), view, max_packed, 0, HEADER + len(body))
    head += struct.pack('<I', book_title)
    head += bytes(HEADER - len(head))

    return Built(bytes(head) + bytes(body), texts, spans, view, max_packed, dictionary, index,
                 {name: len(d) for name, d in sections})


def _render(book):
    """Each topic's markup, how many topics after each are shown with it,
    and the view buffer that needs: the most text one topic shows."""
    plain = [render.topic(book, t) for t in book.topics]
    spans = spans_for(book, [len(m) for m in plain])
    # A topic shown without its descendants lists its children instead.
    texts = [render.topic(book, t, menu=(spans[t.num] == 0 and bool(t.children)))
             for t in book.topics]
    view = max(sum(len(texts[k]) for k in range(num, num + spans[num] + 1))
               for num in range(len(texts)))

    return texts, spans, view


class _Titles:
    """The titles section: each title once, as a length byte and its bytes."""

    def __init__(self):
        self.data = bytearray()
        self.at = {}

    def add(self, s):
        b = render.agon_bytes(s)[:255]
        if b not in self.at:
            self.at[b] = len(self.data)
            self.data += bytes([len(b)]) + b

        return self.at[b]


def _contents(book, titles):
    """The contents tree as nodes in preorder, so a node's first child is the
    node after it: the section's bytes, and how many nodes."""
    nodes = []

    def add(items):
        first = prev = None
        for it in items:
            k = len(nodes)
            nodes.append([titles.add(it.title), NONE if it.topic is None else it.topic,
                          NONE, NONE])
            if first is None:
                first = k
            if prev is not None:
                nodes[prev][3] = k                  # the next sibling
            child = add(it.children)
            nodes[k][2] = NONE if child is None else child
            prev = k

        return first

    add(book.contents)

    return b''.join(struct.pack('<IHHH', *nd) for nd in nodes), len(nodes)


def _topics(book, texts, packed, spans, topic_titles):
    """The topics section and the text section."""
    topics = bytearray()
    text = bytearray()
    for num, t in enumerate(book.topics):
        if len(texts[num]) > 0xFFFF or len(packed[num]) > 0xFFFF:
            raise ValueError(f'topic {num} {t.title!r} is too big')
        topics += struct.pack('<IIHHHH', topic_titles[num], len(text), len(packed[num]),
                              len(texts[num]), NONE if t.parent is None else t.parent,
                              spans[num])
        text += packed[num]

    return bytes(topics), bytes(text)


def _keys(keys_index):
    """The keys, sorted by their bytes, in front-coded blocks: the section,
    its directory, how many blocks and how many keys."""
    entries = sorted({(render.agon_bytes(k.key), k.topic) for k in keys_index.keys})
    data, directory, nblocks = _blocks([(k, struct.pack('<H', t)) for k, t in entries],
                                       KEY_BLOCK)

    return data, directory, nblocks, len(entries)


def _words(index):
    """The full-text index, as the keys are, each word with its topics."""
    entries = []
    for w in sorted(index):
        post = bytearray(varint(len(index[w])))
        prev = 0
        for t, in_title in index[w]:
            post += varint((t - prev) << 1 | (1 if in_title else 0))
            prev = t
        entries.append((w, bytes(post)))
    data, directory, nblocks = _blocks(entries, WORD_BLOCK)

    return data, directory, nblocks, len(entries)


def _norm(rules):
    """The normalising settings: hex_after words, skips, stop words."""
    def string(b):
        return bytes([len(b)]) + b

    norm = bytearray([len(rules.normalize.hex_after)])
    for w in rules.normalize.hex_after:
        norm += string(w.encode())
    norm.append(len(rules.normalize.skips))
    for sk in rules.normalize.skips:
        norm += string(' '.join(sk.prefix).encode()) + bytes([len(sk.units)]) + bytes(sk.units)
    stops = sorted(STOP_WORDS)
    norm.append(len(stops))
    for w in stops:
        norm += string(w.encode())

    return bytes(norm)


def _blocks(entries, per):
    """Front-coded blocks of (key, payload), and their directory."""
    data = bytearray()
    directory = bytearray()
    prev = b''
    for k, (key, payload) in enumerate(entries):
        if len(key) > 255:
            raise ValueError(f'key too long: {key!r}')
        if k % per == 0:
            directory += struct.pack('<I', len(data)) + bytes([len(key)]) + key
            prev = b''
        shared = 0
        while shared < min(len(prev), len(key), 255) and prev[shared] == key[shared]:
            shared += 1
        data += bytes([shared, len(key) - shared]) + key[shared:] + payload
        prev = key

    return bytes(data), bytes(directory), (len(entries) + per - 1) // per


# --- reading, for the tests and for checking what was written ---------------------

class RefDat:
    def __init__(self, data):
        self.d = data
        if data[:8] != MAGIC:
            raise ValueError('not a ref.dat')
        f = struct.unpack_from('<8sHH12s12sHHHHII' + 'I' * 10 + 'HHHHI', data, 0)
        (_, self.version, self.flags, commit, date, self.ntopics, self.nnodes, self.nkb, self.nwb,
         self.nkeys, self.nwords, self.o_nodes, self.o_topics, self.o_titles, self.o_text,
         self.o_keys, self.o_keydir, self.o_words, self.o_worddir, self.o_norm, self.o_dict,
         self.dict_len, self.view, self.max_packed, _, self.size) = f
        self.commit = commit.rstrip(b'\0').decode()
        self.date = date.rstrip(b'\0').decode()
        self.dictionary = data[self.o_dict:self.o_dict + self.dict_len]
        self.title = (self.title_at(struct.unpack_from('<I', data, 104)[0])
                      if self.flags & 1 else None)

    def title_at(self, off):
        k = self.o_titles + off

        return self.d[k + 1:k + 1 + self.d[k]]

    def topic(self, num):
        title, text, plen, rlen, parent, span = struct.unpack_from(
            '<IIHHHH', self.d, self.o_topics + 16 * num)

        return dict(title=self.title_at(title), text=text, packed=plen, length=rlen,
                    parent=None if parent == NONE else parent, span=span)

    def text(self, num):
        t = self.topic(num)
        k = self.o_text + t['text']
        out = lz4.decompress(self.d[k:k + t['packed']], self.dictionary)
        assert len(out) == t['length']

        return out

    def node(self, k):
        title, topic, child, nxt = struct.unpack_from('<IHHH', self.d, self.o_nodes + 10 * k)

        return dict(title=self.title_at(title), topic=None if topic == NONE else topic,
                    child=None if child == NONE else child, next=None if nxt == NONE else nxt)

    def _entries(self, off, count, payload):
        k = off
        prev = b''
        for i in range(count):
            shared, rest = self.d[k], self.d[k + 1]
            key = prev[:shared] + self.d[k + 2:k + 2 + rest]
            k += 2 + rest
            k, value = payload(k)
            yield key, value
            prev = key

    def keys(self):
        def topic(k):
            return k + 2, struct.unpack_from('<H', self.d, k)[0]

        return list(self._entries(self.o_keys, self.nkeys, topic))

    def words(self):
        def postings(k):
            n, k = self._varint(k)
            out = []
            prev = 0
            for _ in range(n):
                v, k = self._varint(k)
                prev += v >> 1
                out.append((prev, bool(v & 1)))

            return k, out

        return dict(self._entries(self.o_words, self.nwords, postings))

    def _varint(self, k):
        v = 0
        shift = 0
        while True:
            b = self.d[k]
            k += 1
            v |= (b & 0x7F) << shift
            shift += 7
            if not b & 0x80:
                return v, k
