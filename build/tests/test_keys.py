import unittest

from refbuild import book, keys, rules

from .test_book import make_docs

NAV = """\
nav:
  - 'Cmds': cmds.md
  - 'API': api.md
  - 'Prose': prose.md
"""

PAGES = {
    'cmds.md': """\
        # VDU 23, 27: Bitmaps

        ## `VDU 23, 27, &21, bitmapId; 0;`

        ## Command 6: Conditionally call a buffer

        `VDU 23, 0, &A0, bufferId; 6, operation`

        ## Filled triangles (PLOT codes &50-&52, &58-5A)

        ## `CD`

        Syntax: `*CD <path>` (Aliases include `ChDir` and `Go`)

        ## `.` (dot)

        Syntax: `*.` (Alias for `Cat`)

        ## `Cat`

        ## Notes on sprites
        """,
    'api.md': """\
        # The API

        ## `0x0A`: mos_fopen

        ## Sysvars

        ```
        sysvar_time:    EQU 00h
        sysvar_keyascii: EQU 05h
        ```
        """,
    'prose.md': """\
        # Getting started

        ## How the SD card is laid out
        """,
}

RULES = {
    'skip': [{'prefix': 'VDU 23, 0, &A0', 'units': [2], 'why': 'buffer ID'}],
    'page': [
        {'file': 'cmds.md', 'vdu': True, 'plot': True, 'heading_code': True,
         'body': [{'match': r'\(Aliases? include ([^)]*)\)'}]},
        {'file': 'api.md',
         'heading': [{'match': r'^(0x[0-9A-Fa-f]+): (\w+)', 'keys': ['{2}', 'mos {1}']}],
         'code': [{'match': r'^(sysvar_\w+):'}]},
        {'file': 'prose.md', 'prose': True},
    ],
    'key': [{'page': 'api.md', 'keys': ['mos api'], 'why': 'test'}],
}


def build(pages=PAGES, data=RULES):
    b = book.build(make_docs(pages, NAV))
    r = rules.parse(data)

    return b, r, keys.build(b, r)


class Extract(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.b, cls.r, cls.ix = build()

    def topic_keys(self, title):
        nums = [t.num for t in self.b.topics if t.title == title]
        self.assertEqual(len(nums), 1, title)

        return {k.key for k in self.ix.keys if k.topic == nums[0]}

    def test_vdu_from_heading_code(self):
        self.assertIn('vdu 23 27 33', self.topic_keys('VDU 23, 27, &21, bitmapId; 0;'))

    def test_vdu_from_a_heading_without_code(self):
        self.assertIn('vdu 23 27', self.topic_keys('VDU 23, 27: Bitmaps'))

    def test_vdu_from_opening_code_with_skip(self):
        self.assertIn('vdu 23 0 160 _ 6',
                      self.topic_keys('Command 6: Conditionally call a buffer'))

    def test_plot_ranges(self):
        ks = self.topic_keys('Filled triangles (PLOT codes &50-&52, &58-5A)')
        for code in (80, 81, 82, 88, 89, 90):
            self.assertIn(f'plot {code}', ks)
            self.assertIn(f'vdu 25 {code}', ks)
        self.assertNotIn('plot 83', ks)

    def test_heading_code_and_aliases(self):
        self.assertTrue({'cd', 'chdir', 'go'} <= self.topic_keys('CD'))

    def test_alias_for_points_the_other_way(self):
        # "(Alias for `Cat`)" is on the alias's own entry: `cat` stays Cat's.
        cat_topics = {k.topic for k in self.ix.keys if k.key == 'cat'}
        self.assertEqual({self.b.topics[t].title for t in cat_topics}, {'Cat'})

    def test_heading_regex(self):
        self.assertTrue({'mos_fopen', 'mos 10'} <= self.topic_keys('0x0A: mos_fopen'))

    def test_code_regex(self):
        self.assertTrue({'sysvar_time', 'sysvar_keyascii'} <= self.topic_keys('Sysvars'))

    def test_explicit_key(self):
        self.assertIn('mos api', self.topic_keys('The API'))

    def test_title_words(self):
        ks = self.topic_keys('How the SD card is laid out')
        self.assertEqual(ks, {'sd', 'card', 'laid'})

    def test_numbers_are_not_title_words(self):
        self.assertNotIn('vdu 10', self.topic_keys('0x0A: mos_fopen'))


class Gaps(unittest.TestCase):
    def test_headings_without_keys_on_rule_pages(self):
        _, _, ix = build()
        self.assertEqual(sorted(g[2] for g in ix.gaps), ['. (dot)', 'Notes on sprites'])

    def test_a_page_with_no_rules_is_all_gaps(self):
        data = dict(RULES)
        data['page'] = [p for p in RULES['page'] if p['file'] != 'prose.md']
        _, _, ix = build(data=data)
        self.assertIn('How the SD card is laid out', [g[2] for g in ix.gaps])

    def test_rule_that_matches_nothing(self):
        data = dict(RULES)
        data['page'] = RULES['page'] + [{'file': 'gone.md', 'prose': True}]
        _, _, ix = build(data=data)
        self.assertIn('[[page]] gone.md: not in the wiki', ix.unused_rules)

    def test_unknown_page_setting(self):
        with self.assertRaises(ValueError):
            rules.parse({'page': [{'file': 'a.md', 'vdus': True}]})


class Lookup(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.b, cls.r, cls.ix = build()

    def look(self, q):
        res = keys.lookup(self.ix.keys, q, self.r.normalize)
        if res[0] == 'list':
            return 'list', [self.b.topics[t].title for _, t in res[1]]
        if res[0] == 'none':
            return ('none',)

        return res[0], [self.b.topics[t].title for t in res[-1]]

    def test_exact(self):
        self.assertEqual(self.look('VDU 23,27,&21'), ('exact', ['VDU 23, 27, &21, bitmapId; 0;']))

    def test_longest_prefix(self):
        self.assertEqual(self.look('vdu 23 27 33 5 0'),
                         ('prefix', ['VDU 23, 27, &21, bitmapId; 0;']))

    def test_vdu_alone_is_never_the_prefix(self):
        self.assertEqual(self.look('vdu 99 1'), ('none',))

    def test_list(self):
        self.assertEqual(self.look('sysvar_'), ('list', ['Sysvars', 'Sysvars']))

    def test_dollar_prefix(self):
        self.assertEqual(keys.prefixes('hotkey$12'), ['hotkey$'])
        self.assertEqual(keys.prefixes('vdu 23 1'), ['vdu 23'])

    def test_none(self):
        self.assertEqual(self.look('xyzzy'), ('none',))
        self.assertEqual(self.look('  '), ('none',))


if __name__ == '__main__':
    unittest.main()
