"""LZ4's block format, with an optional preset dictionary.

A block is a run of sequences: a token byte (literal count in its high
nibble, match length - 4 in its low), more literal-count bytes while 255,
the literals, a 2-byte little-endian match offset, more match-length bytes
while 255. The last sequence has literals only. The format is LZ4's own,
chosen because the eZ80 decodes it with byte copies and no bit fiddling.

A match may reach back past the start of the output into the dictionary,
which sits, as far as offsets go, just before it. ref.dat stores one
dictionary for every topic, so each topic still decompresses on its own.
"""

MIN_MATCH = 4
MAX_OFFSET = 0xFFFF
HASH_BITS = 16


def _hash(b, k):
    v = b[k] | b[k + 1] << 8 | b[k + 2] << 16 | b[k + 3] << 24

    return (v * 2654435761 & 0xFFFFFFFF) >> (32 - HASH_BITS)


def compress(data, dictionary=b'', depth=64):
    """data as an LZ4 block, matches allowed into dictionary."""
    buf = dictionary + data
    base = len(dictionary)
    n = len(buf)
    head = {}
    chain = [0] * n

    def insert(k):
        if k + MIN_MATCH <= n:
            h = _hash(buf, k)
            chain[k] = head.get(h, -1)
            head[h] = k

    for k in range(max(0, base - MAX_OFFSET), base):
        insert(k)

    def best(k):
        if k + MIN_MATCH > n:
            return 0, 0
        h = _hash(buf, k)
        cand = head.get(h, -1)
        blen = boff = 0
        tries = depth
        limit = n - k
        while cand >= 0 and k - cand <= MAX_OFFSET and tries:
            tries -= 1
            if buf[cand + blen] == buf[k + blen]:     # blen < limit: a full match breaks
                m = 0
                while m < limit and buf[cand + m] == buf[k + m]:
                    m += 1
                if m > blen:
                    blen, boff = m, k - cand
                    if m == limit:
                        break
            cand = chain[cand]

        return (blen, boff) if blen >= MIN_MATCH else (0, 0)

    out = bytearray()
    lit = base
    k = base
    while k < n:
        mlen, moff = best(k)
        if mlen:
            # One step of lazy matching: a longer match one byte on wins.
            nlen, noff = best(k + 1) if k + 1 < n else (0, 0)
            if nlen > mlen + 1:
                insert(k)
                k += 1
                mlen, moff = nlen, noff
        if not mlen:
            insert(k)
            k += 1
            continue
        _sequence(out, buf[lit:k], mlen, moff)
        for j in range(k, k + mlen):
            insert(j)
        k += mlen
        lit = k
    _sequence(out, buf[lit:n], 0, 0)

    return bytes(out)


def _sequence(out, literals, mlen, moff):
    nl = len(literals)
    ml = mlen - MIN_MATCH if mlen else 0
    out.append((min(nl, 15) << 4) | min(ml, 15))
    if nl >= 15:
        _length(out, nl - 15)
    out += literals
    if mlen:
        out += bytes([moff & 0xFF, moff >> 8])
        if ml >= 15:
            _length(out, ml - 15)


def _length(out, n):
    while n >= 255:
        out.append(255)
        n -= 255
    out.append(n)


def decompress(block, dictionary=b''):
    out = bytearray(dictionary)
    base = len(dictionary)
    k = 0
    n = len(block)
    while k < n:
        tok = block[k]
        k += 1
        nl = tok >> 4
        if nl == 15:
            while True:
                b = block[k]
                k += 1
                nl += b
                if b != 255:
                    break
        out += block[k:k + nl]
        k += nl
        if k >= n:
            break
        off = block[k] | block[k + 1] << 8
        k += 2
        ml = tok & 15
        if ml == 15:
            while True:
                b = block[k]
                k += 1
                ml += b
                if b != 255:
                    break
        ml += MIN_MATCH
        src = len(out) - off
        if off == 0 or src < 0:
            raise ValueError('bad match offset')
        for j in range(ml):
            out.append(out[src + j])

    return bytes(out[base:])
