"""What the builder made, for a person to read."""


def summary(b, src):
    pages = len(b.pages)

    return '\n'.join([
        f'agon-docs {src.commit[:7]} ({src.date})',
        f'pages      {pages}',
        f'topics     {len(b.topics)}',
        f'links      {b.internal} resolved, {b.external} external',
        f'images     {len(b.images)} skipped with a note',
    ])


def report(b, src):
    out = [summary(b, src), '', '# images skipped']
    for page, line, srcpath in b.images:
        out.append(f'{page}:{line}: {srcpath}')
    out += ['', '# contents']

    def walk(nodes, depth):
        for n in nodes:
            t = '' if n.topic is None else f'  [{n.topic}]'
            out.append('  ' * depth + n.title + t)
            walk(n.children, depth + 1)

    walk(b.contents, 0)

    return '\n'.join(out) + '\n'
