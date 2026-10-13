"""ref's own book, docs/ref: compiled by md2ref, as `make book` makes
out/ref.dat, and read back through the reader core.

Every option ref and md2ref take -- read from their sources, so one added
without a page fails here -- is a key the book finds, and every page is in
its menu, docs/ref/mkdocs.yml.
"""

import os
import re
import subprocess
import tempfile
import unittest

from .agon_docs import ROOT
from .test_md2ref import MD2REF, TOOL, ask

BOOK = os.path.join(ROOT, 'docs', 'ref')


def options(path, pattern):
    with open(os.path.join(ROOT, path)) as f:
        return sorted(set(re.findall(pattern, f.read())))


@unittest.skipUnless(os.path.exists(MD2REF) and os.path.exists(TOOL), 'no out/host: make host')
class RefBook(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with tempfile.TemporaryDirectory() as d:
            out = os.path.join(d, 'ref.dat')
            cls.run_ = subprocess.run([MD2REF, '-o', out, BOOK], capture_output=True, text=True)
            with open(out, 'rb') as f:
                cls.data = f.read()

    def test_it_compiles_with_nothing_to_say(self):
        # No link that leads nowhere -- md2ref would stop -- and no warning.
        self.assertEqual(self.run_.returncode, 0, self.run_.stdout)
        self.assertRegex(self.run_.stdout, r'^md2ref: .*ref\.dat: 9 pages, \d+ topics, \d+ keys, '
                                           r'\d+ words\nDone in \d+\.\d\d seconds\n$')
        self.assertIn("title ref, the Agon's documentation reader\n", ask(self.data, 'info'))

    def lookup(self, names):
        got = ask(self.data, 'lookup', names).splitlines()

        return {n: g.split(' |')[0] for n, g in zip(names, got)}

    def test_every_option_of_ref_is_a_key(self):
        names = options('src/viewer/main.c', r'strcmp\(a, "(-[^"]+)"\)')
        self.assertIn('-k', names)
        for name, kind in self.lookup(names).items():
            with self.subTest(option=name):
                self.assertEqual(kind.split()[0], '1', 'ref %s: %s' % (name, kind))

    def test_every_option_of_md2ref_is_a_key(self):
        names = options('src/md2ref/md2ref.c', r'strcmp\(argv\[i\], "(-[^"]+)"\)') + ['@list']
        self.assertIn('-z', names)
        for name, kind in self.lookup(names).items():
            with self.subTest(option=name):
                self.assertEqual(kind.split()[0], '1', 'md2ref %s: %s' % (name, kind))

    def test_every_page_is_in_the_menu(self):
        contents = ask(self.data, 'contents')
        self.assertNotIn('Pages outside the menu', contents)
        self.assertEqual([c for c in contents.splitlines() if not c.startswith(' ')],
                         ['Reading', 'Writing documents', 'The format'])


if __name__ == '__main__':
    unittest.main()
