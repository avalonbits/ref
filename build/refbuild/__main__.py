"""python3 -m refbuild: fetch agon-docs at the pinned commit and compile it.

    python3 -m refbuild [--docs DIR] [--report FILE]

--docs reads an agon-docs checkout instead of fetching the commit in SOURCE.
"""

import argparse
import os
import sys

from . import book, keys, report, rules, source

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))     # build/
ROOT = os.path.dirname(HERE)


def main(argv=None):
    ap = argparse.ArgumentParser(prog='refbuild')
    ap.add_argument('--docs', help='an agon-docs checkout, instead of fetching')
    ap.add_argument('--source', default=os.path.join(ROOT, 'SOURCE'))
    ap.add_argument('--rules', default=os.path.join(HERE, 'rules.toml'))
    ap.add_argument('--cache', default=os.path.join(HERE, 'cache'))
    ap.add_argument('--report', help='write the report here as well as a summary to stdout')
    ap.add_argument('--gaps', default=os.path.join(HERE, 'gaps.txt'),
                    help='the gaps already known; new ones are listed in the summary')
    ap.add_argument('--write-gaps', action='store_true',
                    help='record the current gaps as known')
    args = ap.parse_args(argv)

    src = source.read_source(args.source)
    docs = args.docs or source.fetch(src, args.cache)
    rl = rules.load(args.rules)
    try:
        b = book.build(docs, rl.link_fixes)
    except book.BookError as e:
        for err in e.errors:
            print(err, file=sys.stderr)
        print(f'{len(e.errors)} link(s) could not be resolved', file=sys.stderr)

        return 1

    try:
        ix = keys.build(b, rl)
    except ValueError as e:
        print(e, file=sys.stderr)

        return 1
    if ix.unused_rules:
        for u in ix.unused_rules:
            print(u, file=sys.stderr)
        print('rules that apply to nothing: fix build/rules.toml', file=sys.stderr)

        return 1

    if args.write_gaps:
        report.write_gaps(args.gaps, ix.gaps)
    known = report.read_gaps(args.gaps)
    text = report.report(b, src, ix, known)
    if args.report:
        with open(args.report, 'w') as f:
            f.write(text)
    print(report.summary(b, src, ix, known))

    return 0


if __name__ == '__main__':
    sys.exit(main())
