"""build/rules.toml: how the builder reads agon-docs."""

import tomllib
from dataclasses import dataclass, field

from .normalize import Config, Skip, normalize


@dataclass
class PageRules:
    vdu: bool = False
    heading_code: bool = False
    plot: bool = False
    prose: bool = False
    heading: list = field(default_factory=list)     # {match, keys}
    code: list = field(default_factory=list)        # {match}
    body: list = field(default_factory=list)        # {match}


@dataclass
class Rules:
    link_fixes: dict = field(default_factory=dict)  # (page, href) -> href
    normalize: Config = field(default_factory=Config)
    pages: dict = field(default_factory=dict)       # page -> PageRules
    keys: list = field(default_factory=list)        # {page, anchor?, keys}


def load(path):
    with open(path, 'rb') as f:
        data = tomllib.load(f)

    return parse(data)


def _need(table, what, *names):
    for k in names:
        if k not in table:
            raise ValueError(f'{what} needs {k}: {table}')


def parse(data):
    r = Rules()
    for fix in data.get('link_fix', []):
        _need(fix, 'link_fix', 'page', 'href', 'to', 'why')
        r.link_fixes[(fix['page'], fix['href'])] = fix['to']

    norm = data.get('normalize', {})
    r.normalize.hex_after = [w.lower() for w in norm.get('hex_after', [])]
    for sk in data.get('skip', []):
        _need(sk, 'skip', 'prefix', 'units', 'why')
        if any(u not in (1, 2) for u in sk['units']):
            raise ValueError(f'skip units are 1 or 2: {sk}')
        # The prefix is normalised before any skip exists, so a skip cannot
        # rewrite its own prefix.
        prefix = normalize(sk['prefix'], Config(hex_after=r.normalize.hex_after)).split(' ')
        r.normalize.skips.append(Skip(prefix, list(sk['units'])))

    known = {'file', 'vdu', 'heading_code', 'plot', 'prose', 'heading', 'code', 'body', 'why'}
    for pg in data.get('page', []):
        _need(pg, 'page', 'file')
        extra = set(pg) - known
        if extra:
            raise ValueError(f'page {pg["file"]}: unknown {sorted(extra)}')
        pr = PageRules(pg.get('vdu', False), pg.get('heading_code', False),
                       pg.get('plot', False), pg.get('prose', False))
        for h in pg.get('heading', []):
            _need(h, 'page.heading', 'match', 'keys')
            pr.heading.append(h)
        for c in pg.get('code', []):
            _need(c, 'page.code', 'match')
            pr.code.append(c)
        for b in pg.get('body', []):
            _need(b, 'page.body', 'match')
            pr.body.append(b)
        if pg['file'] in r.pages:
            raise ValueError(f'page {pg["file"]} has two sets of rules')
        r.pages[pg['file']] = pr

    for k in data.get('key', []):
        _need(k, 'key', 'page', 'keys', 'why')
        r.keys.append(k)

    return r
