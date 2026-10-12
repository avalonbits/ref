"""md2ref, built for the PC: Markdown pages in, a document ref reads out.

Needs out/host/md2ref and out/host/reftool, which `make host` builds (and
`make test` first). The documents it writes are read back through the
reader core, as ref on the Agon reads them.
"""

import glob
import os
import shutil
import subprocess
import tempfile
import unittest

from .agon_docs import ROOT, docs

MD2REF = os.path.join(ROOT, 'out', 'host', 'md2ref')
TOOL = os.path.join(ROOT, 'out', 'host', 'reftool')
HERE = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'md2ref')
DOC = os.path.join(HERE, 'doc')
FAILED = 100
TITLE = 'Welcome to the community Agon Platform documentation'    # index.md's heading


def md2ref(*args):
    return subprocess.run([MD2REF, *args], capture_output=True, text=True, cwd=HERE)


def compile_doc(*args):
    """The document md2ref writes for args, as bytes."""
    with tempfile.TemporaryDirectory() as d:
        out = os.path.join(d, 'doc.dat')
        p = md2ref('-o', out, *args)
        if p.returncode != 0:
            raise AssertionError(p.stdout + p.stderr)
        with open(out, 'rb') as f:
            return f.read()


def ask(data, mode, lines=(), *extra):
    with tempfile.NamedTemporaryFile(suffix='.dat') as f:
        f.write(data)
        f.flush()
        p = subprocess.run([TOOL, f.name, mode, *extra], input='\n'.join(lines) + '\n',
                           capture_output=True, text=True, encoding='latin-1', check=True)

    return p.stdout


