"""Normalising keys and lookups, so every way of writing a thing meets.

The reader core on the Agon does exactly this to what the user types, with
the same settings, which ref.dat carries; tests/normalize.json holds cases
that both implementations must agree on. Change one, change the other.

    normalize(text, cfg) -> str

1.  Lower case (ASCII only: the docs' keys are ASCII).
2.  Split into tokens at spaces, tabs and commas. A semicolon also ends a
    token, and marks it *wide*: in a VDU sequence `n;` is a 16-bit value.
3.  From each token, drop an inner placeholder -- `<...>` after the first
    character, as in `hotkey$<n>` -- then strip the characters
    ( ) [ ] { } < > " ' ` : * . ! ? | from both ends. Tokens left empty go.
4.  Numbers are written in decimal: `12`, `&C`, `0xC`, `0Ch` (a digit
    first, so `ah` stays a word) and MOS 3's `16_C` (a base 2-36, then
    digits in it). After a word in cfg.hex_after (rst), bare digits are hex,
    and the number is written in hex, lower case: `rst 10h`, `rst &10` and
    `rst 10` are all `rst 10`, so normalising a key gives the key back. A
    number above 0xFFFFFF stays as it was written.
5.  A query that starts with a number is a VDU sequence: `vdu` goes first.
6.  Skips: where the tokens start with a skip's prefix (the longest such),
    each of the skip's units after it becomes `_`, whatever was written
    there -- the buffer ID of a buffered command, the channel of an audio
    command. A unit of width 2 takes two tokens when they are both numbers
    and the first is not wide (`1 0` as bytes), else one (`1;`, or a name).
7.  Trailing `_` tokens go: a placeholder at the end adds nothing.
8.  The tokens, joined with single spaces.
"""

import re
from dataclasses import dataclass, field

STRIP = '()[]{}<>"\'`:*.!?|'
SEPARATORS = ' \t,'
MAX_NUMBER = 0xFFFFFF
_ASCII_LOWER = {c: c + 32 for c in range(65, 91)}


@dataclass
class Skip:
    prefix: list            # normalised tokens
    units: list             # widths, 1 or 2


@dataclass
class Config:
    skips: list = field(default_factory=list)
    hex_after: list = field(default_factory=list)


def tokens(text):
    """(token, wide) pairs: steps 1-3."""
    out = []
    cur = []

    def end(wide):
        if cur:
            out.append((''.join(cur), wide))
            cur.clear()
        elif wide and out:
            # `n ;` -- a semicolon after a space still marks the number.
            out[-1] = (out[-1][0], True)

    for ch in text.translate(_ASCII_LOWER):
        if ch in SEPARATORS:
            end(False)
        elif ch == ';':
            end(True)
        else:
            cur.append(ch)
    end(False)

    cleaned = []
    for tok, wide in out:
        tok = _drop_placeholder(tok)
        tok = tok.strip(STRIP)
        if tok:
            cleaned.append((tok, wide))

    return cleaned


def _drop_placeholder(tok):
    k = tok.find('<', 1)
    while k > 0:
        e = tok.find('>', k)
        if e < 0:
            break
        tok = tok[:k] + tok[e + 1:]
        k = tok.find('<', 1)

    return tok


def number(tok, hex_context=False):
    """The value of a number token, or None."""
    v = None
    if re.fullmatch(r'[0-9]+', tok):
        v = int(tok, 16 if hex_context else 10)
    elif hex_context and re.fullmatch(r'[0-9a-f]+', tok):
        v = int(tok, 16)
    elif re.fullmatch(r'&[0-9a-f]+', tok):
        v = int(tok[1:], 16)
    elif re.fullmatch(r'0x[0-9a-f]+', tok):
        v = int(tok[2:], 16)
    elif re.fullmatch(r'[0-9][0-9a-f]*h', tok):
        v = int(tok[:-1], 16)
    else:
        m = re.fullmatch(r'([0-9]+)_([0-9a-z]+)', tok)
        if m and 2 <= int(m.group(1)) <= 36:
            base = int(m.group(1))
            digits = '0123456789abcdefghijklmnopqrstuvwxyz'[:base]
            if all(c in digits for c in m.group(2)):
                v = int(m.group(2), base)
    if v is not None and v > MAX_NUMBER:
        return None

    return v


def normalize_tokens(text, cfg):
    """Steps 1-7, as (token, is_number) pairs."""
    raw = tokens(text)
    out = []        # (text, is_number, wide)
    for k, (tok, wide) in enumerate(raw):
        hex_context = k > 0 and raw[k - 1][0] in cfg.hex_after
        v = number(tok, hex_context)
        if v is None:
            out.append((tok, False, wide))
        else:
            out.append((format(v, 'x') if hex_context else str(v), True, wide))

    if out and out[0][1]:
        out.insert(0, ('vdu', False, False))

    best = None
    for sk in cfg.skips:
        n = len(sk.prefix)
        if [t for t, _, _ in out[:n]] == sk.prefix and (best is None or n > len(best.prefix)):
            best = sk
    if best:
        k = len(best.prefix)
        res = out[:k]
        for width in best.units:
            if k >= len(out):
                break
            if width == 2 and out[k][1] and not out[k][2] \
                    and k + 1 < len(out) and out[k + 1][1]:
                k += 2
            else:
                k += 1
            res.append(('_', False, False))
        out = res + out[k:]

    while out and out[-1][0] == '_':
        out.pop()

    return [(t, isnum) for t, isnum, _ in out]


def normalize(text, cfg):
    return ' '.join(t for t, _ in normalize_tokens(text, cfg))


def vdu_key(text, cfg):
    """The key a VDU sequence in the docs gives: normalised, then cut at its
    first named parameter -- `VDU 23, 27, &21, bitmapId; 0;` is `vdu 23 27 33`.
    None when it is not a VDU sequence."""
    toks = normalize_tokens(text, cfg)
    if not toks or toks[0][0] != 'vdu' or len(toks) < 2 or not toks[1][1]:
        return None
    out = ['vdu']
    for t, isnum in toks[1:]:
        if not isnum and t != '_':
            break
        out.append(t)
    while out[-1] == '_':
        out.pop()

    return ' '.join(out)
