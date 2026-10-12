#!/usr/bin/env python3
"""Markdown that makes md2ref's every per-item step pay, for each step that
was once worse than n log n: written to <dir>/<case>/, one folder a case.

    test/stress.py <dir> [scale]

Each case is ordinary Markdown, only more of one thing than a page has --
anchors, links, titles, topics in one run, words sharing letters, a word
in every topic, keys, wide tables, unmatched brackets, code spans, text
that repeats around links -- so that a step costing n squared shows.
test/stress.sh times each on the Agon against a budget of cycles a byte;
test/growth.sh counts each at two sizes on the PC and requires the work to
grow as the input does.
"""
import os
import sys


def lcg(seed):
    v = seed
    while True:
        v = (v * 1103515245 + 12345) % 2 ** 31
        yield v


def name(k):
    """A word of letters for k: distinct, and not a stop word."""
    s = ''
    k += 1
    while k:
        k, r = divmod(k - 1, 26)
        s = chr(ord('a') + r) + s
    return 'q' + s


def cases(scale):
    n = int(600 * scale)
    out = {}

    # Anchors and links: many headings, every one linked to from a page
    # read before, and from its own page.
    links = ''.join(('\n## Part %d\n\n' % k if k % 100 == 0 else '')
                    + '- [%s](b.md#%s)\n' % (name(k), name(k)) for k in range(n))
    out['anchors'] = {
        'a.md': '# Index\n' + links,
        'b.md': '# Book\n\n' + ''.join('## %s\n\nSee [back](#%s).\n\n' % (name(k), name(k))
                                       for k in range(n)),
    }

    # One topic over many: its span, and the menu of a topic too big.
    out['span'] = {'index.md': '# Top\n\n' + ''.join('## %s\n\n%s text.\n\n' % (name(k), name(k))
                                                     for k in range(n))}

    # Topics that each start with the same three letters, in topics of
    # their own: the run sort's inserting among those that share them.
    out['prefix'] = {'index.md': '# Top\n\n' + ''.join('## T%d\n\n%s\n\n' % (
        k, ' '.join('abc%s' % name(k * 8 + j) for j in range(8))) for k in range(n))}

    # One word in every topic, among enough others that the words file is
    # many runs: its list of topics, gathered from all of them.
    out['common'] = {'index.md': '# Top\n\n' + ''.join('## %s\n\nshared %s\n\n' % (
        name(k), ' '.join(name(k * 30 + j) for j in range(30))) for k in range(n))}

    # Many keys: headings of code spans, each span a key.
    out['keys'] = {'index.md': '# Top\n\n' + ''.join(
        '## H%d\n\n%s\n\ntext\n\n' % (k, '') if False else
        '## %s\n\ntext\n\n' % ' '.join('`k%s`' % name(k * 60 + j) for j in range(60))
        for k in range(max(1, n // 10)))}

    # A table wider than the screen, of long cells.
    cols = 40
    head = '| ' + ' | '.join('C%d' % c for c in range(cols)) + ' |\n|' + '---|' * cols + '\n'
    out['table'] = {'index.md': '# Top\n' + ''.join(
        '\n## Part %d\n\n' % t + head + ''.join(
            '| ' + ' | '.join(name((t * 4 + r) * cols + c) * 12 for c in range(cols)) + ' |\n'
            for r in range(4)) for t in range(max(1, n // 100)))}

    # Brackets, stars and backticks that never close, in one paragraph.
    r = lcg(1)
    junk = []
    for k in range(n * 2):
        junk.append(['[', '*', '`', '_'][next(r) % 4] + name(k))
    out['unclosed'] = {'index.md': '# Top\n\n' + ' '.join(junk) + '\n'}

    # Headings with many code spans: keys, each span.
    # (Four headings, each with more spans the more input: a step that goes
    # over a heading's spans once for each of them grows as their square.)
    out['spans'] = {'index.md': '# Top\n\n' + ''.join(
        '## %s\n\ntext\n\n' % ' '.join('`%s`' % name(k * n + j) for j in range(n // 4))
        for k in range(4))}

    # Headings one after another with no blank line between: one run of
    # many topics.
    out['run'] = {'index.md': '# Top\n' + ''.join('## %s\n%s\n' % (name(k), name(k)) for k in range(n))}

    # The same line again and again in one topic, each with a link to a
    # page read later: placeholders all through text that repeats.
    out['repeats'] = {
        'a.md': '# A\n\n' + '- [x](b.md)\n' * (n // 2),
        'b.md': '# B\n\ntext\n',
    }

    return out


def main():
    root = sys.argv[1]
    scale = float(sys.argv[2]) if len(sys.argv) > 2 else 1
    for case, pages in cases(scale).items():
        d = os.path.join(root, case)
        os.makedirs(d, exist_ok=True)
        for page, text in pages.items():
            with open(os.path.join(d, page), 'w') as f:
                f.write(text)


if __name__ == '__main__':
    main()