@unittest.skipUnless(os.path.exists(MD2REF) and os.path.exists(TOOL), 'no out/host: make host')
class Md2ref(unittest.TestCase):
    def test_contents_follow_the_headings(self):
        got = ask(compile_doc('doc'), 'contents').splitlines()
        self.assertEqual(got, ['My Program  [0]',
                               '  Getting started  [1]',
                               'API  [2]',
                               '  frob(int x)  [3]',
                               '  strtol, strtoul, strtoll  [4]',
                               '    VDU 23, 0, &A0, n: Buffered  [5]'])

    def test_title_from_the_first_heading_or_t(self):
        self.assertIn('title My Program\n', ask(compile_doc('doc'), 'info'))
        self.assertIn('title Other\n', ask(compile_doc('-t', 'Other', 'doc'), 'info'))

    def test_header_marks_md2ref(self):
        info = ask(compile_doc('doc'), 'info')
        self.assertIn('commit md2ref\n', info)
        self.assertIn('date \n', info)

    def test_code_spans_in_headings_are_keys(self):
        # frob(int x) is a key only as the span is: the title's words are
        # frob, int and x.
        got = ask(compile_doc('doc'), 'lookup', ['frob(int x)', 'strtol', 'strtoul', 'strtoll',
                                                  'vdu 23,0,&a0']).splitlines()
        self.assertEqual([g.split(' |')[0] for g in got],
                         ['1 1 3', '1 1 4', '1 1 4', '1 1 4', '1 1 5'])

    def test_body_words_are_searched(self):
        # One hit each: topic, query words in its title, query words matched.
        got = ask(compile_doc('doc'), 'search', ['frobs', 'converts']).splitlines()
        self.assertEqual(got, ['1 3:0:1', '1 4:0:1'])

    def test_blocks_render(self):
        text = ask(compile_doc('doc'), 'layout', (), '76')
        for want in ('1. Install it.', '   myprog -v', '  - nested strong',
                     '| Careful', '| Notes work too.', '| A quote.'):
            with self.subTest(want=want):
                self.assertIn(want + '\n', text)

    def test_a_directory_reads_index_first(self):
        self.assertEqual(compile_doc('doc'),
                         compile_doc(os.path.join('doc', 'index.md'), os.path.join('doc', 'api.md')))

    def test_same_input_same_bytes(self):
        self.assertEqual(compile_doc('doc'), compile_doc('doc'))

    def test_bad_link_is_an_error_at_its_line(self):
        with tempfile.TemporaryDirectory() as d:
            out = os.path.join(d, 'x.dat')
            p = md2ref('-o', out, os.path.join('bad', 'link.md'))
            self.assertEqual(p.returncode, FAILED)
            self.assertEqual(p.stdout,
                             "bad/link.md:5: error: cannot resolve link 'missing.md#here'\n")
            self.assertFalse(os.path.exists(out))

    def test_w_makes_a_bad_link_a_warning(self):
        with tempfile.TemporaryDirectory() as d:
            out = os.path.join(d, 'x.dat')
            p = md2ref('-w', '-o', out, os.path.join('bad', 'link.md'))
            self.assertEqual(p.returncode, 0)
            self.assertIn("bad/link.md:5: warning: cannot resolve link 'missing.md#here'\n",
                          p.stdout)
            self.assertTrue(os.path.exists(out))

    def test_unclosed_fence_is_an_error(self):
        p = md2ref('-o', os.devnull, os.path.join('bad', 'fence.md'))
        self.assertEqual(p.returncode, FAILED)
        self.assertEqual(p.stdout, 'bad/fence.md:3: error: code block never closed\n')

    def test_name_writes_ref_doc_from_the_current_directory(self):
        with tempfile.TemporaryDirectory() as d:
            shutil.copytree(DOC, os.path.join(d, 'proj'))
            proj = os.path.join(d, 'proj')
            p = subprocess.run([MD2REF, 'myprog'], capture_output=True, text=True, cwd=proj)
            self.assertEqual(p.returncode, 0, p.stdout)
            self.assertEqual(p.stdout,
                             'md2ref: ref-doc/myprog.dat: 2 pages, 6 topics, 14 keys, 43 words\n')
            with open(os.path.join(proj, 'ref-doc', 'myprog.dat'), 'rb') as f:
                self.assertEqual(f.read(), compile_doc('doc'))

    def test_name_with_pages(self):
        with tempfile.TemporaryDirectory() as d:
            p = subprocess.run([MD2REF, '-t', 'Other', 'myprog', DOC], capture_output=True,
                               text=True, cwd=d)
            self.assertEqual(p.returncode, 0, p.stdout)
            with open(os.path.join(d, 'ref-doc', 'myprog.dat'), 'rb') as f:
                self.assertEqual(f.read(), compile_doc('-t', 'Other', 'doc'))

    def test_a_name_ref_cannot_open_is_refused(self):
        for name in ('MyProg', 'toolongname', 'docs/x', 'x.md'):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as d:
                p = subprocess.run([MD2REF, name], capture_output=True, text=True, cwd=d)
                self.assertEqual(p.returncode, FAILED)
                self.assertIn("is not a document's name", p.stdout)
                self.assertFalse(os.path.exists(os.path.join(d, 'ref-doc')))

    def test_usage(self):
        for args in ((), ('-o', 'x.dat'), ('-q', '-o', 'x.dat', 'doc')):
            with self.subTest(args=args):
                p = md2ref(*args)
                self.assertEqual(p.returncode, FAILED)
                self.assertTrue(p.stdout.startswith('usage: md2ref'))


@unittest.skipUnless(os.path.exists(MD2REF) and os.path.exists(TOOL), 'no out/host: make host')
class Md2refAgonDocs(unittest.TestCase):
    """All of agon-docs, as a stress: every page compiles, every view lays out."""

    def test_agon_docs(self):
        _, root = docs()
        d = os.path.join(root, 'docs')
        pages = [os.path.join(d, 'index.md')]
        pages += sorted(p for p in glob.glob(os.path.join(d, '*.md'))
                        + glob.glob(os.path.join(d, '*', '*.md')) if p != pages[0])
        with tempfile.TemporaryDirectory() as t:
            out = os.path.join(t, 'mos.dat')
            p = md2ref('-w', '-o', out, *pages)
            self.assertEqual(p.returncode, 0, p.stdout)
            warnings = {line.split("'")[1] for line in p.stdout.splitlines() if ': warning: ' in line}
            self.assertEqual(warnings, {'#VDP-pflags-vdpflags'})    # wrong in the source
            with open(out, 'rb') as f:
                data = f.read()
        info = dict(line.split(' ', 1) for line in ask(data, 'info').splitlines() if ' ' in line)
        self.assertEqual(info['title'], TITLE)
        self.assertGreater(int(info['topics']), 700)
        self.assertEqual(len(ask(data, 'layout', (), '76').split('\n@@ ')), int(info['topics']))
