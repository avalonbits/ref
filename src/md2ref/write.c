/*
 * The scratch files, and the document: header, contents, topics, titles,
 * text, keys, words, settings and an empty dictionary, in that order
 * (docs/FORMAT.md).
 *
 * The text is written to <out>~t as each topic is complete, and the words'
 * records to <out>~w, as the tables before them are only known at the end.
 * Then the document is written front to back: the tables from memory, the
 * text copied a window at a time with each link's placeholder patched as
 * it goes by, and the words gathered from their file. The header, which
 * says where everything is, goes in last, over the space left for it.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "md2ref.h"

#define HEAD_SIZE 128
#define VERSION   1
#define CHUNK     2048              /* each file's window */

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

/* --- files, a window at a time ------------------------------------------------ */

typedef struct {
    FILE* f;
    char* path;
    uint8_t* buf;
    unsigned cap;           /* buf's size */
    int moved;              /* read from since written: back to the end to write */
    unsigned n;             /* in buf: to write, or read and not yet taken */
    unsigned at;            /* reading: taken so far */
    unsigned long size;     /* written */
    unsigned long on_card;  /* of that, what flush has put in the file */
    int failed;
} file;

static file text_file;
static file words_file;
static file keys_file;
static file links_file;
static file runs_file;
static file doc;

#define LINKS_BUF 512

static int open_file_sized(file* f, const char* path, const char* suffix, const char* mode,
                           unsigned cap);

static int open_file(file* f, const char* path, const char* suffix, const char* mode)
{
    return open_file_sized(f, path, suffix, mode, CHUNK);
}

static int open_file_sized(file* f, const char* path, const char* suffix, const char* mode,
                           unsigned cap)
{
    f->path = md_realloc(NULL, (unsigned)(strlen(path) + strlen(suffix) + 1));
    strcpy(f->path, path);
    strcat(f->path, suffix);
    f->f = fopen(f->path, mode);
    if (!f->buf) {
        f->buf = md_realloc(NULL, cap);
        f->cap = cap;
    }
    f->moved = 0;
    f->n = f->at = 0;
    f->size = f->on_card = 0;
    f->failed = !f->f;
    if (!f->f) {
        md_error(f->path, 0, "cannot be written");

        return -1;
    }

    return 0;
}

static void flush(file* f)
{
    if (f->moved && f->f) {
        /* Back to where writing left off -- by where that is: agondev's
         * fseek from SEEK_END goes nowhere. */
        if (fseek(f->f, (long)f->on_card, SEEK_SET) != 0) {
            f->failed = 1;
        }
        f->moved = 0;
    }
    if (f->n && f->f && fwrite(f->buf, 1, f->n, f->f) != f->n) {
        f->failed = 1;
    }
    f->on_card += f->n;
    f->n = 0;
}

static void put(file* f, const void* p, unsigned n)
{
    const uint8_t* s = p;

    f->size += n;
    while (n) {
        unsigned k = f->cap - f->n < n ? f->cap - f->n : n;

        memcpy(f->buf + f->n, s, k);
        f->n += k;
        s += k;
        n -= k;
        if (f->n == f->cap) {
            flush(f);
        }
    }
}

/* Written, closed, and open again to be read from the start. */
static int reread(file* f)
{
    flush(f);
    if (f->f) {
        fclose(f->f);
    }
    f->f = fopen(f->path, "rb");
    f->n = f->at = 0;
    if (!f->f || f->failed) {
        md_error(f->path, 0, "cannot be read back");

        return -1;
    }

    return 0;
}

static unsigned get(file* f, uint8_t* p, unsigned n)
{
    return f->f ? (unsigned)fread(p, 1, n, f->f) : 0;
}

/* n bytes from at, of a file still being written, or as many as there are:
 * from the card, as far as it is there, and the rest from what waits in buf
 * to be written. */
static unsigned read_at(file* f, unsigned long at, uint8_t* p, unsigned n)
{
    unsigned long on_card = f->on_card;
    unsigned got;

    if (at >= f->size) {
        return 0;
    }
    if (n > f->size - at) {
        n = (unsigned)(f->size - at);   /* no more than there is: a record near the end */
    }
    got = n;

    if (at < on_card && f->f) {
        unsigned k = at + n <= on_card ? n : (unsigned)(on_card - at);

        f->moved = 1;
        if (fseek(f->f, (long)at, SEEK_SET) != 0 || fread(p, 1, k, f->f) != k) {
            f->failed = 1;
        }
        p += k;
        at += k;
        n -= k;
    }
    if (n) {
        memcpy(p, f->buf + (at - on_card), n);
    }

    return got;
}

