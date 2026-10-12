"""md2ref, built for the PC: Markdown pages in, a document ref reads out.

Needs out/host/md2ref and out/host/reftool, which `make host` builds (and
`make test` first). The documents it writes are read back through the
reader core, as ref on the Agon reads them.
"""

import glob
import hashlib
import os
import shutil
import struct
import subprocess
import tempfile
import unittest

from refbuild.keys import STOP_WORDS

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


def views(data):
    """Each topic's view: its markup, and its span's after it."""
    out = []
    with tempfile.NamedTemporaryFile(suffix='.dat') as f:
        f.write(data)
        f.flush()
        raw = subprocess.run([TOOL, f.name, 'views'], capture_output=True, check=True).stdout
    i = 0
    while i < len(raw):
        n = raw[i] | raw[i + 1] << 8
        out.append(raw[i + 2:i + 2 + n])
        i += 2 + n

    return out


def link(t):
    return bytes([0x14, t & 0xFF, t >> 8])


def compile_pages(pages, *args):
    """The document md2ref writes from pages, {name: text}, in a folder."""
    with tempfile.TemporaryDirectory() as d:
        for name, text in pages.items():
            with open(os.path.join(d, name), 'w') as f:
                f.write(text)
        out = os.path.join(d, 'out.dat')
        p = subprocess.run([MD2REF, '-o', out, *args, d], capture_output=True, text=True)
        data = None
        if p.returncode == 0:
            with open(out, 'rb') as f:
                data = f.read()
        # Scratch files left -- and, after a failure, any document at all.
        left = sorted(set(os.listdir(d)) - set(pages) - ({'out.dat'} if data else set()))

        return p, data, left


