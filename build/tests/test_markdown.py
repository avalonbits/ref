import unittest

from refbuild import markdown as md


def parse(text):
    return md.parse(text).blocks


class Headings(unittest.TestCase):
    def test_level_text_and_id(self):
        h, = parse('## `VDU 4`: Write text at text cursor {#vdu-4}')
        self.assertIsInstance(h, md.Heading)
        self.assertEqual(h.level, 2)
        self.assertEqual(h.id, 'vdu-4')
        self.assertEqual(h.inlines, [('code', 'VDU 4'), ('text', ': Write text at text cursor')])

    def test_closing_hashes_and_no_id(self):
        h, = parse('### MODE ###')
        self.assertEqual((h.level, h.id, h.inlines), (3, None, [('text', 'MODE')]))

    def test_hash_inside_code_block_is_not_a_heading(self):
        blocks = parse('```c\n#include "agon/vdp.h"\n```')
        self.assertEqual([type(b) for b in blocks], [md.Code])

    def test_setext(self):
        h1, p, h2 = parse('VDP Buffered Commands API\n=========================\n\nText.\n\nPart\n----')
        self.assertEqual((h1.level, md.plain(h1.inlines), h1.line), (1, 'VDP Buffered Commands API', 1))
        self.assertEqual((h2.level, md.plain(h2.inlines)), (2, 'Part'))

    def test_setext_ends_a_paragraph(self):
        p, h = parse('one\ntwo\n===')
        self.assertEqual(md.plain(p.inlines), 'one')
        self.assertEqual(md.plain(h.inlines), 'two')

    def test_line_numbers(self):
        blocks = parse('# One\n\ntext\n\n## Two')
        self.assertEqual([b.line for b in blocks], [1, 3, 5])


class Lists(unittest.TestCase):
    def test_nested_by_indent(self):
        lst, = parse('- a\n    - a1\n    - a2\n- b')
        self.assertFalse(lst.ordered)
        self.assertEqual(len(lst.items), 2)
        inner = lst.items[0][1]
        self.assertIsInstance(inner, md.ListBlock)
        self.assertEqual([md.plain(it[0].inlines) for it in inner.items], ['a1', 'a2'])

    def test_nested_by_two_spaces(self):
        lst, = parse('- a\n  - a1\n- b')
        self.assertEqual(len(lst.items), 2)
        self.assertIsInstance(lst.items[0][1], md.ListBlock)

    def test_tab_indent(self):
        lst, = parse('1. step\n\t- detail\n2. next')
        self.assertTrue(lst.ordered)
        self.assertEqual(len(lst.items), 2)
        self.assertIsInstance(lst.items[0][1], md.ListBlock)

    def test_list_right_after_a_paragraph(self):
        p, lst = parse('Returns:\n- `A`: the key')
        self.assertIsInstance(p, md.Para)
        self.assertIsInstance(lst, md.ListBlock)

    def test_loose_list_stays_one_list(self):
        lst, = parse('- a\n\n- b\n\n- c')
        self.assertEqual(len(lst.items), 3)

    def test_lazy_continuation(self):
        lst, = parse('- one\ntwo')
        self.assertEqual(md.plain(lst.items[0][0].inlines), 'one two')

    def test_ordered_start(self):
        lst, = parse('3. c\n4. d')
        self.assertEqual(lst.start, 3)


class Code(unittest.TestCase):
    def test_fenced_keeps_text_and_lang(self):
        c, = parse('```c\nint x;\n\tint y;\n```')
        self.assertEqual(c.lang, 'c')
        self.assertEqual(c.text, 'int x;\n    int y;')

    def test_fenced_in_a_list_is_dedented(self):
        lst, = parse('- see:\n\n    ```\n    VDU 1\n    ```')
        code = lst.items[0][1]
        self.assertEqual(code.text, 'VDU 1')

    def test_indented(self):
        p, c = parse('text\n\n    LD A, 1\n    RET')
        self.assertEqual(c.text, 'LD A, 1\nRET')

    def test_unclosed_fence_is_an_error(self):
        with self.assertRaises(md.ParseError) as e:
            parse('a\n\n```\nnever closed')
        self.assertEqual(e.exception.line, 3)