static void close_file(file* f, int keep)
{
    if (f->f) {
        fclose(f->f);
        f->f = NULL;
    }
    if (f->path && !keep) {
        remove(f->path);
    }
    free(f->path);
    f->path = NULL;
    free(f->buf);
    f->buf = NULL;
}

static const char* out_path;

int scratch_open(const char* out)
{
    out_path = out;
    if (open_file(&text_file, out, "~t", "wb") != 0 || open_file(&words_file, out, "~w", "wb") != 0
        || open_file(&keys_file, out, "~k", "wb") != 0
        || open_file_sized(&links_file, out, "~l", "w+b", LINKS_BUF) != 0) {
        scratch_close();

        return -1;
    }

    return 0;
}

void scratch_close(void)
{
    close_file(&text_file, 0);
    close_file(&words_file, 0);
    close_file(&keys_file, 0);
    close_file(&links_file, 0);
    close_file(&runs_file, 0);
}

int runs_open(void)
{
    return open_file(&runs_file, out_path, "~r", "wb");
}

void runs_put(const uint8_t* p, unsigned n)
{
    put(&runs_file, p, n);
}

unsigned long runs_size(void)
{
    return runs_file.size;
}

int runs_reread(void)
{
    return reread(&runs_file);
}

unsigned runs_get(unsigned long at, uint8_t* p, unsigned n)
{
    if (!runs_file.f || fseek(runs_file.f, (long)at, SEEK_SET) != 0) {
        return 0;
    }

    return get(&runs_file, p, n);
}

unsigned long write_text(const uint8_t* p, unsigned n)
{
    unsigned long at = text_file.size;

    put(&text_file, p, n);

    return at;
}

/* The links waiting for a page to be read, <out>~l: a record appended,
 * where it starts; and read back, from anywhere. A small buffer, as each
 * record is read back on its own. */
unsigned long links_put(const uint8_t* p, unsigned n)
{
    unsigned long at = links_file.size;

    put(&links_file, p, n);

    return at;
}

unsigned links_read(unsigned long at, uint8_t* p, unsigned n)
{
    return read_at(&links_file, at, p, n);
}

int links_failed(void)
{
    return links_file.failed;
}

void keys_put(const uint8_t* p, unsigned n)
{
    put(&keys_file, p, n);
}

int keys_rewind(void)
{
    return reread(&keys_file);
}

unsigned keys_get(uint8_t* p, unsigned n)
{
    return get(&keys_file, p, n);
}

void words_put(const uint8_t* p, unsigned n)
{
    put(&words_file, p, n);
}

int words_rewind(void)
{
    return reread(&words_file);
}

unsigned words_get(uint8_t* p, unsigned n)
{
    return get(&words_file, p, n);
}

/* --- patches: each link's placeholder, in the order of the text ---------------- */

/* Its place in the text file, and the topic number to put there -- or what
 * it waits for: a link's page to be read (PATCH_WAITING), or its own page
 * (PATCH_MADE + the topic's number as made). Each is settled when that
 * page is read, so all hold a number by the end, and the text is swept
 * once and patched in order, as acc's sweep does. In blocks, as acc keeps
 * them: never moved, so a table of them never needs room for two. */
typedef struct {
    unsigned long at;
    unsigned value;
} patch;

#define PATCH_BLOCK 256
#define MAX_PATCH_BLOCKS 256

static patch* patch_block[MAX_PATCH_BLOCKS];
static unsigned npatches;

#define PATCH(k) (patch_block[(k) >> 8][(uint8_t)(k)])

unsigned add_patch(unsigned long at, unsigned value)
{
    if (!patch_block[npatches >> 8]) {
        if ((npatches >> 8) >= MAX_PATCH_BLOCKS) {
            md_error("md2ref", 0, "too many links (at most %u)", PATCH_BLOCK * MAX_PATCH_BLOCKS);

            return npatches;
        }
        patch_block[npatches >> 8] = md_realloc(NULL, PATCH_BLOCK * (unsigned)sizeof(patch));
    }
    PATCH(npatches).at = at;
    PATCH(npatches).value = value;

    return npatches++;
}

void set_patch(unsigned k, unsigned value)
{
    if (k < npatches) {
        PATCH(k).value = value;
    }
}

unsigned patch_count(void)
{
    return npatches;
}

unsigned patch_value(unsigned k)
{
    return PATCH(k).value;
}

/* The text, from its file into the document, patched. Placeholders are
 * written in the order of the text, so the next to patch is always the
 * first not yet patched. */
