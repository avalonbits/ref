"""ref.dat: the book in one file, read with seeks, never loaded whole.

Every integer is little-endian. Offsets are from the start of the file
unless said otherwise.

header, 128 bytes
     0  8  magic "AGONREF\\x1a"
     8  2  format version (1)
    10  2  flags (0)
    12 12  agon-docs commit, its first 12 hex digits
    24 12  agon-docs commit date, YYYY-MM-DD, NUL-padded
    36  2  topics            38  2  contents nodes
    40  2  key blocks        42  2  word blocks
    44  4  keys              48  4  words
    52  4  nodes offset      56  4  topics offset
    60  4  titles offset     64  4  text offset
    68  4  keys offset       72  4  key directory offset
    76  4  words offset      80  4  word directory offset
    84  4  normalising settings offset
    88  4  dictionary offset 92  2  dictionary length
    94  2  view size: the most text a topic shows, with what is joined to it
    96  2  the largest compressed topic
    98  2  (0)
   100  4  file size
   104 24  (0)

nodes, 10 bytes each: the contents tree, node 0 the first at the top
     title (u32, into titles), topic (u16, NONE for a nav section),
     first child (u16), next sibling (u16); NONE = 0xFFFF

topics, 16 bytes each, in book order
     title (u32, into titles), text (u32, from the text offset),
     compressed length (u16), length (u16), parent (u16 or NONE),
     span (u16): how many topics after it are shown with it -- all its
     descendants, when they fit in the view; else none, and its text ends
     with links to its children

titles: a length byte, then the title (at most 255 bytes)

text: each topic's markup (refbuild/render.py), compressed on its own as
     an LZ4 block whose matches may reach into the dictionary

keys: blocks of KEY_BLOCK keys sorted by bytes, then by topic. Each key:
     shared (u8: bytes in common with the key before; 0 first in a block),
     rest length (u8), the rest, topic (u16)
key directory: per block, its offset (u32, from the keys offset), then its
     first key: a length byte and the key

words: blocks of WORD_BLOCK words, sorted, front-coded as keys are, each
     followed by its topics: a count (varint), then for each topic
     varint(gap << 1 | in title), where gap is the topic number less the
     one before (the first: the topic number)
word directory: as the key directory

normalising settings (refbuild/normalize.py):
     count (u8), then each hex_after word: a length byte and the word;
     count (u8), then each skip: its prefix (a length byte and the
     normalised prefix), a count of units (u8), and the units (u8 each);
     count (u8), then each stop word the full-text index leaves out (a
     length byte and the word), sorted

varint: seven bits a byte, low first, the top bit set on all but the last
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
    for s, c in cnt.most_common():
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
    plain = [render.topic(book, t) for t in book.topics]
    spans = spans_for(book, [len(m) for m in plain])
    # A topic shown without its descendants lists its children instead.
    texts = [render.topic(book, t, menu=(spans[t.num] == 0 and bool(t.children)))
             for t in book.topics]
    n = len(texts)
    view = max(sum(len(texts[k]) for k in range(num, num + spans[num] + 1))
               for num in range(n))

    dictionary = train_dictionary(texts)
    packed = [lz4.compress(t, dictionary) for t in texts]

    titles = bytearray()
    title_off = {}

    def title(s):
        b = render.agon_bytes(s)[:255]
        if b not in title_off:
            title_off[b] = len(titles)
            titles.extend(bytes([len(b)]) + b)

        return title_off[b]

    topic_titles = [title(t.title) for t in book.topics]

    # Contents: nodes in preorder, so a node's first child is the next node.
    nodes = []

    def add_nodes(items):
        first = None
        prev = None
        for it in items:
            k = len(nodes)
            nodes.append([title(it.title), NONE if it.topic is None else it.topic, NONE, NONE])
            if first is None:
                first = k
            if prev is not None:
                nodes[prev][3] = k
            child = add_nodes(it.children)
            nodes[k][2] = NONE if child is None else child
            prev = k

        return first

    add_nodes(book.contents)

    text = bytearray()
    topics = bytearray()
    for num, t in enumerate(book.topics):
        if len(texts[num]) > 0xFFFF or len(packed[num]) > 0xFFFF:
            raise ValueError(f'topic {num} {t.title!r} is too big')
        topics += struct.pack('<IIHHHH', topic_titles[num], len(text), len(packed[num]),
                              len(texts[num]), NONE if t.parent is None else t.parent, spans[num])
        text += packed[num]

    # Keys, sorted by their bytes, in front-coded blocks.
    entries = sorted({(render.agon_bytes(k.key), k.topic) for k in keys_index.keys})
    keys_data, keydir, nkb = _blocks([(k, struct.pack('<H', t)) for k, t in entries], KEY_BLOCK)

    titles_plain = [render.agon_bytes(t.title) for t in book.topics]
    index = fulltext.build([render.plain_text(m) for m in texts], titles_plain)
    wentries = []
    for w in sorted(index):
        post = bytearray(varint(len(index[w])))
        prev = 0
        for t, ti in index[w]:
            post += varint((t - prev) << 1 | (1 if ti else 0))
            prev = t
        wentries.append((w, bytes(post)))
    words_data, worddir, nwb = _blocks(wentries, WORD_BLOCK)

    norm = bytearray([len(rules.normalize.hex_after)])
    for w in rules.normalize.hex_after:
        norm += bytes([len(w)]) + w.encode()
    norm.append(len(rules.normalize.skips))
    for sk in rules.normalize.skips:
        p = ' '.join(sk.prefix).encode()
        norm += bytes([len(p)]) + p + bytes([len(sk.units)]) + bytes(sk.units)
    stops = sorted(STOP_WORDS)
    norm.append(len(stops))
    for w in stops:
        norm += bytes([len(w)]) + w.encode()

    sections = [('nodes', b''.join(struct.pack('<IHHH', *nd) for nd in nodes)),
                ('topics', bytes(topics)), ('titles', bytes(titles)), ('text', bytes(text)),
                ('keys', keys_data), ('keydir', keydir), ('words', words_data),
                ('worddir', worddir), ('norm', bytes(norm)), ('dict', dictionary)]
    offs = {}
    body = bytearray()
    for name, data in sections:
        offs[name] = HEADER + len(body)
        body += data
    size = HEADER + len(body)

    commit = src.commit[:12].encode()
    date = src.date.encode()
    head = struct.pack('<8sHH12s12sHHHHII' + 'I' * 10 + 'HHHHI',
                       MAGIC, VERSION, 0, commit, date, n, len(nodes), nkb, nwb,
                       len(entries), len(wentries),
                       offs['nodes'], offs['topics'], offs['titles'], offs['text'],
                       offs['keys'], offs['keydir'], offs['words'], offs['worddir'],
                       offs['norm'], offs['dict'],
                       len(dictionary), view, max(len(p) for p in packed), 0, size)
    head += bytes(HEADER - len(head))
    data = bytes(head) + bytes(body)

    return Built(data, texts, spans, view, max(len(p) for p in packed), dictionary, index,
                 {name: len(d) for name, d in sections})


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
        (_, self.version, _, commit, date, self.ntopics, self.nnodes, self.nkb, self.nwb,
         self.nkeys, self.nwords, self.o_nodes, self.o_topics, self.o_titles, self.o_text,
         self.o_keys, self.o_keydir, self.o_words, self.o_worddir, self.o_norm, self.o_dict,
         self.dict_len, self.view, self.max_packed, _, self.size) = f
        self.commit = commit.rstrip(b'\0').decode()
        self.date = date.rstrip(b'\0').decode()
        self.dictionary = data[self.o_dict:self.o_dict + self.dict_len]

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
