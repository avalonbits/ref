import os
import tempfile
import textwrap
import unittest

from refbuild import book, rules, source

NAV = """\
site_name: test
theme:
  name: material
nav:
  - 'Welcome':
    - 'Start here': index.md
  - 'MOS':
      - 'API': mos/API.md
  - 'Two tops': two.md
markdown_extensions:
  - pymdownx.emoji:
      emoji_index: !!python/name:material.extensions.emoji.twemoji
"""

PAGES = {
    'index.md': """\
        # Welcome

        Read the [API](mos/API.md) and [getkey](mos/API.md#0x00-mos_getkey),
        and the [templates](Projects.md).
        """,
    'mos/API.md': """\
        # What is the MOS API

        Intro, see [below](#usage).

        ## Usage {#usage}

        Text[^n].

        ### `0x00`: mos_getkey

        Back to [the top](../index.md) or [usage](#usage), or [out](https://x.org).

        ### `0x00`: mos_getkey

        A duplicate heading, which gets _1.

        ## Sysvars

        | Name | What |
        |---|---|
        | sysvar_time | Clock {#sv-time} |

        ![font map](../images/font.png)

        [^n]: The footnote.
        """,
    'two.md': """\
        Preamble.

        # First

        ## Under first

        # Second
        """,
    'Projects.md': """\
        # The Templates
        """,
}


def make_docs(pages=PAGES, nav=NAV):
    d = tempfile.mkdtemp()
    with open(os.path.join(d, 'mkdocs.yml'), 'w') as f:
        f.write(nav)
    for path, text in pages.items():
        full = os.path.join(d, 'docs', path)
        os.makedirs(os.path.dirname(full), exist_ok=True)
        with open(full, 'w') as f:
            f.write(textwrap.dedent(text))

    return d


def titles(b):
    return [t.title for t in b.topics]


class Nav(unittest.TestCase):
    def test_tree(self):
        nav = source.read_nav(NAV)
        self.assertEqual([n.title for n in nav], ['Welcome', 'MOS', 'Two tops'])
        self.assertEqual(nav[0].children[0].file, 'index.md')
        self.assertEqual(nav[1].children[0].title, 'API')
        self.assertEqual(nav[2].file, 'two.md')


