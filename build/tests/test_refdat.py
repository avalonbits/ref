import random
import unittest

from refbuild import fulltext, keys, lz4, refdat, render

from .agon_docs import docs, load_rules, the_book

_built = None


def built():
    global _built
    if _built is None:
        from refbuild import source
        import os
        from .agon_docs import ROOT
        src = source.read_source(os.path.join(ROOT, 'SOURCE'))
        b = the_book()
        _built = (b, refdat.build(b, keys.build(b, load_rules()), load_rules(), src))

    return _built


class LZ4(unittest.TestCase):
    def round_trip(self, data, d=b''):
        packed = lz4.compress(data, d)
        self.assertEqual(lz4.decompress(packed, d), data)

        return packed

    def test_round_trips(self):
        rnd = random.Random(1)
        for data in (b'', b'a', b'abcd' * 100, bytes(rnd.randrange(256) for _ in range(3000)),
                     b'x' * 70000, b'The quick brown fox. ' * 50):
            self.round_trip(data)

    def test_long_runs_use_length_bytes(self):
        packed = self.round_trip(b'y' * 1000)
        self.assertLess(len(packed), 20)

    def test_dictionary(self):
        d = b'VDU 23, 0, &A0, bufferId; '
        data = b'see VDU 23, 0, &A0, bufferId; 6'
        with_d = self.round_trip(data, d)
        self.assertLess(len(with_d), len(lz4.compress(data)))

    def test_known_block(self):
        # LZ4's own encoding of 'abcabcabcabc': 3 literals, a match of 9 at 3.
        self.assertEqual(lz4.compress(b'abcabcabcabc'), bytes([0x35]) + b'abc' + bytes([3, 0, 0]))
        self.assertEqual(lz4.decompress(bytes([0x35]) + b'abc' + bytes([3, 0, 0])), b'abcabcabcabc')


class FullText(unittest.TestCase):
    def test_words(self):
        self.assertEqual(fulltext.words(b'The VDU sprite is 64 pixels, Run$Path mos_fopen ab'),
                         [b'vdu', b'sprite', b'pixels', b'run$path', b'mos_fopen'])

    def test_ranking(self):
        index = fulltext.build([b'sprite frames', b'a sprite here', b'bitmaps and sprites', b'none'],
                               [b'Frames', b'Sprites', b'Other', b'Sprite frames'])
        # 'sprite' matches sprite and sprites. Titles first, then how many
        # of the query's words, then book order.
        self.assertEqual([t for t, _, _ in fulltext.search(index, 'sprite')], [1, 3, 0, 2])
        self.assertEqual([t for t, _, _ in fulltext.search(index, 'sprite frame')], [3, 0, 1, 2])

    def test_short_and_stop_words_ignored(self):
        index = fulltext.build([b'the sprite'], [b'x'])
        self.assertEqual(fulltext.search(index, 'the s'), [])
        self.assertEqual(fulltext.search(index, 'sp'), [(0, 0, 1)])


class File(unittest.TestCase):
    def test_header(self):
        b, out = built()
        r = refdat.RefDat(out.data)
        self.assertEqual((r.version, r.commit, r.date), (1, 'f9806bd3cbff', '2026-07-15'))
        self.assertEqual(r.ntopics, len(b.topics))
        self.assertEqual(r.size, len(out.data))
        self.assertEqual(r.view, out.view_size)

    def test_every_topic_decompresses_to_its_markup(self):
        b, out = built()
        r = refdat.RefDat(out.data)
        for num in range(r.ntopics):
            self.assertEqual(r.text(num), out.texts[num])
            self.assertEqual(r.topic(num)['title'], render.agon_bytes(b.topics[num].title)[:255])

    def test_keys_round_trip(self):
        b, out = built()
        r = refdat.RefDat(out.data)
        ix = keys.build(b, load_rules())
        want = sorted({(render.agon_bytes(k.key), k.topic) for k in ix.keys})
        self.assertEqual(r.keys(), want)

    def test_words_round_trip(self):
        _, out = built()
        r = refdat.RefDat(out.data)
        self.assertEqual(r.words(), out.index)

    def test_contents(self):
        b, out = built()
        r = refdat.RefDat(out.data)
        top = [r.node(0)['title']]
        k = r.node(0)['next']
        while k is not None:
            top.append(r.node(k)['title'])
            k = r.node(k)['next']
        self.assertEqual(top, [render.agon_bytes(n.title) for n in b.contents])
        self.assertIsNone(r.node(0)['topic'])
        self.assertEqual(r.node(r.node(0)['child'])['topic'], 0)

    def test_spans(self):
        b, out = built()
        for num, span in enumerate(out.spans):
            t = b.topics[num]
            if span:
                last = num + span
                self.assertTrue(last + 1 == len(b.topics) or b.topics[last + 1].level <= t.level
                                or b.topics[last + 1].page != t.page)
                size = sum(len(out.texts[k]) for k in range(num, last + 1))
                self.assertLessEqual(size, refdat.JOIN_LIMIT)

    def test_unjoined_topic_lists_its_children(self):
        b, out = built()
        api = b.pages['mos/API.md']
        self.assertEqual(out.spans[api], 0)
        text = render.plain_text(out.texts[api])
        self.assertIn(b'In this section:', text)
        self.assertIn(b'Usage from Z80 assembler', text)

    def test_size(self):
        _, out = built()
        self.assertLess(len(out.data), 520 * 1024)
