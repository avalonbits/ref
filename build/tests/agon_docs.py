"""agon-docs at the commit in SOURCE, fetched once into build/cache."""

import os

from refbuild import book, rules, source

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROOT = os.path.dirname(HERE)

_book = None


def docs():
    src = source.read_source(os.path.join(ROOT, 'SOURCE'))

    return src, source.fetch(src, os.path.join(HERE, 'cache'))


def load_rules():
    return rules.load(os.path.join(HERE, 'rules.toml'))


def the_book():
    global _book
    if _book is None:
        _, d = docs()
        _book = book.build(d, load_rules().link_fixes)

    return _book