static void copy_text(void)
{
    static uint8_t w[CHUNK];
    unsigned long at = 0;
    unsigned n;
    int carry = -1;                 /* a placeholder's high byte, for the next window */
    unsigned p = 0;

    while ((n = get(&text_file, w, CHUNK)) > 0) {
        if (carry >= 0) {
            w[0] = (uint8_t)carry;
            carry = -1;
        }
        for (; p < npatches && PATCH(p).at < at + n; p++) {
            unsigned t = PATCH(p).value <= 0xFFFF ? PATCH(p).value : NONE;
            unsigned k = (unsigned)(PATCH(p).at - at);

            w[k] = (uint8_t)(t & 0xFF);
            if (k + 1 < n) {
                w[k + 1] = (uint8_t)(t >> 8);
            } else {
                carry = (int)(t >> 8);
            }
        }
        put(&doc, w, n);
        at += n;
    }
}

/* --- titles: each once ----------------------------------------------------------- */

static uint8_t* pool;
static unsigned pool_len;
static unsigned* title_at;      /* each topic's title's offset */
static unsigned book_title_at;

/* Each title in the pool once, found through a hash of the titles in it:
 * slots hold a title's offset + 1, 0 for none, twice as many as titles. */
static unsigned* title_slot;
static unsigned title_slots;                /* a power of two */

static unsigned add_title(const char* s)
{
    unsigned n = (unsigned)strlen(s);
    unsigned h;
    unsigned mask = title_slots - 1;
    unsigned k;

    if (n > 255) {
        n = 255;
    }
    for (h = md_hash(0, s, n) & mask; title_slot[h]; h = (h + 1) & mask) {
        k = title_slot[h] - 1;
        if (pool[k] == n && memcmp(pool + k + 1, s, n) == 0) {
            return k;
        }
    }
    k = pool_len;
    title_slot[h] = k + 1;
    pool[pool_len] = (uint8_t)n;
    memcpy(pool + pool_len + 1, s, n);
    pool_len += 1 + n;

    return k;
}

/* The nav's items' titles, in the pool, by item: for the contents. */
static unsigned* nav_title_at;

static void make_titles(void)
{
    unsigned size = 256 + (unsigned)strlen(book_title);
    int t;
    int k;

    for (t = 0; t < ntopics; t++) {
        size += 1 + (unsigned)strlen(TOPIC(t).title);
    }
    for (k = 0; k < nav_count; k++) {
        size += 1 + (unsigned)strlen(nav[k].title);
    }
    pool = arena_alloc(size);
    title_at = arena_alloc((unsigned)ntopics * sizeof(unsigned));
    for (title_slots = 64; title_slots < (unsigned)(ntopics + 1) * 2; title_slots *= 2) {
    }
    title_slot = md_realloc(NULL, title_slots * (unsigned)sizeof(unsigned));
    memset(title_slot, 0, title_slots * sizeof(unsigned));
    for (t = 0; t < ntopics; t++) {
        title_at[t] = add_title(TOPIC(t).title);
    }
    book_title_at = add_title(book_title);
    if (nav_first >= 0) {
        nav_title_at = arena_alloc((unsigned)(nav_count + 1) * sizeof(unsigned));
        for (k = 0; k < nav_count; k++) {
            nav_title_at[k] = nav[k].title[0] ? add_title(nav[k].title) : 0;
        }
    }
    free(title_slot);
    title_slot = NULL;
}

/* --- the contents, from the nav ------------------------------------------------ */

/* With a nav, the contents are its sections, which open no topic, and its
 * pages, each with its topics under it: nodes in preorder, made here as
 * the builder makes them. Without one, a node for each topic, as before. */
typedef struct {
    unsigned title;
    uint16_t topic;
    uint16_t child;
    uint16_t next;
    uint8_t pad;
} node;

static node* nodes;
static unsigned nnodes;

/* Topic t and those under it, as nodes: the first's index. */
static unsigned topic_nodes(int t)
{
    unsigned at = nnodes++;
    unsigned prev = NONE;
    int c;

    nodes[at].title = title_at[t];
    nodes[at].topic = (uint16_t)t;
    nodes[at].child = NONE;
    nodes[at].next = NONE;
    for (c = TOPIC(t).first_child; c >= 0; c = TOPIC(c).next) {
        unsigned ci = topic_nodes(c);

        if (prev == NONE) {
            nodes[at].child = (uint16_t)ci;
        } else {
            nodes[prev].next = (uint16_t)ci;
        }
        prev = ci;
    }

    return at;
}

/* Items from k on, as nodes: the first's index, NONE for none. A page is
 * its top topic, titled by the nav when the nav titles it. */
