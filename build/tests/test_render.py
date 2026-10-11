import unittest

from refbuild import layout, markdown as md, render
from refbuild.render import (BREAK, CODE_OFF, CODE_ON, EM_OFF, EM_ON, END, HEAD, LINK,
                             LINK_END, NOTE, PARA, PRE, RULE, SPACE)


def markup(text):
    r = render.Renderer()
    r.blocks(md.parse(text).blocks, first_space=False)

    return bytes(r.out)


def screen(text, width=40):
    return [l.decode('latin-1') for l in layout.text(markup(text), width)]


class Markup(unittest.TestCase):
    def test_paragraph(self):
        self.assertEqual(markup('Hello *there* `x`.'),
                         bytes([PARA, 0, 0]) + b'Hello ' + bytes([EM_ON]) + b'there'
                         + bytes([EM_OFF]) + b' ' + bytes([CODE_ON]) + b'x' + bytes([CODE_OFF])
                         + b'.' + bytes([END]))

    def test_link_carries_its_topic(self):
        r = render.Renderer()
        b = r.inlines([('ref', 0x1234, [('text', 'go')])])
        self.assertEqual(b, bytes([LINK, 0x34, 0x12]) + b'go' + bytes([LINK_END]))

    def test_external_link_keeps_its_url(self):
        r = render.Renderer()
        self.assertEqual(r.inlines([('ext', 'https://x.org', [('text', 'site')])]),
                         b'site <https://x.org>')
        self.assertEqual(r.inlines([('ext', 'https://x.org', [('text', 'https://x.org')])]),
                         b'https://x.org')

    def test_heading_level(self):
        self.assertEqual(markup('### Three')[:3], bytes([HEAD, 3, 0]))

    def test_spacing(self):
        m = markup('one\n\ntwo')
        self.assertEqual(m[0], PARA)
        self.assertEqual(m[m.index(b'one' + bytes([END])) + 4], PARA | SPACE)

    def test_code_block_keeps_lines(self):
        self.assertEqual(markup('```\na\n  b\n```'), bytes([PRE, 0, 0]) + b'a\n  b\n' + bytes([END]))

    def test_note(self):
        m = markup('!!! note\n\n    Careful.')
        self.assertEqual(m[:3], bytes([PARA | NOTE, 2, 2]))
        # The body follows its title without a blank line.
        self.assertIn(bytes([END, PARA | NOTE, 2, 2]) + b'Careful.', m)

    def test_rule(self):
        self.assertEqual(markup('---'), bytes([RULE, 0, 0, END]))

    def test_hard_break(self):
        self.assertIn(bytes([BREAK]), markup('one  \ntwo'))


class Characters(unittest.TestCase):
    def test_agon_font(self):
        self.assertEqual(render.agon_bytes('§ µ ® x'), b'\xa7 \xb5 \xae x')
        self.assertEqual(render.agon_bytes('‘q’ ⇨ ⇐ ⇧ ⇓'),
                         b"'q' \x90 \x8f \x8d v")
        self.assertEqual(render.agon_bytes('中'), b'?')

    def test_no_control_bytes_from_text(self):
        self.assertEqual(render.agon_bytes('a\x01b'), b'a?b')


class Lists(unittest.TestCase):
    def test_bullets_hang(self):
        self.assertEqual(screen('- one two three four five six seven', 16),
                         ['- one two three', '  four five six', '  seven'])

    def test_nested_and_numbered(self):
        self.assertEqual(screen('1. first\n    - inner\n2. second'),
                         ['1. first', '   - inner', '2. second'])


class Tables(unittest.TestCase):
    def test_ruled_columns(self):
        self.assertEqual(screen('| ID | Name |\n|---|---|\n| 1 | one |\n| 22 | two |'),
                         ['ID | Name', '---+-----', '1  | one', '22 | two'])

    def test_wide_table_wraps_cells(self):
        long = ' '.join(['word'] * 30)
        lines = screen(f'| Bit | Meaning |\n|---|---|\n| 1 | {long} |\n| 2 | short |', 80)
        self.assertTrue(all(len(l) <= render.TABLE_WIDTH for l in lines))
        self.assertEqual(lines[0], 'Bit | Meaning')
        self.assertTrue(lines[2].startswith('1   | word word'))
        self.assertTrue(lines[3].startswith('    | word'))
        # Rows that wrap are ruled apart.
        self.assertIn('----+-', lines[-2])
        self.assertEqual(lines[-1], '2   | short')

    def test_link_in_a_cell_stays_a_link(self):
        t = md.Table([[('text', 'A')]], [None], [[[('ref', 7, [('text', 'seven')])]]], 1)
        b = render.table(t, render.Renderer())
        self.assertIn(bytes([LINK, 7, 0]) + b'seven' + bytes([LINK_END]), b)

    def test_column_widths_give_way_widest_first(self):
        self.assertEqual(render.column_widths([10, 50], [10, 10], 40), [10, 27])
        # Neither can give way past its minimum: the table is wider, and scrolls.
        self.assertEqual(render.column_widths([30, 30], [20, 20], 40), [20, 20])


class Layout(unittest.TestCase):
    def test_wraps_at_width(self):
        self.assertEqual(screen('aaa bbb ccc ddd', 8), ['aaa bbb', 'ccc ddd'])

    def test_exact_fit(self):
        self.assertEqual(screen('aaaa bbb', 8), ['aaaa bbb'])

    def test_runs_of_spaces_are_one(self):
        # The wiki puts two spaces after a full stop; the markup has one.
        self.assertEqual(screen('one.  Two', 40), ['one. Two'])
        self.assertEqual(screen('one.  Two three', 9), ['one. Two', 'three'])

    def test_long_word_is_cut(self):
        self.assertEqual(screen('abcdefghij', 4), ['abcd', 'efgh', 'ij'])

    def test_controls_take_no_room(self):
        self.assertEqual(screen('`aaa` *bbb*', 7), ['aaa bbb'])

    def test_break(self):
        self.assertEqual(screen('one  \ntwo', 40), ['one', 'two'])

    def test_pre_is_never_wrapped(self):
        self.assertEqual(screen('```\n' + 'x' * 50 + '\n```', 20), ['x' * 50])

    def test_note_bar(self):
        self.assertEqual(screen('!!! note "Hey"\n    Body text.'), ['| Hey', '| Body text.'])

    def test_rule(self):
        self.assertEqual(screen('---', 10), ['-' * 10])

    def test_blank_line_dropped_at_the_top(self):
        r = render.Renderer()
        r.block(PARA, 0, 0, b'x', space=True)
        self.assertEqual(layout.text(bytes(r.out), 10), [b'x'])

    def test_links_on_a_line(self):
        m = bytes([PARA, 0, 0]) + b'see ' + bytes([LINK, 5, 0]) + b'this' + bytes([LINK_END]) \
            + b' and ' + bytes([LINK, 0, 1]) + b'that' + bytes([LINK_END]) + bytes([END])
        ln, = layout.lines(m, 40)
        self.assertEqual(layout.links(m, ln), [(5, 4, 8), (256, 13, 17)])

    def test_link_target_with_a_zero_byte_is_not_the_end(self):
        m = bytes([PARA, 0, 0]) + bytes([LINK, 0, 0]) + b'top' + bytes([LINK_END, END])
        self.assertEqual(layout.text(m, 10), [b'top'])


if __name__ == '__main__':
    unittest.main()
