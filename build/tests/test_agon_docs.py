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


# (query, page, the topic's title starts with): each must open that topic,
# exactly or as the longest key the query starts with.
REACHES = [
    ('vdu 23 27 33', 'vdp/Bitmaps-API.md', 'VDU 23, 27, &21, bitmapId; 0;'),
    ('vdu 23,27,&21', 'vdp/Bitmaps-API.md', 'VDU 23, 27, &21, w; h; format'),
    ('23;27;0x21', 'vdp/Bitmaps-API.md', 'VDU 23, 27, &21, bitmapId; 0;'),
    ('VDU 23 27 33 5 0', 'vdp/Bitmaps-API.md', 'VDU 23, 27, &21, bitmapId; 0;'),
    ('mos_fopen', 'mos/API.md', '0x0A: mos_fopen'),
    ('mos 0x0a', 'mos/API.md', '0x0A: mos_fopen'),
    ('cd', 'mos/Star-Commands.md', 'CD'),
    ('run$path', 'mos/System-Variables.md', 'System Path variables'),
    ('plot 85', 'vdp/PLOT-Commands.md', 'Filled triangles'),
    ('gcol', 'BBC-BASIC-for-Agon.md', 'GCOL'),
    ('vdu 23 27 3', 'vdp/Bitmaps-API.md', 'VDU 23, 27, 3, x; y;'),
    ('vdu 23 27 3 100; 50;', 'vdp/Bitmaps-API.md', 'VDU 23, 27, 3, x; y;'),
    ('vdu 0', 'vdp/VDU-Commands.md', 'VDU 0:'),
    ('vdu 4', 'vdp/VDU-Commands.md', 'VDU 4:'),
    ('vdu 7', 'vdp/VDU-Commands.md', 'VDU 7:'),
    ('vdu 12', 'vdp/VDU-Commands.md', 'VDU 12:'),
    ('vdu 17 3', 'vdp/VDU-Commands.md', 'VDU 17, colour:'),
    ('vdu 19,1,4,0,0,0', 'vdp/VDU-Commands.md', 'VDU 19, l, p, r, g, b'),
    ('vdu 22 3', 'vdp/VDU-Commands.md', 'VDU 22, n:'),
    ('vdu 22,3', 'vdp/Screen-Modes.md', 'VDU 22, mode'),
    ('vdu 25 85', 'vdp/PLOT-Commands.md', 'Filled triangles'),
    ('plot 4', 'vdp/PLOT-Commands.md', 'Line drawing'),
    ('plot 157', 'vdp/PLOT-Commands.md', 'Circle drawing'),
    ('vdu 23 0 &c0 0', 'vdp/Screen-Modes.md', 'VDU 23, 0, &C0, n'),
    ('vdu 23,0,&89,3,1;', 'vdp/System-Commands.md', 'VDU 23, 0, &89, 3, cursorId;'),
    ('vdu 23 0 &87', 'vdp/System-Commands.md', 'VDU 23, 0, &87'),
    ('VDU 23, 0, &A0, 1; 6', 'vdp/Buffered-Commands-API.md', 'Command 6:'),
    ('vdu 23 0 160 1 0 0 20 0', 'vdp/Buffered-Commands-API.md', 'Command 0:'),
    ('vdu 23,0,&a0,&ffff;3,100;', 'vdp/Buffered-Commands-API.md', 'Command 3:'),
    ('vdu 23 0 &a0', 'vdp/Buffered-Commands-API.md', 'VDP Buffered Commands API'),
    ('VDU 23,0,&85,1,5,0', 'vdp/Enhanced-Audio-API.md', 'Command 5, 0: Load sample'),
    ('vdu 23 0 &85 0 0 127 440; 500;', 'vdp/Enhanced-Audio-API.md', 'Command 0: Play note'),
    ('vdu 23 0 133', 'vdp/System-Commands.md', 'VDU 23, 0, &85, channel'),
    ('vdu 23 27 4 0', 'vdp/Bitmaps-API.md', 'VDU 23, 27, 4, n'),
    ('vdu 23 0 &c8 3', 'vdp/Context-Management-API.md', 'VDU 23, 0, &C8, 3'),
    ('vdu 23 0 &c4 2 1; 3 255 0 0', 'vdp/Copper-API.md', 'Command 2: Set a palette entry'),
    ('vdu 23 0 &c2 0', 'vdp/Tile-Engine.md', 'Initialise/Reset Tile Bank'),
    ('vdu 23 0 &95 1', 'vdp/Font-API.md', 'VDU 23, 0, &95, 1, bufferId;'),
    ('vdu 23,0,&95', 'vdp/Font-API.md', 'VDU 23, 0, &95: Font management'),
    ('mos_getkey', 'mos/API.md', '0x00: mos_getkey'),
    ('mos 0', 'mos/API.md', '0x00: mos_getkey'),
    ('mos_load', 'mos/API.md', '0x01: mos_load'),
    ('mos_sysvars', 'mos/API.md', '0x08: mos_sysvars'),
    ('mos_editline', 'mos/API.md', '0x09: mos_editline'),
    ('ffs_dfindfirst', 'mos/API.md', '0x94: ffs_dfindfirst'),
    ('mos &50', 'mos/API.md', '0x50: mos_getfunction'),
    ('rst 08h', 'mos/API.md', 'RST 08h'),
    ('rst 8', 'mos/API.md', 'RST 08h'),
    ('rst 10', 'mos/API.md', 'RST 10h'),
    ('RST 18H', 'mos/API.md', 'RST 18h'),
    ('sysvar_scrmode', 'mos/API.md', 'System State Information'),
    ('sysvar_keyascii', 'mos/API.md', 'System State Information'),
    ('f_printf', 'mos/C-Functions.md', '0x05 - int'),
    ('setvarval', 'mos/C-Functions.md', '0x09 - int'),
    ('getsysvars', 'mos/C-Functions.md', '0x10 - void'),
    ('SD_init', 'mos/C-Functions.md', '0x00 - BYTE'),
    ('*CAT', 'mos/Star-Commands.md', 'Cat'),
    ('dir', 'mos/Star-Commands.md', 'Cat'),
    ('erase', 'mos/Star-Commands.md', 'Erase'),
    ('loadfile', 'mos/Star-Commands.md', 'LoadFile'),
    ('setmacro', 'mos/Star-Commands.md', 'SetMacro'),
    ('cli$prompt', 'mos/System-Variables.md', 'CLI$Prompt'),
    ('moslet$path', 'mos/System-Variables.md', 'System Path variables'),
    ('hotkey$1', 'mos/System-Variables.md', 'Hotkeys'),
    ('obey$dir', 'mos/System-Variables.md', 'Obey files'),
    ('mode', 'BBC-BASIC-for-Agon.md', 'MODE'),
    ('get$', 'BBC-BASIC-for-Agon.md', 'GET$'),
    ('bye', 'BBC-BASIC-for-Agon.md', 'BYE'),
    ('sound', 'BBC-BASIC-for-Agon.md', 'SOUND'),
    ('copper', 'vdp/Copper-API.md', 'The "Copper" API'),
    ('sprite', 'vdp/Bitmaps-API.md', 'VDU 23, 27, 4, n'),
]


class Reaches(unittest.TestCase):
    def test_every_query_opens_its_topic(self):
        from refbuild import keys
        b = the_book()
        r = load_rules()
        ix = keys.build(b, r)
        self.assertGreaterEqual(len(REACHES), 56)
        for q, page, title in REACHES:
            with self.subTest(q=q):
                res = keys.lookup(ix.keys, q, r.normalize)
                self.assertIn(res[0], ('exact', 'prefix'), res)
                got = [(b.topics[t].page, b.topics[t].title) for t in res[-1]]
                self.assertTrue(any(p == page and t.startswith(title) for p, t in got), got)

    def test_no_new_gaps(self):
        from refbuild import keys, report
        import os
        from .agon_docs import HERE
        ix = keys.build(the_book(), load_rules())
        known = report.read_gaps(os.path.join(HERE, 'gaps.txt'))
        self.assertEqual(report.new_gaps(ix, known), [])
        self.assertEqual(ix.unused_rules, [])