static unsigned item_nodes(int k)
{
    unsigned first = NONE;
    unsigned prev = NONE;

    for (; k >= 0; k = nav[k].next) {
        unsigned at;

        if (nav[k].page >= 0) {
            int top = pages[nav[k].page].first_topic;

            at = topic_nodes(top);
            if (nav[k].title[0]) {
                nodes[at].title = nav_title_at[k];
            }
        } else {
            at = nnodes++;
            nodes[at].title = nav_title_at[k];
            nodes[at].topic = NONE;
            nodes[at].next = NONE;
            nodes[at].child = NONE;
            nodes[at].child = (uint16_t)item_nodes(nav[k].child);
        }
        if (first == NONE) {
            first = at;
        } else {
            nodes[prev].next = (uint16_t)at;
        }
        prev = at;
    }

    return first;
}

static void make_nodes(void)
{
    nodes = md_realloc(NULL, (unsigned)(ntopics + nav_count + 1) * (unsigned)sizeof(node));
    nnodes = 0;
    item_nodes(nav_first);
}

/* --- the document -------------------------------------------------------------- */

static void u8(unsigned v)
{
    uint8_t b = (uint8_t)v;

    put(&doc, &b, 1);
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

static void emit_words(const void* p, unsigned n)
{
    put(&doc, p, n);
}

int write_book(const char* path)
{
    static uint8_t head[HEAD_SIZE];
    unsigned long o_nodes, o_topics, o_titles, o_text, o_keys, o_keydir, o_words, o_worddir,
        o_norm, o_dict;
    int t;

    make_titles();
    if (nav_first >= 0) {
        make_nodes();
    } else {
        nnodes = (unsigned)ntopics;
    }
    if (reread(&text_file) != 0 || open_file(&doc, path, "", "wb") != 0) {
        return -1;
    }
    o_nodes = HEAD_SIZE;
    o_topics = o_nodes + 10UL * (unsigned long)nnodes;
    o_titles = o_topics + 16UL * (unsigned long)ntopics;
    o_text = o_titles + pool_len;
    o_keys = o_text + text_file.size;
    o_keydir = o_keys + keys_section.len;
    o_words = o_keydir + keys_section.dir_len;

    put(&doc, head, HEAD_SIZE);             /* for now */
    if (nodes) {
        unsigned k;

        for (k = 0; k < nnodes; k++) {
            u32(nodes[k].title);
            u16(nodes[k].topic);
            u16(nodes[k].child);
            u16(nodes[k].next);
        }
        free(nodes);
        nodes = NULL;
    } else {
        for (t = 0; t < ntopics; t++) {
            u32(title_at[t]);
            u16((unsigned)t);
            u16(TOPIC(t).first_child >= 0 ? (unsigned)TOPIC(t).first_child : NONE);
            u16(TOPIC(t).next >= 0 ? (unsigned)TOPIC(t).next : NONE);
        }
    }
    for (t = 0; t < ntopics; t++) {
        u32(title_at[t]);
        u32(TOPIC(t).text_at);
        u16(TOPIC(t).packed);
        u16(TOPIC(t).len);
        u16(TOPIC(t).parent < 0 ? NONE : (unsigned)TOPIC(t).parent);
        u16((unsigned)TOPIC(t).span);
    }
    put(&doc, pool, pool_len);
    copy_text();
    close_file(&text_file, 0);
    put(&doc, keys_section.data, (unsigned)keys_section.len);
    put(&doc, keys_section.dir, keys_section.dir_len);
    if (write_words(emit_words) != 0) {
        close_file(&doc, 0);

        return -1;
    }
    o_worddir = o_words + words_section.len;
    put(&doc, words_section.dir, words_section.dir_len);
    o_norm = o_worddir + words_section.dir_len;
    put(&doc, norm_settings, norm_len);
    o_dict = o_norm + norm_len;

    memcpy(head, "AGONREF\x1a", 8);
    put16(head + 8, VERSION);
    put16(head + 10, book_title[0] ? 1 : 0);
    memcpy(head + 12, "md2ref", 6);
    put16(head + 36, (unsigned)ntopics);
    put16(head + 38, nnodes);
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
    put32(head + 100, o_dict);
    put32(head + 104, book_title[0] ? book_title_at : 0);

    /* The header, over the space left for it. */
    flush(&doc);
    if (doc.f && (fseek(doc.f, 0, SEEK_SET) != 0 || fwrite(head, 1, HEAD_SIZE, doc.f) != HEAD_SIZE)) {
        doc.failed = 1;
    }
    if (!doc.f || fclose(doc.f) != 0 || doc.failed || doc.size != o_dict) {
        doc.f = NULL;
        md_error(path, 0, "could not be written whole");
        close_file(&doc, 0);

        return -1;
    }
    doc.f = NULL;
    close_file(&doc, 1);

    return 0;
}
