"""What the builder made, for a person to read."""

import os


def gap_line(g):
    page, _, title = g

    return f'{page}: {title}'


def read_gaps(path):
    """The gaps already looked at and left: one `page: title` a line."""
    if not os.path.exists(path):
        return set()
    with open(path) as f:
        return {line.rstrip('\n') for line in f if line.strip() and not line.startswith('#')}


def write_gaps(path, gaps):
    with open(path, 'w') as f:
        f.write('# Headings no rule gives a key: prose sections of structured pages,\n'
                '# looked at and left. The builder lists any gap not in here.\n')
        for g in gaps:
            f.write(gap_line(g) + '\n')


def new_gaps(ix, known):
    return [g for g in ix.gaps if gap_line(g) not in known]


def summary(b, src, ix=None, known=frozenset()):
    pages = len(b.pages)
    out = [
        f'agon-docs {src.commit[:7]} ({src.date})',
        f'pages      {pages}',
        f'topics     {len(b.topics)}',
        f'links      {b.internal} resolved, {b.external} external',
        f'images     {len(b.images)} skipped with a note',
    ]
    if ix is not None:
        new = new_gaps(ix, known)
        out.append(f'keys       {len(ix.keys)}, {len({k.key for k in ix.keys})} distinct')
        out.append(f'gaps       {len(ix.gaps)} headings with no key, {len(new)} new')
        for g in new:
            out.append(f'  new gap  {g[0]}:{g[1]}: {g[2]}')

    return '\n'.join(out)


def report(b, src, ix=None, known=frozenset()):
    out = [summary(b, src, ix, known), '', '# images skipped']
    for page, line, srcpath in b.images:
        out.append(f'{page}:{line}: {srcpath}')
    if ix is not None:
        out += ['', '# gaps: headings no rule gave a key']
        for page, line, title in ix.gaps:
            out.append(f'{page}:{line}: {title}')
    out += ['', '# contents']

    def walk(nodes, depth):
        for n in nodes:
            t = '' if n.topic is None else f'  [{n.topic}]'
            out.append('  ' * depth + n.title + t)
            walk(n.children, depth + 1)

    walk(b.contents, 0)
    if ix is not None:
        out += ['', '# keys']
        for k in ix.keys:
            out.append(f'{k.key}  ->  [{k.topic}] {b.topics[k.topic].title}')

    return '\n'.join(out) + '\n'
