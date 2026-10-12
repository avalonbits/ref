"""The reader core, built for the PC, against the builder's own answers.

Needs out/host/reftool, which `make host` builds (and `make test` first).
"""

import json
import os
import random
import subprocess
import tempfile
import unittest

from refbuild import fulltext, keys, layout, render

from .agon_docs import ROOT, load_rules
from .test_agon_docs import REACHES
from .test_refdat import built

TOOL = os.path.join(ROOT, 'out', 'host', 'reftool')
HERE = os.path.dirname(os.path.abspath(__file__))

_dat = None


def dat():
    global _dat
    if _dat is None:
        _, out = built()
        fd, path = tempfile.mkstemp(suffix='.dat')
        with os.fdopen(fd, 'wb') as f:
            f.write(out.data)
        _dat = path

    return _dat


def run(mode, lines=(), *extra):
    p = subprocess.run([TOOL, dat(), mode, *extra], input='\n'.join(lines).encode('latin-1') + b'\n',
                       capture_output=True, check=True)

    return p.stdout


@unittest.skipUnless(os.path.exists(TOOL), 'no out/host/reftool: make host')
class Core(unittest.TestCase):
    def test_normalize_shared_cases(self):
        with open(os.path.join(HERE, 'normalize.json')) as f:
            cases = json.load(f)['normalize']
        got = run('norm', [c[0] for c in cases]).decode('latin-1').split('\n')
        for (text, want), g in zip(cases, got):
            with self.subTest(text=text):
                self.assertEqual(g, want)

    def test_normalize_agrees_on_every_key_and_title(self):
        # Every key, and every title, normalised by both.
        b, _ = built()
        cfg = load_rules().normalize
        texts = sorted({k.key for k in keys.build(b, load_rules()).keys}
                       | {t.title.encode('latin-1', 'replace').decode('latin-1') for t in b.topics})
        got = run('norm', texts).decode('latin-1').split('\n')
        from refbuild.normalize import normalize
        bad = [(t, g, normalize(t, cfg)) for t, g in zip(texts, got) if g != normalize(t, cfg)]
        self.assertEqual(bad[:5], [])

    def test_finds_every_key_the_builder_wrote(self):
        b, _ = built()
        ix = keys.build(b, load_rules())
        by_key = {}
        for k in ix.keys:
            by_key.setdefault(k.key, []).append(k.topic)
        qs = sorted(by_key)
        got = run('lookup', qs).decode('latin-1').split('\n')
        for q, g in zip(qs, got):
            kind, count, *rest = g.split(' |')[0].split(' ')
            with self.subTest(key=q):
                self.assertEqual(kind, '1')
                self.assertEqual(int(count), len(by_key[q]))
                self.assertEqual([int(t) for t in rest], sorted(by_key[q])[:64])

    def test_lookups_agree(self):
        b, _ = built()
        r = load_rules()
        ix = keys.build(b, r)
        rnd = random.Random(7)
        distinct = sorted({k.key for k in ix.keys})
        qs = [q for q, _, _ in REACHES]
        qs += [k[:n] for k in rnd.sample(distinct, 200) for n in (2, 4, len(k) - 1) if n > 0]
        qs += ['vdu 23 27 33 9 9 9', 'hotkey$99', 'xyzzy', '', 'vdu 99', 'mos_', '0x16', 'rst ff',
               'VDU 23,0,&85,3,5,0', 'vdu 23 0 &a0 1 0 9']
        got = run('lookup', qs).decode('latin-1').split('\n')
        names = {'none': '0', 'exact': '1', 'prefix': '2', 'list': '3'}
        for q, g in zip(qs, got):
            res = keys.lookup(ix.keys, q, r.normalize)
            head, _, keytext = g.partition(' |')
            kind, count, *topics = head.split(' ')
            with self.subTest(q=q):
                self.assertEqual(kind, names[res[0]])
                if res[0] == 'list':
                    want = res[1]
                    self.assertEqual(int(count), len(want))
                    self.assertEqual([int(t) for t in topics], [t for _, t in want][:64])
                    self.assertEqual([k.strip() for k in keytext.split(';')[:-1]],
                                     [k for k, _ in want][:64])
                elif res[0] != 'none':
                    self.assertEqual([int(t) for t in topics], sorted(res[-1])[:64])

    def test_keys_from(self):
        b, _ = built()
        r = load_rules()
        ix = keys.build(b, r)
        from refbuild.normalize import normalize
        qs = ['mos_f', 'vdu 23 27', 'plot 8', 'cd', 'zzz', '', 'VDU 23,0,&A0,1;']
        got = run('from', qs).decode('latin-1').split('\n')
        for q, g in zip(qs, got):
            n = normalize(q, r.normalize)
            want = [(k.key, k.topic) for k in ix.keys if n and k.key.startswith(n)]
            count, *rest = g.split(' ', 1)
            pairs = [tuple(x.strip().rsplit('=', 1)) for x in (rest[0].split(';')[:-1] if rest else [])]
            with self.subTest(q=q):
                self.assertEqual(int(count), len(want))
                self.assertEqual([(k, int(t)) for k, t in pairs], want[:64])

    def test_titles_section(self):
        b, _ = built()
        got = run('titles').decode('latin-1').split('\n')[:-1]
        self.assertEqual(got, [render.agon_bytes(t.title)[:255].decode('latin-1') for t in b.topics])

    def test_searches_agree(self):
        b, out = built()
        rnd = random.Random(3)
        words = sorted(out.index)
        qs = ['sprite', 'sprite frame', 'buffer call', 'the s', 'mos_fopen', 'VDU', 'zz', 'a',
              'keyboard layout', 'run$path', 'palette entry copper']
        qs += [w[:rnd.randint(2, len(w))].decode() for w in rnd.sample(words, 120)]
        qs += [t.title for t in rnd.sample(b.topics, 40)]
        qs = [q.encode('latin-1', 'replace').decode('latin-1') for q in qs]
        got = run('search', qs).decode('latin-1').split('\n')
        for q, g in zip(qs, got):
            want = fulltext.search(out.index, q, words)
            n, *hits = g.split(' ')
            with self.subTest(q=q):
                self.assertEqual(int(n), len(want))
                self.assertEqual(hits, [f'{t}:{ti}:{c}' for t, ti, c in want[:100]])

    def test_every_view_decompresses_to_what_was_rendered(self):
        _, out = built()
        data = run('views')
        k = 0
        for num in range(len(out.texts)):
            n = data[k] | data[k + 1] << 8
            want = b''.join(out.texts[num:num + out.spans[num] + 1])
            with self.subTest(topic=num):
                self.assertEqual(data[k + 2:k + 2 + n], want)
            k += 2 + n
        self.assertEqual(k, len(data))

    def test_layout_agrees(self):
        _, out = built()
        for width in (79, 39):
            text = run('layout', (), str(width)).split(b'\n')
            k = 0
            for num in range(len(out.texts)):
                view = b''.join(out.texts[num:num + out.spans[num] + 1])
                want = layout.text(view, width)
                head = text[k].split(b' ')
                self.assertEqual(head[:2], [b'@@', str(num).encode()])
                n = int(head[2])
                got = text[k + 1:k + 1 + n]
                st = [tuple(int(v) for v in x.split(b':')) for x in text[k + 1 + n].split()]
                with self.subTest(width=width, topic=num):
                    self.assertEqual(got, want)
                    self.assertEqual(st, layout.states(view, layout.lines(view, width)))
                k += 2 + n

    def test_book_title(self):
        info = run('info').decode('latin-1')
        self.assertIn('title Community Agon Platform documentation\n', info)

    def test_contents(self):
        b, _ = built()
        self.assertEqual(run('contents').decode('latin-1'), _contents_text(b))


def _contents_text(b):
    out = []

    def walk(nodes, depth):
        for n in nodes:
            t = '' if n.topic is None else f'  [{n.topic}]'
            out.append('  ' * depth + render.agon_bytes(n.title).decode('latin-1') + t)
            walk(n.children, depth + 1)

    walk(b.contents, 0)

    return '\n'.join(out) + '\n'


if __name__ == '__main__':
    unittest.main()
