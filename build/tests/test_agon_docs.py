"""The whole of agon-docs at the pinned commit."""

import unittest

from refbuild import book, markdown as md

from .agon_docs import docs, load_rules, the_book


class WholeWiki(unittest.TestCase):
    def test_every_link_resolves(self):
        _, d = docs()
        book.build(d, load_rules().link_fixes)         # raises BookError if not

    def test_every_nav_page_is_in(self):
        b = the_book()
        for page in ('index.md', 'mos/API.md', 'mos/Star-Commands.md',
                     'vdp/Buffered-Commands-API.md', 'vdp/Enhanced-Audio-API.md',
                     'BBC-BASIC-for-Agon.md', 'Third-Party-Projects.md'):
            self.assertIn(page, b.pages)
        self.assertEqual(len(b.pages), 32)              # 31 in the nav, and Projects.md

    def test_topics(self):
        b = the_book()
        self.assertGreater(len(b.topics), 700)
        by_title = {t.title: t for t in b.topics}
        self.assertIn('VDU 23, 27, 3, x; y;: Draw current bitmap on screen at pixel position x, y',
                      by_title)
        self.assertIn('0x0A: mos_fopen', by_title)

    def test_contents_follow_the_nav(self):
        b = the_book()
        self.assertEqual([n.title for n in b.contents],
                         ['Welcome & getting started', 'MOS', 'VDP', 'BBC Basic', 'GPIO',
                          'Guides', 'Additional resources', book.OTHER_PAGES])
        mos = b.contents[1]
        self.assertEqual([n.title for n in mos.children][:3], ['Overview', 'Command line', 'API'])

    def test_table_cell_anchor(self):
        b = the_book()
        num = b.anchors['vdp/VDP-Variables.md']['last-colour']
        self.assertEqual(b.topics[num].page, 'vdp/VDP-Variables.md')

    def test_no_stray_inline_markup(self):
        # Backticks left in text mean a code span went unparsed. The wiki has
        # two typos of its own (`VDU`` and `direction``), and the keyboard
        # map's "` ~" key, which is a backtick.
        stray = []
        for t in the_book().topics:
            for blk, inl in book.blocks_inlines(t.blocks):
                for nd in book._walk_inlines(inl):
                    if nd[0] == 'text' and '`' in nd[1]:
                        stray.append((t.page, blk.line))
        self.assertEqual(sorted(stray), [('mos/Keyboard.md', 149),
                                         ('vdp/Enhanced-Audio-API.md', 373),
                                         ('vdp/VDU-Commands.md', 276)])


if __name__ == '__main__':
    unittest.main()
