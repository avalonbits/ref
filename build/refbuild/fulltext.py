"""The full-text index, and the search the viewer runs over it.

Every word of three letters or more in every topic, lower case, the
commonest English words and plain numbers left out, each with the topics it
appears in, and whether it appears in the topic's title.

A search splits what was typed into words the same way (two letters are
enough to start matching; the first eight count), finds every indexed word that starts with each,
and ranks the topics: first by how many of the query's words are in the
topic's title, then by how many of them the topic has, then in the book's
order. The reader core ranks exactly so; tests hold the two together.
"""

import re

from .keys import STOP_WORDS

MIN_WORD = 3
MIN_QUERY = 2
MAX_QUERY = 8           # query words read; the rest are ignored
WORD = re.compile(rb'[a-z0-9_$]+')


def ascii_lower(b):
    return bytes(c + 32 if 65 <= c <= 90 else c for c in b)


def words(text):
    """The index's words in text (bytes, the Agon's character set)."""
    out = []
    for w in WORD.findall(ascii_lower(text)):
        if len(w) >= MIN_WORD and not w.isdigit() and w.decode() not in STOP_WORDS \
                and w.strip(b'_$'):
            out.append(w)

    return out


def query_words(query):
    if isinstance(query, str):
        query = query.encode('latin-1', 'replace')
    out = []
    for w in WORD.findall(ascii_lower(query)):
        if len(out) == MAX_QUERY:
            break
        if len(w) >= MIN_QUERY and w.decode() not in STOP_WORDS and w not in out:
            out.append(w)

    return out


def build(texts, titles):
    """{word: [(topic, in_title)]} from each topic's plain text and title."""
    index = {}
    for num, text in enumerate(texts):
        tw = set(words(titles[num]))
        for w in set(words(text)) | tw:
            index.setdefault(w, []).append((num, w in tw))
    for w in index:
        index[w].sort()

    return index


def search(index, query, sorted_words=None):
    """Topics ranked for query: [(topic, in_title_count, matched_count)]."""
    qs = query_words(query)
    if not qs:
        return []
    ws = sorted_words if sorted_words is not None else sorted(index)
    title = {}
    count = {}
    for q in qs:
        hit = set()
        in_title = set()
        for w in ws:
            if w.startswith(q):
                for t, ti in index[w]:
                    hit.add(t)
                    if ti:
                        in_title.add(t)
        for t in hit:
            count[t] = count.get(t, 0) + 1
        for t in in_title:
            title[t] = title.get(t, 0) + 1
    ranked = sorted(count, key=lambda t: (-title.get(t, 0), -count[t], t))

    return [(t, title.get(t, 0), count[t]) for t in ranked]
