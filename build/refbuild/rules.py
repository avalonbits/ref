"""build/rules.toml: how the builder reads agon-docs."""

import tomllib
from dataclasses import dataclass, field


@dataclass
class Rules:
    link_fixes: dict = field(default_factory=dict)  # (page, href) -> href


def load(path):
    with open(path, 'rb') as f:
        data = tomllib.load(f)

    return parse(data)


def parse(data):
    r = Rules()
    for fix in data.get('link_fix', []):
        for k in ('page', 'href', 'to', 'why'):
            if k not in fix:
                raise ValueError(f'link_fix needs {k}: {fix}')
        r.link_fixes[(fix['page'], fix['href'])] = fix['to']

    return r
