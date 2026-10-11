"""Where agon-docs comes from: the commit pinned in SOURCE, and mkdocs.yml's nav."""

import io
import os
import re
import tarfile
import urllib.request
from dataclasses import dataclass


@dataclass
class Source:
    repo: str
    commit: str
    date: str


def read_source(path):
    fields = {}
    with open(path) as f:
        for line in f:
            line = line.strip()
            if line and not line.startswith('#'):
                k, _, v = line.partition(' ')
                fields[k] = v.strip()
    src = Source(fields['repo'], fields['commit'], fields['date'])
    if not re.fullmatch(r'[0-9a-f]{40}', src.commit):
        raise ValueError(f'{path}: commit must be a full 40-digit hash')
    if not re.fullmatch(r'\d{4}-\d\d-\d\d', src.date):
        raise ValueError(f'{path}: date must be YYYY-MM-DD')

    return src


def fetch(src, cache):
    """agon-docs at the pinned commit, unpacked under cache; its directory.

    The only network access the builder makes, and only once: an unpacked
    commit is kept and found again.
    """
    dest = os.path.join(cache, 'agon-docs-' + src.commit)
    if os.path.isfile(os.path.join(dest, 'mkdocs.yml')):
        return dest
    m = re.fullmatch(r'https://github\.com/([^/]+/[^/]+?)(?:\.git)?/?', src.repo)
    if not m:
        raise ValueError(f'only GitHub repositories can be fetched: {src.repo}')
    url = f'https://codeload.github.com/{m.group(1)}/tar.gz/{src.commit}'
    with urllib.request.urlopen(url, timeout=60) as r:
        data = r.read()
    tmp = dest + '.part'
    os.makedirs(tmp, exist_ok=True)
    with tarfile.open(fileobj=io.BytesIO(data), mode='r:gz') as t:
        top = t.getmembers()[0].name.split('/')[0]
        if not top.endswith(src.commit):
            raise ValueError(f'the archive is not of {src.commit}: {top}')
        t.extractall(tmp, filter='data')
    os.rename(os.path.join(tmp, top), dest)
    os.rmdir(tmp)

    return dest


# --- mkdocs.yml's nav ---------------------------------------------------------------
#
# mkdocs.yml carries !!python/name tags that a YAML loader refuses without
# running code, and the builder needs nothing else from it, so the nav is
# read on its own: a tree of '- Title: file.md' and '- Title:' lines.


@dataclass
class NavNode:
    title: str
    file: str | None
    children: list


def read_nav(text):
    lines = text.split('\n')
    start = None
    for k, s in enumerate(lines):
        if re.match(r'^nav\s*:\s*$', s):
            start = k + 1
            break
    if start is None:
        raise ValueError('mkdocs.yml has no nav')
    entries = []
    for s in lines[start:]:
        if not s.strip() or s.lstrip().startswith('#'):
            continue
        if not s.startswith((' ', '-')):
            break                               # the next top-level key
        m = re.match(r'^(\s*)-\s+(.*)$', s)
        if not m:
            raise ValueError(f'nav: cannot read {s!r}')
        indent = len(m.group(1))
        item = m.group(2).strip()
        m2 = re.match(r'''^(?:'([^']*)'|"([^"]*)"|([^:]+?))\s*:\s*(.*)$''', item)
        if m2:
            title = m2.group(1) or m2.group(2) or m2.group(3)
            file = _unquote(m2.group(4).strip()) or None
        else:
            title, file = None, _unquote(item)
        entries.append((indent, title, file))

    root = NavNode('', None, [])
    stack = [(-1, root)]
    for indent, title, file in entries:
        while stack[-1][0] >= indent:
            stack.pop()
        node = NavNode(title or '', file, [])
        stack[-1][1].children.append(node)
        stack.append((indent, node))

    return root.children


def _unquote(s):
    if len(s) >= 2 and s[0] == s[-1] and s[0] in '\'"':
        return s[1:-1]

    return s