class Topics(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.b = book.build(make_docs())

    def test_split_at_headings(self):
        self.assertEqual(titles(self.b), [
            'Welcome',
            'What is the MOS API', 'Usage', '0x00: mos_getkey', '0x00: mos_getkey', 'Sysvars',
            'Two tops', 'First', 'Under first', 'Second',
            'The Templates'])

    def test_tree_of_topics(self):
        t = self.b.topics
        api = self.b.pages['mos/API.md']
        self.assertEqual(t[api].children, [api + 1, api + 4])
        self.assertEqual(t[api + 1].children, [api + 2, api + 3])
        self.assertEqual(t[api + 2].parent, api + 1)

    def test_page_with_several_h1s_has_its_own_top(self):
        top = self.b.topics[self.b.pages['two.md']]
        self.assertEqual((top.title, top.level), ('Two tops', 0))
        self.assertEqual([self.b.topics[c].title for c in top.children], ['First', 'Second'])
        self.assertEqual(len(top.blocks), 1)                # the preamble

    def test_own_text_only(self):
        usage = self.b.topics[self.b.pages['mos/API.md'] + 1]
        self.assertEqual(len(usage.blocks), 1)

    def test_anchors(self):
        a = self.b.anchors['mos/API.md']
        api = self.b.pages['mos/API.md']
        self.assertEqual(a['usage'], api + 1)
        self.assertEqual(a['0x00-mos_getkey'], api + 2)
        self.assertEqual(a['0x00-mos_getkey_1'], api + 3)
        self.assertEqual(a['sv-time'], api + 4)
        self.assertEqual(a['what-is-the-mos-api'], api)

    def test_contents(self):
        c = self.b.contents
        self.assertEqual([n.title for n in c],
                         ['Welcome', 'MOS', 'Two tops', book.OTHER_PAGES])
        api = c[1].children[0]
        self.assertEqual((api.title, api.topic), ('API', self.b.pages['mos/API.md']))
        self.assertEqual([n.title for n in api.children], ['Usage', 'Sysvars'])
        self.assertIsNone(c[0].topic)

    def test_page_outside_the_nav_is_kept(self):
        self.assertIn('Projects.md', self.b.pages)

    def test_footnote_goes_with_its_topic(self):
        usage = self.b.topics[self.b.pages['mos/API.md'] + 1]
        self.assertEqual(usage.footnotes[0][0], 1)
        self.assertIn(('text', '[1]'), usage.blocks[0].inlines)


class Links(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.b = book.build(make_docs())

    def refs(self, num):
        out = []
        for _, inl in book.blocks_inlines(self.b.topics[num].blocks):
            for nd in book._walk_inlines(inl):
                if nd[0] in ('ref', 'ext'):
                    out.append(nd[:2])

        return out

    def test_resolved_to_topics(self):
        api = self.b.pages['mos/API.md']
        self.assertEqual(self.refs(0), [('ref', api), ('ref', api + 2),
                                        ('ref', self.b.pages['Projects.md'])])
        self.assertEqual(self.refs(api + 2), [('ref', 0), ('ref', api + 1),
                                              ('ext', 'https://x.org')])

    def test_counts(self):
        self.assertEqual((self.b.internal, self.b.external), (6, 1))

    def test_image_becomes_a_note(self):
        self.assertEqual(self.b.images, [('mos/API.md', 23, '../images/font.png')])
        sysvars = self.b.topics[self.b.pages['mos/API.md'] + 4]
        self.assertIn('[Image: font map -- see the online docs]',
                      book.md.plain(sysvars.blocks[1].inlines))

    def test_unresolved_link_is_an_error_with_its_line(self):
        pages = dict(PAGES)
        pages['index.md'] = '# Welcome\n\nText.\n\nSee [nothing](mos/API.md#nowhere).\n'
        with self.assertRaises(book.BookError) as e:
            book.build(make_docs(pages))
        err, = e.exception.errors
        self.assertEqual((err.page, err.line, err.href), ('index.md', 5, 'mos/API.md#nowhere'))
        self.assertIn('no heading #nowhere on mos/API.md', str(err))

    def test_missing_page_is_an_error(self):
        pages = dict(PAGES)
        pages['index.md'] = '# Welcome\n\n[gone](Gone.md)\n'
        with self.assertRaises(book.BookError) as e:
            book.build(make_docs(pages))
        self.assertIn('no page Gone.md', str(e.exception))

    def test_fix(self):
        pages = dict(PAGES)
        pages['index.md'] = '# Welcome\n\n[x](mos/API.md#Usage)\n'
        b = book.build(make_docs(pages), {('index.md', 'mos/API.md#Usage'): 'mos/API.md#usage'})
        self.assertEqual(b.internal, 4)

    def test_unused_fix_is_reported(self):
        with self.assertRaises(book.BookError) as e:
            book.build(make_docs(), {('index.md', '#gone'): '#x'})
        self.assertIn('no longer has', str(e.exception))


class Rules(unittest.TestCase):
    def test_link_fix_needs_a_reason(self):
        with self.assertRaises(ValueError):
            rules.parse({'link_fix': [{'page': 'a.md', 'href': '#x', 'to': '#y'}]})


class Slugs(unittest.TestCase):
    def test_python_markdown_slugs(self):
        self.assertEqual(book.slugify('VDU 23, 0, &89, command, [<args>]: Mouse control **'),
                         'vdu-23-0-89-command-args-mouse-control')
        self.assertEqual(book.slugify('VDP pflags (vdpflags)'), 'vdp-pflags-vdpflags')
        self.assertEqual(book.slugify('Arcs §§§§'), 'arcs')


if __name__ == '__main__':
    unittest.main()