class Tables(unittest.TestCase):
    def test_cells_and_aligns(self):
        t, = parse('| ID | Name |\n|---:|:---|\n| 1 | `a|b` |\n| 2 |')
        self.assertEqual(t.aligns, ['r', 'l'])
        self.assertEqual(t.rows[0][1], [('code', 'a|b')])
        self.assertEqual(t.rows[1][1], [])                  # padded

    def test_lone_backtick_is_text(self):
        t, = parse('| a | b | c |\n|---|---|---|\n| BK | ` ~ | x |')
        self.assertEqual([md.plain(c) for c in t.rows[0]], ['BK', '` ~', 'x'])

    def test_cell_anchor(self):
        t, = parse('| ID | What |\n|---|---|\n| 0x500 | Last char {#last-char} |')
        self.assertEqual(t.anchors, ['last-char'])
        self.assertEqual(md.plain(t.rows[0][1]), 'Last char')

    def test_table_right_after_a_paragraph(self):
        p, t = parse('Flags:\n| a | b |\n|---|---|\n| 1 | 2 |')
        self.assertIsInstance(t, md.Table)


class Containers(unittest.TestCase):
    def test_admonition(self):
        n, after = parse('!!! danger "Attention"\n\n    Careful.\n\n    - one\n\nAfter.')
        self.assertEqual((n.kind, n.title), ('danger', 'Attention'))
        self.assertEqual([type(b) for b in n.blocks], [md.Para, md.ListBlock])
        self.assertEqual(md.plain(after.inlines), 'After.')

    def test_details(self):
        n, = parse('??? note "More"\n    hidden')
        self.assertEqual((n.kind, n.title), ('details', 'More'))

    def test_quote(self):
        q, = parse('> Note: the tile\n> engine')
        self.assertEqual(md.plain(q.blocks[0].inlines), 'Note: the tile engine')

    def test_footnotes(self):
        page = md.parse('Text[^1].\n\n[^1]: The note.\n    More of it.')
        self.assertEqual(page.blocks[0].inlines, [('text', 'Text'), ('fnref', '1'), ('text', '.')])
        self.assertEqual(md.plain(page.footnotes['1'][0].inlines), 'The note. More of it.')

    def test_front_matter_and_toc_skipped(self):
        blocks = parse('---\nhide:\n  - toc\n---\n# FAQ\n\n[TOC]\n\nText')
        self.assertEqual([type(b) for b in blocks], [md.Heading, md.Para])

    def test_rule(self):
        self.assertIsInstance(parse('---')[0], md.Rule)


class Inlines(unittest.TestCase):
    def inl(self, text):
        return md.Inline({}).parse(text)

    def test_emphasis(self):
        self.assertEqual(self.inl('a *b* **c**'),
                         [('text', 'a '), ('em', [('text', 'b')]), ('text', ' '),
                          ('strong', [('text', 'c')])])

    def test_intraword_underscore_is_text(self):
        self.assertEqual(self.inl('sysvar_keyascii and mos_getkey'),
                         [('text', 'sysvar_keyascii and mos_getkey')])
        # An underscore inside a word never opens, even with a closer later.
        self.assertEqual(self.inl('Run_Path and Path_ here'),
                         [('text', 'Run_Path and Path_ here')])

    def test_underscore_emphasis(self):
        self.assertEqual(self.inl('each _bit_ here'),
                         [('text', 'each '), ('em', [('text', 'bit')]), ('text', ' here')])

    def test_trailing_stars_are_text(self):
        self.assertEqual(self.inl('Mouse control **'), [('text', 'Mouse control **')])

    def test_code_spans(self):
        self.assertEqual(self.inl('``a ` b`` and `c`'),
                         [('code', 'a ` b'), ('text', ' and '), ('code', 'c')])

    def test_links(self):
        self.assertEqual(self.inl('[the `x` call](./API.md#x "t")'),
                         [('link', './API.md#x', [('text', 'the '), ('code', 'x'),
                                                  ('text', ' call')])])

    def test_reference_link(self):
        page = md.parse('See [docs][d].\n\n[d]: https://x.org')
        self.assertEqual(page.blocks[0].inlines[1], ('link', 'https://x.org', [('text', 'docs')]))

    def test_autolink_and_image(self):
        self.assertEqual(self.inl('<https://a.b> ![font map](f.png)'),
                         [('link', 'https://a.b', [('text', 'https://a.b')]), ('text', ' '),
                          ('img', 'font map', 'f.png')])

    def test_html(self):
        self.assertEqual(self.inl('one<br>two <b>bold</b> <Moslet$Path>'),
                         [('text', 'one'), ('br',), ('text', 'two bold <Moslet$Path>')])

    def test_escapes(self):
        self.assertEqual(self.inl(r'\* Requires VDP 1.03'), [('text', '* Requires VDP 1.03')])

    def test_hard_break(self):
        p, = parse('one  \ntwo')
        self.assertEqual(p.inlines, [('text', 'one'), ('br',), ('text', ' two')])


if __name__ == '__main__':
    unittest.main()