def filler(word, n):
    """n paragraphs of text naming word, about 100 bytes each."""
    return ''.join('The %s paragraph %d says nothing much at all, at some length, to fill a '
                   'topic.\n\n' % (word, k) for k in range(n))


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

    def test_settings_hold_the_stop_words(self):
        # No hex-after words, no skips, and the builder's stop words, sorted.
        data = compile_doc('doc')
        norm, end = struct.unpack_from('<II', data, 84)
        settings = data[norm:end]
        want = bytes([0, 0, len(STOP_WORDS)])
        for w in sorted(STOP_WORDS):
            want += bytes([len(w)]) + w.encode()
        self.assertEqual(settings, want)

    def test_title_from_the_first_heading_or_t(self):
        self.assertIn('title My Program\n', ask(compile_doc('doc'), 'info'))
        self.assertIn('title Other\n', ask(compile_doc('-t', 'Other', 'doc'), 'info'))

    def test_header_marks_md2ref(self):
        info = ask(compile_doc('doc'), 'info')
        self.assertIn('commit md2ref\n', info)
        self.assertIn('date \n', info)

    def test_links_forward_back_and_in_a_cell(self):
        # index.md links forward, to pages not read yet: placeholders,
        # patched; api.md links back, to a page read; and in a table's cell,
        # to its own page, read once the page is.
        v = views(compile_doc('doc'))
        self.assertIn(link(2) + b'the API', v[0])
        self.assertIn(link(3) + b'\x12frob', v[0])         # [`frob`](api.md#frob)
        self.assertIn(link(0) + b'the start', v[3])
        self.assertIn(link(3) + b'unused', v[3])            # in a cell, wrapped or not

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

    def test_which_text_makes_words(self):
        # Three letters or more of a-z, 0-9, _ and $, lower-cased, less the
        # stop words, all-digit runs, and runs of only _ and $: here doc
        # (the title), words, $ff, x86, upper, x_1, 9_9 and $12.
        p, data, left = compile_pages({'index.md': '# Doc\n\nWords: 1234 2048 $$$ ___ $_$ $ff x86 _x zz '
                                                   'Through UPPER further the x_1 9_9 $12.\n'})
        self.assertEqual(p.returncode, 0, p.stdout)
        self.assertIn('words 8\n', ask(data, 'info'))
        self.assertEqual(ask(data, 'search', ['upper', 'x86']).splitlines(),
                         ['1 0:0:1', '1 0:0:1'])

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
        with tempfile.TemporaryDirectory() as d:
            out = os.path.join(d, 'x.dat')
            p = md2ref('-o', out, os.path.join('bad', 'fence.md'))
            self.assertEqual(p.returncode, FAILED)
            self.assertEqual(p.stdout, 'bad/fence.md:3: error: code block never closed\n')
            self.assertEqual(os.listdir(d), [])     # nothing written, no scratch left

    def test_name_writes_ref_doc_from_the_current_directory(self):
        with tempfile.TemporaryDirectory() as d:
            shutil.copytree(DOC, os.path.join(d, 'proj'))
            proj = os.path.join(d, 'proj')
            p = subprocess.run([MD2REF, 'myprog'], capture_output=True, text=True, cwd=proj)
            self.assertEqual(p.returncode, 0, p.stdout)
            # Then the time, worded as zap and acc word it; a page this small
            # takes well under a second, which a clock read in the wrong
            # units would not.
            self.assertRegex(p.stdout,
                             r'^md2ref: ref-doc/myprog\.dat: 2 pages, 6 topics, 14 keys, 43 words\n'
                             r'Done in 0\.\d\d seconds\n$')
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

    def test_at_list_names_the_pages(self):
        # One a line, in order; \r\n and blank lines as an editor may leave.
        with tempfile.TemporaryDirectory() as d:
            lst = os.path.join(d, 'pages.mdl')
            with open(lst, 'w', newline='') as f:
                f.write('doc/index.md\r\n\r\ndoc/api.md  \r\n')
            self.assertEqual(compile_doc('@' + lst), compile_doc('doc'))

    def test_at_list_that_cannot_be_read(self):
        p = md2ref('-o', os.devnull, '@nosuch.mdl')
        self.assertEqual(p.returncode, FAILED)
        self.assertEqual(p.stdout, 'nosuch.mdl: error: cannot be read\n')

    def test_e_writes_the_errors_to_a_file_too(self):
        with tempfile.TemporaryDirectory() as d:
            errs = os.path.join(d, 'errs.txt')
            p = md2ref('-e', errs, '-o', os.path.join(d, 'x.dat'), os.path.join('bad', 'link.md'))
            self.assertEqual(p.returncode, FAILED)
            with open(errs) as f:
                self.assertEqual(f.read(), p.stdout)
            self.assertEqual(p.stdout,
                             "bad/link.md:5: error: cannot resolve link 'missing.md#here'\n")

    def test_e_file_holds_warnings_and_is_made_when_all_is_well(self):
        with tempfile.TemporaryDirectory() as d:
            errs = os.path.join(d, 'errs.txt')
            with open(errs, 'w') as f:
                f.write('stale\n')
            p = md2ref('-e', errs, '-w', '-o', os.path.join(d, 'x.dat'),
                       os.path.join('bad', 'link.md'))
            self.assertEqual(p.returncode, 0)
            with open(errs) as f:
                self.assertEqual(f.read(),
                                 "bad/link.md:5: warning: cannot resolve link 'missing.md#here'\n")
            md2ref('-e', errs, '-o', os.path.join(d, 'x.dat'), 'doc')
            with open(errs) as f:
                self.assertEqual(f.read(), '')

    def test_a_topic_too_big_to_show_its_children_links_to_them(self):
        # Big's subtree is over 16 KB, so it ends with links to its
        # children; Small's is not, so it shows them, and has none.
        page = ('# Big\n\n' + filler('big', 10) + '## Part one\n\n' + filler('one', 150)
                + '## Part two\n\n' + filler('two', 150) + '# Small\n\nIntro.\n\n## Bit\n\nA bit.\n')
        p, data, left = compile_pages({'index.md': page})
        self.assertEqual(p.returncode, 0, p.stdout)
        contents = ask(data, 'contents').splitlines()
        self.assertEqual([c.strip() for c in contents][:6],
                         ['index  [0]', 'Big  [1]', 'Part one  [2]', 'Part two  [3]', 'Small  [4]',
                          'Bit  [5]'])
        v = views(data)
        self.assertIn(b'In this section:', v[1])
        self.assertIn(link(2) + b'Part one', v[1])
        self.assertIn(link(3) + b'Part two', v[1])
        self.assertNotIn(b'In this section:', v[4])
        self.assertIn(b'A bit.', v[4])                      # shown with its child
        # The page's top, made when Small began, counts all under it.
        self.assertIn(link(1) + b'Big', v[0])
        self.assertIn(link(4) + b'Small', v[0])
        self.assertEqual(left, [])

    def test_a_link_to_a_page_not_read_yet_that_leads_nowhere(self):
        # Pending until every page is read; then an error at its line.
        p, data, left = compile_pages({'index.md': '# A\n\nSee\n[it](b.md#nothing).\n',
                                       'b.md': '# B\n\nText.\n'})
        self.assertEqual(p.returncode, FAILED)
        self.assertRegex(p.stdout, r"index\.md:3: error: cannot resolve link 'b\.md#nothing'\n$")
        self.assertIsNone(data)
        self.assertEqual(left, [])

    def test_a_second_level_1_heading_makes_the_page_its_top(self):
        # The first # heading is taken for the top until the second shows
        # it is not: then the page's top comes first, and the numbers of all
        # the page has read move up -- in the menus and the words as well.
        # One A is too big to show A1 and A2, so its links to them are
        # written when One B begins -- before Two shows the page needs a top.
        page = ('# One\n\n' + filler('alpha', 5) + '## One A\n\n' + filler('aardvark', 250)
                + '### A1\n\nFirst.\n\n### A2\n\nSecond.\n\n## One B\n\nBee.\n\n# Two\n\nZebra.\n')
        other = '# Other\n\nSee [One B](multi.md#one-b) and [Two](multi.md#two).\n'
        p, data, left = compile_pages({'index.md': other, 'multi.md': page})
        self.assertEqual(p.returncode, 0, p.stdout)
        contents = [c.strip() for c in ask(data, 'contents').splitlines()]
        self.assertEqual(contents, ['Other  [0]', 'multi  [1]', 'One  [2]', 'One A  [3]',
                                    'A1  [4]', 'A2  [5]', 'One B  [6]', 'Two  [7]'])
        v = views(data)
        self.assertIn(link(4) + b'A1', v[3])                # written before the top was made
        self.assertIn(link(5) + b'A2', v[3])
        self.assertIn(link(3) + b'One A', v[2])             # written after
        self.assertIn(link(6) + b'One B', v[2])
        self.assertIn(link(6) + b'One B', v[0])             # links from before the page
        self.assertIn(link(7) + b'Two', v[0])
        got = ask(data, 'search', ['aardvark', 'zebra', 'bee', 'second']).splitlines()
        self.assertEqual([g.split()[1].split(':')[0] for g in got], ['3', '7', '6', '5'])

    def test_a_page_with_its_only_level_1_heading_first_has_it_as_top(self):
        # Its title words are all stop words: no keys at all, which is fine.
        p, data, _ = compile_pages({'index.md': '# Only\n\nText.\n\n## Under\n\nMore.\n'})
        self.assertEqual(p.returncode, 0, p.stdout)
        self.assertEqual([c.strip() for c in ask(data, 'contents').splitlines()],
                         ['Only  [0]', 'Under  [1]'])
        self.assertIn('keys 0\n', ask(data, 'info'))

    def test_a_page_bigger_than_the_window(self):
        # Pages are read a run of blocks at a time, through 8 KB to start:
        # a 200 KB page compiles as it would whole.
        # With a 20 KB code block, which the window grows to hold.
        code = '```\n' + 'y = 2\n' * 3400 + 'endofcode\n```\n\n'
        page = '# Long\n\n' + ''.join('## Part %d\n\n' % k + filler('part%d' % k, 20)
                                       + (code if k == 50 else '') for k in range(100))
        p, data, left = compile_pages({'index.md': page})
        self.assertEqual(p.returncode, 0, p.stdout)
        self.assertEqual(ask(data, 'info').split('\ntopics ')[1].split('\n')[0], '101')
        got = ask(data, 'search', ['part0', 'part57', 'part99', 'endofcode']).splitlines()
        self.assertEqual([g.split()[1].split(':')[0] for g in got], ['1', '58', '100', '51'])

    def test_runs_end_only_where_blocks_must(self):
        # Each of these would come out differently if a run ended inside
        # it: blank lines then a line at the margin, inside a fence (never
        # closed); a paragraph's second line (two paragraphs); a list
        # numbered 1. 1. with a blank line between (numbered afresh); a
        # note's body. Repeated past the window's first 8 KB.
        unit = ('## Unit {k}\n\n```\nfence {k} top\n\nat the margin {k}\n```\n\n'
                'After {k}.\nAnd more {k}.\n\n'
                '1. item {k} one\n\n1. item {k} two\n\n   more of two {k}\n\n'
                '!!! note "Note {k}"\n    body {k}\n\n    more body {k}\n\nEnd {k}.\n\n')
        page = '# Runs\n\n' + ''.join(unit.format(k=k) for k in range(80))
        p, data, left = compile_pages({'index.md': page})
        self.assertEqual(p.returncode, 0, p.stdout)
        v = b''.join(views(data))
        text = ask(data, 'layout', (), '76')
        for k in (0, 41, 79):
            with self.subTest(unit=k):
                self.assertIn(('fence %d top\n\nat the margin %d\n\x00' % (k, k)).encode(), v)
                self.assertIn(('After %d. And more %d.\x00' % (k, k)).encode(), v)
                self.assertIn('1. item %d one\n\n2. item %d two\n' % (k, k), text)
                self.assertIn('| Note %d\n| body %d\n' % (k, k), text)

    def test_a_topic_with_many_words_has_each_once(self):
        # 200 words, each said twice: more than the topic's first table of
        # them holds, so it grows. One is in the title as well, which
        # marks it so however many times the text says it.
        body = ' '.join('w%dz' % k for k in range(200))
        p, data, left = compile_pages({'index.md': '# Doc w7z\n\n%s\n\n%s\n' % (body, body)})
        self.assertEqual(p.returncode, 0, p.stdout)
        self.assertIn('words 201\n', ask(data, 'info'))       # and doc
        self.assertEqual(ask(data, 'search', ['w0z', 'w7z', 'w199z']).splitlines(),
                         ['1 0:0:1', '1 0:1:1', '1 0:0:1'])

    def test_text_is_compressed(self):
        # Ten words in an order that never repeats for long: LZ4 finds the
        # words again, through its hash of every four bytes. The whole
        # document, its index too, comes to about half the page; with a
        # hash that told nothing apart it was larger than the page.
        names = ['alpha', 'bravo', 'charlie', 'delta', 'echo', 'foxtrot', 'golf', 'hotel',
                 'india', 'juliet']
        v, said = 1, []
        for _ in range(1800):
            v = (v * 1103515245 + 12345) % 2 ** 31
            said.append(names[v >> 16 & 7 if v & 1 else (v >> 8) % 10])
        page = '# Doc\n\n' + '\n\n'.join(' '.join(said[k:k + 12])
                                           for k in range(0, len(said), 12)) + '\n'
        p, data, left = compile_pages({'index.md': page})
        self.assertEqual(p.returncode, 0, p.stdout)
        self.assertLess(len(data), len(page) * 3 // 4)

    def test_characters_into_the_agon_font(self):
        # Printable ASCII as it is, to ~; DEL and a stray byte are '?';
        # quotes, dashes and arrows in UTF-8 become the font's own.
        page = '# T\n\nA ~z\x7f \u2018q\u2019 \u2014 \u2192 \xe9\n'
        p, data, left = compile_pages({'index.md': page})
        self.assertEqual(p.returncode, 0, p.stdout)
        self.assertIn(b"A ~z? 'q' - \x90 \xe9", b''.join(views(data)))

    def test_tabs_are_stops_of_four(self):
        # A tab indents a code block, and a second one indents its line
        # within it, to the next stop of four.
        p, data, left = compile_pages({'index.md': '# T\n\npara\n\n\tif x:\n\t\ty = 1\n'})
        self.assertEqual(p.returncode, 0, p.stdout)
        self.assertIn(b'if x:\n    y = 1\n', b''.join(views(data)))

    def test_text_is_compressed_past_64_kb(self):
        # The compressor's table of positions is shared by every topic, and
        # cleared only when they would pass 16 bits: 120 KB of topics, and
        # those after the first 64 KB compress like the rest.
        page = '# Doc\n\n' + ''.join('## Part %d\n\n%s' % (k, filler('part%d' % k, 20))
                                   for k in range(75))
        p, data, left = compile_pages({'index.md': page})
        self.assertEqual(p.returncode, 0, p.stdout)
        self.assertGreater(len(page), 120000)
        self.assertLess(len(data), len(page) // 3)

    def test_words_from_the_markup(self):
        # The words are read from the markup as its plain text would give
        # them: a code span's marks and a link's join what is either side
        # (foobar, ablnkok), and a line break parts them (one, two).
        p, data, left = compile_pages({'index.md': '# Tee\n\nfoo`bar` ab[lnk](#tee)ok one<br>two\n'})
        self.assertEqual(p.returncode, 0, p.stdout)
        self.assertEqual(ask(data, 'search', ['foobar', 'ablnkok', 'one', 'two', 'onetwo']).splitlines(),
                         ['1 0:0:1', '1 0:0:1', '1 0:0:1', '1 0:0:1', '0'])

    def test_runs_of_spaces_are_one(self):
        # In plain text, in a code span and across a line's end alike.
        p, data, left = compile_pages({'index.md': '# T\n\na   b `c   d`  e\n   f\n'})
        self.assertEqual(p.returncode, 0, p.stdout)
        self.assertIn(b'a b \x12c d\x13 e f', b''.join(views(data)))

    def test_inline_markup_straight_after_text(self):
        # Each thing inline Markdown starts with, right after plain text.
        p, data, left = compile_pages({'index.md': '# T\n\nx\\*y z<br>w v![pic](p.png) u _em_ s *st* '
                                                   'r`c` q\n'})
        self.assertEqual(p.returncode, 0, p.stdout)
        self.assertIn(b'x*y z\x0bw v[Image: pic] u \x10em\x11 s \x10st\x11 r\x12c\x13 q',
                      b''.join(views(data)))

    def test_table_columns_narrowed_as_before(self):
        # Columns too wide for the screen are narrowed as narrowing the
        # widest that can still give, a character at a time, left them --
        # the first of those tied -- though now found directly. These are
        # md5s of each table's view as that loop rendered it.
        tables = {
            'wide': ('| a | b | c |\n|---|---|---|\n| ' + ' | '.join(
                ['word ' * 30, 'x', 'longer words here ' * 10]) + ' |\n',
                '0b5341ca5053af303752c8cdb485bab2'),
            'ties': ('| a | b | c | d |\n|---|---|---|---|\n| ' + ' | '.join(['abcdefghij ' * 5] * 4)
                     + ' |\n', '224858946b07674a8b57f736699afc52'),
            'mins': ('| a | b | c |\n|---|---|---|\n| supercalifragilistic expialidocious | '
                     + 'short ' * 20 + ' | ' + 'antidisestablishmentarianism ' * 3 + ' |\n',
                     '7baf324e18a018f85eec09749077bf7a'),
            'many': ('| ' + ' | '.join('h%d' % c for c in range(12)) + ' |\n|' + '---|' * 12 + '\n| '
                     + ' | '.join(('w%d ' % c) * (c + 3) for c in range(12)) + ' |\n',
                     '63fdedb9bdc1e5003234409f9413031b'),
            'fits': ('| a | b |\n|---|---|\n| one | two |\n', 'ec69949bf2b6c53905a451a0e221324a'),
            'allmin': ('| a | b | c |\n|---|---|---|\n| ' + ' | '.join(
                ['pneumonoultramicroscopicsilicovolcanoconiosis'] * 3) + ' |\n',
                'd745c6ddba32f472b90add1de9db95e6'),
        }
        for name, (table, want) in tables.items():
            with self.subTest(table=name):
                p, data, left = compile_pages({'index.md': '# T\n\n' + table})
                self.assertEqual(p.returncode, 0, p.stdout)
                with tempfile.NamedTemporaryFile(suffix='.dat') as f:
                    f.write(data)
                    f.flush()
                    raw = subprocess.run([TOOL, f.name, 'views'], capture_output=True, check=True).stdout
                self.assertEqual(hashlib.md5(raw).hexdigest(), want)

    def test_a_wrapped_link_in_a_cell_to_a_later_page(self):
        # A link in a table's cell is written on every line its label wraps
        # to; each is a placeholder, and every one gets the topic once the
        # page it leads to is read. (A page with a menu between, so that
        # its own placeholders are settled while these still wait.)
        cell = 'word ' * 12 + '[a link whose label runs over more than one line of the cell](c.md#target)'
        pages = {
            'a.md': '# A\n\n| one | two |\n|---|---|\n| %s | %s |\n' % (cell, cell),
            'b.md': '# B\n\n' + filler('b', 200) + '## B1\n\ntext\n',
            'c.md': '# C\n\n## Target\n\ntext\n',
        }
        p, data, left = compile_pages(pages)
        self.assertEqual(p.returncode, 0, p.stdout)
        text = b''.join(views(data))
        self.assertGreaterEqual(text.count(b'\x14'), 4)        # at least two placeholders a cell
        self.assertNotIn(b'\x14\xff\xff', text)                # and none left leading nowhere

    def test_topics_past_a_block_and_moved_across_one(self):
        # Topics are kept 256 to a block. A page whose second level-1
        # heading makes the page its top moves its topics up one, here
        # across the first block's end.
        pages = {
            'a.md': '# A\n\n' + ''.join('## T%d\n\ntext\n\n' % k for k in range(248)),
            'b.md': '# B1\n\n' + ''.join('## S%d\n\ntext\n\n' % k for k in range(20)) + '# B2\n\ntext\n',
        }
        p, data, left = compile_pages(pages)
        self.assertEqual(p.returncode, 0, p.stdout)
        got = [line.strip().rsplit('  [', 1) for line in ask(data, 'contents').splitlines()]
        self.assertEqual([g[0] for g in got[249:]],
                         ['b', 'B1'] + ['S%d' % k for k in range(20)] + ['B2'])
        self.assertEqual([g[1] for g in got[249:]], ['%d]' % k for k in range(249, 272)])

    def test_many_links_to_a_page_read_later(self):
        # More placeholders than a block of patches holds, all waiting for
        # the page they lead to: each gets its topic when it is read.
        pages = {
            'a.md': '# A\n\n' + ''.join('- [x%d](b.md#t%d)\n' % (k, k % 3) for k in range(300)),
            'b.md': '# B\n\n## T0\n\nzero\n\n## T1\n\none\n\n## T2\n\ntwo\n',
        }
        p, data, left = compile_pages(pages)
        self.assertEqual(p.returncode, 0, p.stdout)
        text = b''.join(views(data))
        self.assertEqual(text.count(b'\x14'), 300)
        self.assertNotIn(b'\x14\xff\xff', text)

    def test_keys_on_a_page_given_a_top_later(self):
        # Keys are written as each topic is made, by its number as made, and
        # read back by its number at the end: here b's top is made after
        # frob, and comes before it.
        p, data, left = compile_pages({'a.md': '# A\n\ntext\n',
                                       'b.md': '# B1\n\ntext\n\n## `frob`\n\ntext\n\n# B2\n\ntext\n'})
        self.assertEqual(p.returncode, 0, p.stdout)
        self.assertEqual(ask(data, 'lookup', ['frob']).split(' |')[0], '1 1 3')

    def test_links_waiting_on_the_card_then_more(self):
        # The links waiting for a page are records in a scratch file, read
        # back when it is read: b's here, from among d's, more than the
        # file's buffer holds; and then c adds more for d, after that.
        pages = {
            'a.md': '# A\n\n' + ''.join('- [b%d](b.md#bt%d) [d%d](d.md#dt%d)\n' % (k, k % 2, k, k % 3)
                                      for k in range(100)),
            'b.md': '# B\n\n## BT0\n\nzero\n\n## BT1\n\none\n',
            'c.md': '# C\n\n' + ''.join('- [c%d](d.md#dt%d)\n' % (k, k % 3) for k in range(100)),
            'd.md': '# D\n\n## DT0\n\nzero\n\n## DT1\n\none\n\n## DT2\n\ntwo\n',
        }
        p, data, left = compile_pages(pages)
        self.assertEqual(p.returncode, 0, p.stdout)
        self.assertEqual(left, [])
        text = b''.join(views(data))
        self.assertEqual(text.count(b'\x14'), 300)
        self.assertNotIn(b'\x14\xff\xff', text)

    def test_words_past_the_dictionary(self):
        # More different words than the dictionary has room for: those found
        # once it is full are kept as words, sorted on the card, and woven in
        # among the dictionary's as the words are written -- in order, so a
        # search finds the first, a middle and the last.
        page = '# Many\n\n' + ''.join('## T%d\n\n%s\n\n' % (k, ' '.join(
            'z%sq' % name for name in ['%05d' % (k * 100 + j) for j in range(100)])) for k in range(60))
        p, data, left = compile_pages({'index.md': page})
        self.assertEqual(p.returncode, 0, p.stdout)
        self.assertIn('words %d\n' % (6000 + 1 + 50 + 1), ask(data, 'info'))  # many, t10-t59, section
        got = ask(data, 'search', ['z00000q', 'z03017q', 'z05999q']).splitlines()
        self.assertEqual([g.split()[1].split(':')[0] for g in got], ['1', '31', '60'])

    def test_postings_past_one_gather(self):
        # More postings than one pass gathers: the same 90 words in 400
        # topics, gathered a range of words a pass, each word every topic.
        common = ' '.join('w%sz' % chr(ord('a') + j % 26) * (1 + j // 26) for j in range(90))
        page = '# Top\n\n' + ''.join('## Part %d\n\n%s\n\n' % (k, common) for k in range(400))
        p, data, left = compile_pages({'index.md': page})
        self.assertEqual(p.returncode, 0, p.stdout)
        for word in ('waz', 'wlzwlz', 'wyzwyzwyz', 'wzz'):     # the last sort into a later pass
            got = ask(data, 'search', [word]).splitlines()
            self.assertEqual(got[0].split()[0], '400', word)

    def test_a_title_word_said_in_the_menu_too(self):
        # A topic too big to show its children has a menu of them, whose
        # words are its own too: "things" is in its title, and in its menu
        # without the title's bit. Kept once, it keeps the bit.
        page = '# Alpha Things\n\n' + filler('alpha', 200) + ''.join('## Things %d\n\ntext\n\n' % k
                                                                  for k in range(5))
        p, data, left = compile_pages({'index.md': page})
        self.assertEqual(p.returncode, 0, p.stdout)
        self.assertEqual(ask(data, 'search', ['things']).split()[:2], ['6', '0:1:1'])

    def test_words_past_one_run(self):
        # More words than one 16 KB run of the words file holds: sorted in
        # runs, merged, every word still finds its topic.
        page = '# Words\n\n' + ''.join(
            '## T%d\n\n%s\n\n' % (k, ' '.join('w%dq%s' % (k, c) for c in 'abcdefghijklmnop'))
            for k in range(250))
        p, data, left = compile_pages({'index.md': page})
        self.assertEqual(p.returncode, 0, p.stdout)
        # 16 for each, its title (T10 on: three letters), Words, and
        # section: Words is too big to show its children, so links to them.
        self.assertIn('words %d\n' % (250 * 16 + 240 + 2), ask(data, 'info'))
        got = ask(data, 'search', ['w0qa', 'w123qh', 'w249qp']).splitlines()
        self.assertEqual([g.split()[1].split(':')[0] for g in got], ['1', '124', '250'])

    def test_a_block_longer_than_the_window_can_grow_to(self):
        # One code block of 40 KB: past the 32 KB the window may grow to.
        page = '# Big code\n\n```\n' + 'x = 1\n' * 7000 + '```\n'
        p, data, left = compile_pages({'index.md': page})
        self.assertEqual(p.returncode, FAILED)
        self.assertRegex(p.stdout, r'index\.md:3: error: a block of more than 32768 bytes')
        self.assertIsNone(data)
        self.assertEqual(left, [])

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
