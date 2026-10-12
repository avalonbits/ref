/*
 * The document written: header, contents, topics, titles, text, keys,
 * words, settings and an empty dictionary, in that order (docs/FORMAT.md).
 *
 * Topics are numbered in preorder, page by page, so contents node k is
 * topic k: the pages' top topics are the top level, each topic's children
 * under it.
 */
#include <stdio.h>
#include <string.h>

#include "md2ref.h"

#define HEAD_SIZE  128
#define NONE    0xFFFF
#define VERSION 1

static FILE* out;
static unsigned long written;
static int failed;

static void bytes(const void* p, unsigned n)
{
    if (n && fwrite(p, 1, n, out) != n) {
        failed = 1;
    }
    written += n;
}

static void u8(unsigned v)
{
    uint8_t b = (uint8_t)v;

    bytes(&b, 1);
}

static void u16(unsigned v)
{
    u8(v & 0xFF);
    u8(v >> 8);
}

static void u32(unsigned long v)
{
    u16((unsigned)(v & 0xFFFF));
    u16((unsigned)(v >> 16));
}

static void put32(uint8_t* p, unsigned long v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void put16(uint8_t* p, unsigned v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

/* --- titles: each once ----------------------------------------------------------- */

static uint8_t* pool;
static unsigned pool_len;
static unsigned* title_at;      /* each topic's title's offset */
static unsigned book_title_at;

static unsigned add_title(const char* s)
{
    unsigned n = (unsigned)strlen(s);
    unsigned k = 0;

    if (n > 255) {
        n = 255;
    }
    while (k < pool_len) {
        if (pool[k] == n && memcmp(pool + k + 1, s, n) == 0) {
            return k;
        }
        k += 1 + pool[k];
    }
    pool[pool_len] = (uint8_t)n;
    memcpy(pool + pool_len + 1, s, n);
    pool_len += 1 + n;

    return k;
}

static void make_titles(void)
{
    unsigned size = 256 + (unsigned)strlen(book_title);
    int t;

    for (t = 0; t < ntopics; t++) {
        size += 1 + (unsigned)strlen(topics[t].title);
    }
    pool = arena_alloc(size);
    title_at = arena_alloc((unsigned)ntopics * sizeof(unsigned));
    for (t = 0; t < ntopics; t++) {
        title_at[t] = add_title(topics[t].title);
    }
    book_title_at = add_title(book_title);
}

/* --- the file ---------------------------------------------------------------------- */

/* The next sibling of topic t in the contents. */
static unsigned next_sibling(int t)
{
    int p = topics[t].parent;
    int k;

    if (p < 0) {
        int pg = topics[t].page;

        return pg + 1 < npages ? (unsigned)pages[pg + 1].first_topic : NONE;
    }
    for (k = 0; k + 1 < topics[p].nchildren; k++) {
        if (topics[p].children[k] == t) {
            return (unsigned)topics[p].children[k + 1];
        }
    }

    return NONE;
}

int write_book(const char* path)
{
    static uint8_t head[HEAD_SIZE];
    unsigned long text_len = 0;
    unsigned max_packed = 0;
    unsigned long o_nodes, o_topics, o_titles, o_text, o_keys, o_keydir, o_words, o_worddir,
        o_norm, o_dict, size;
    unsigned long at;
    int t;

    make_titles();
    for (t = 0; t < ntopics; t++) {
        unsigned n = markup[t].packed_len;

        if (n > 0xFFFF) {
            md_error(pages[topics[t].page].path, 0, "topic '%s' is too big", topics[t].title);

            return -1;
        }
        text_len += n;
        if (n > max_packed) {
            max_packed = n;
        }
    }

    o_nodes = HEAD_SIZE;
    o_topics = o_nodes + 10UL * (unsigned long)ntopics;
    o_titles = o_topics + 16UL * (unsigned long)ntopics;
    o_text = o_titles + pool_len;
    o_keys = o_text + text_len;
    o_keydir = o_keys + keys_section.len;
    o_words = o_keydir + keys_section.dir_len;
    o_worddir = o_words + words_section.len;
    o_norm = o_worddir + words_section.dir_len;
    o_dict = o_norm + norm_len;
    size = o_dict;

    memcpy(head, "AGONREF\x1a", 8);
    put16(head + 8, VERSION);
    put16(head + 10, book_title[0] ? 1 : 0);
    memcpy(head + 12, "md2ref", 6);
    put16(head + 36, (unsigned)ntopics);
    put16(head + 38, (unsigned)ntopics);
    put16(head + 40, keys_section.blocks);
    put16(head + 42, words_section.blocks);
    put32(head + 44, keys_section.count);
    put32(head + 48, words_section.count);
    put32(head + 52, o_nodes);
    put32(head + 56, o_topics);
    put32(head + 60, o_titles);
    put32(head + 64, o_text);
    put32(head + 68, o_keys);
    put32(head + 72, o_keydir);
    put32(head + 76, o_words);
    put32(head + 80, o_worddir);
    put32(head + 84, o_norm);
    put32(head + 88, o_dict);
    put16(head + 92, 0);
    put16(head + 94, view_size);
    put16(head + 96, max_packed);
    put32(head + 100, size);
    put32(head + 104, book_title[0] ? book_title_at : 0);

    out = fopen(path, "wb");
    if (!out) {
        md_error(path, 0, "cannot be written");

        return -1;
    }
    written = 0;
    failed = 0;
    bytes(head, HEAD_SIZE);
    for (t = 0; t < ntopics; t++) {
        u32(title_at[t]);
        u16((unsigned)t);
        u16(topics[t].nchildren ? (unsigned)topics[t].children[0] : NONE);
        u16(next_sibling(t));
    }
    at = 0;
    for (t = 0; t < ntopics; t++) {
        u32(title_at[t]);
        u32(at);
        u16(markup[t].packed_len);
        u16(markup[t].len);
        u16(topics[t].parent < 0 ? NONE : (unsigned)topics[t].parent);
        u16((unsigned)topics[t].span);
        at += markup[t].packed_len;
    }
    bytes(pool, pool_len);
    for (t = 0; t < ntopics; t++) {
        bytes(markup[t].packed, markup[t].packed_len);
    }
    bytes(keys_section.data, keys_section.len);
    bytes(keys_section.dir, keys_section.dir_len);
    bytes(words_section.data, words_section.len);
    bytes(words_section.dir, words_section.dir_len);
    bytes(norm_settings, norm_len);
    if (fclose(out) != 0 || failed || written != size) {
        md_error(path, 0, "could not be written whole");

        return -1;
    }

    return 0;
}
