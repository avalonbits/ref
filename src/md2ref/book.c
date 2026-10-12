/*
 * The pages as a book: topics split at headings, each holding its own text
 * up to the next heading, with its descendants numbered after it; anchors
 * worked out as Python-Markdown makes them, so links written for MkDocs
 * resolve; and links resolved to topic numbers.
 *
 * A page that opens with its only level-1 heading has that heading as its
 * top topic; otherwise its top topic is the text before its first heading,
 * titled from its file name, and every heading sits under it.
 */
#include <stdio.h>
#include <string.h>

#include "md2ref.h"

#define MAX_TOPICS 4000
#define TITLE_MAX  256

topic* topics;
int ntopics;
const char* book_title;

typedef struct {
    int page;
    const char* name;
    int topic;
} anchor;

static anchor* anchors;
static int nanchors;
static int max_anchors;
static int max_topics;

static void add_anchor(int page, const char* name, int t)
{
    int k;

    for (k = 0; k < nanchors; k++) {
        if (anchors[k].page == page && strcmp(anchors[k].name, name) == 0) {
            return;                         /* the first keeps it */
        }
    }
    if (nanchors == max_anchors) {
        max_anchors = max_anchors ? max_anchors * 2 : 256;
        anchors = md_realloc(anchors, (unsigned)max_anchors * (unsigned)sizeof(anchor));
    }
    anchors[nanchors].page = page;
    anchors[nanchors].name = name;
    anchors[nanchors].topic = t;
    nanchors++;
}

static int has_anchor(int page, const char* name)
{
    int k;

    for (k = 0; k < nanchors; k++) {
        if (anchors[k].page == page && strcmp(anchors[k].name, name) == 0) {
            return 1;
        }
    }

    return 0;
}

/* Python-Markdown's toc slug: word characters, spaces and hyphens kept,
 * lower case, runs of spaces and hyphens one hyphen. */
static char* slugify(const char* title)
{
    char* out = arena_alloc((unsigned)strlen(title) + 1);
    char* o = out;
    const char* s = title;
    int dash = 0;

    while (*s == ' ') {
        s++;
    }
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;

        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') {
            if (dash && o > out) {
                *o++ = '-';
            }
            dash = 0;
            *o++ = (char)c;
        } else if (c >= 'A' && c <= 'Z') {
            if (dash && o > out) {
                *o++ = '-';
            }
            dash = 0;
            *o++ = (char)(c + 32);
        } else if (c == ' ' || c == '-') {
            dash = 1;
        }
    }
    *o = 0;

    return out;
}

static char* title_of(const char* heading)
{
    static char buf[TITLE_MAX];

    inline_plain(heading, buf, (int)sizeof buf);

    return arena_strdup(buf);
}

/* UTF-8 text in the Agon's character set, as titles are kept. */
static char* agon_string(const char* s)
{
    char* out = arena_alloc((unsigned)strlen(s) + 1);
    char* o = out;

    while (*s) {
        *o++ = (char)agon_char(&s);
    }
    *o = 0;

    return out;
}

/* A page's name as a title: its file name, without .md, - and _ as spaces. */
static char* title_from_name(const char* name)
{
    char* t = agon_string(name);
    char* dot = strrchr(t, '.');
    char* c;

    if (dot) {
        *dot = 0;
    }
    for (c = t; *c; c++) {
        if (*c == '-' || *c == '_') {
            *c = ' ';
        }
    }

    return t;
}

/* A heading that starts a topic: not in a note, a quote or a list. */
static int starts_topic(const block* b)
{
    return b->kind == B_HEAD && !b->note && !b->indent;
}

/* A topic, its heading kept: the page it is from will not be. -1 if there
 * are too many. */
static int new_topic(const char* title, const char* heading, int page, int level, int parent,
                     int first_block)
{
    topic* t;

    if (ntopics >= MAX_TOPICS) {
        md_error(pages[page].path, 0, "too many topics (at most %d)", MAX_TOPICS);

        return -1;
    }
    if (ntopics == max_topics) {
        max_topics = max_topics ? max_topics * 2 : 64;
        topics = md_realloc(topics, (unsigned)max_topics * (unsigned)sizeof(topic));
    }
    t = &topics[ntopics];
    memset(t, 0, sizeof *t);

    t->title = title;
    t->heading = heading ? arena_strdup(heading) : NULL;
    t->page = page;
    t->level = level;
    t->parent = parent;
    t->first_block = first_block;
    t->end_block = first_block;

    return ntopics++;
}

static void split_page(int pi)
{
    page* p = &pages[pi];
    int h1 = 0;
    int k;
    int top;
    int start = 0;
    static int stack[8];
    int depth;

    for (k = 0; k < p->nblocks; k++) {
        if (starts_topic(&p->blocks[k]) && p->blocks[k].level == 1) {
            h1++;
        }
    }
    p->first_topic = ntopics;
    if (p->nblocks && p->blocks[0].kind == B_HEAD && p->blocks[0].level == 1 && h1 == 1) {
        top = new_topic(title_of(p->blocks[0].text), p->blocks[0].text, pi, 1, -1, 1);
        start = 1;
    } else {
        top = new_topic(title_from_name(p->name), NULL, pi, 0, -1, 0);
    }
    if (top < 0) {
        return;
    }
    stack[0] = top;
    depth = 1;
    for (k = start; k < p->nblocks; k++) {
        block* b = &p->blocks[k];
        int t;

        if (!starts_topic(b)) {
            topics[stack[depth - 1]].end_block = k + 1;
            continue;
        }
        while (depth > 1 && topics[stack[depth - 1]].level >= b->level) {
            depth--;
        }
        t = new_topic(title_of(b->text), b->text, pi, b->level, stack[depth - 1], k + 1);
        if (t < 0) {
            return;
        }
        topics[stack[depth - 1]].nchildren++;
        if (depth < 8) {
            stack[depth++] = t;
        }
    }
    p->ntopics = ntopics - p->first_topic;
}

/* Anchors: explicit {#id}s first, then each heading's slug, made unique as
 * Python-Markdown makes them (_1, _2 ...); then {#id}s on paragraphs and
 * table cells, leading to the topic they are in. */
static void page_anchors(int pi)
{
    page* p = &pages[pi];
    int t;
    int k;

    for (t = p->first_topic; t < p->first_topic + p->ntopics; t++) {
        block* b = topics[t].heading ? &p->blocks[topics[t].first_block - 1] : NULL;

        if (b && b->id) {
            topics[t].anchor = arena_strdup(b->id);
            add_anchor(pi, topics[t].anchor, t);
        }
    }
    for (t = p->first_topic; t < p->first_topic + p->ntopics; t++) {
        char* base;
        char* a;
        int n = 1;

        if (!topics[t].heading || topics[t].anchor) {
            continue;
        }
        base = slugify(topics[t].title);
        a = base;
        while (has_anchor(pi, a)) {
            a = arena_alloc((unsigned)strlen(base) + 8);
            sprintf(a, "%s_%d", base, n++);
        }
        topics[t].anchor = a;
        add_anchor(pi, a, t);
    }
    for (t = p->first_topic; t < p->first_topic + p->ntopics; t++) {
        for (k = topics[t].first_block; k < topics[t].end_block; k++) {
            int j;

            for (j = 0; j < p->blocks[k].nids; j++) {
                add_anchor(pi, arena_strdup(p->blocks[k].ids[j]), t);
            }
        }
    }
}

void book_page(int pi)
{
    split_page(pi);
    page_anchors(pi);
}

int book_done(const char* title)
{
    int t;

    /* Each topic's children, in order. */
    for (t = 0; t < ntopics; t++) {
        topics[t].children = arena_alloc((unsigned)(topics[t].nchildren + 1) * sizeof(int));
        topics[t].nchildren = 0;
    }
    for (t = 0; t < ntopics; t++) {
        if (topics[t].parent >= 0) {
            topic* parent = &topics[topics[t].parent];

            parent->children[parent->nchildren++] = t;
        }
    }
    book_title = title ? agon_string(title) : ntopics ? topics[0].title : "";

    return ntopics ? 0 : -1;
}

int resolve_link(const page* p, int no, const char* href, int len)
{
    static char path[TITLE_MAX];
    static char frag[TITLE_MAX];
    const char* hash = memchr(href, '#', (size_t)len);
    int plen = hash ? (int)(hash - href) : len;
    int flen = hash ? len - plen - 1 : 0;
    const char* base;
    int pi = (int)(p - pages);
    int k;

    if (plen >= TITLE_MAX || flen >= TITLE_MAX) {
        plen = flen = 0;
    }
    memcpy(path, href, (size_t)plen);
    path[plen] = 0;
    memcpy(frag, hash ? hash + 1 : "", (size_t)flen);
    frag[flen] = 0;

    if (plen) {
        /* The pages are one folder: a link names a page by its file name. */
        base = strrchr(path, '/');
        base = base ? base + 1 : path;
        for (pi = 0; pi < npages; pi++) {
            const char* n = pages[pi].name;
            int bl = (int)strlen(base);

            if (strcmp(n, base) == 0
                || (strncmp(n, base, (size_t)bl) == 0 && strcmp(n + bl, ".md") == 0)) {
                break;
            }
        }
        if (pi == npages) {
            goto fail;
        }
    }
    if (!flen) {
        return pages[pi].first_topic;
    }
    for (k = 0; k < nanchors; k++) {
        if (anchors[k].page == pi && strcmp(anchors[k].name, frag) == 0) {
            return anchors[k].topic;
        }
    }

fail:
    if (md_keep_going) {
        md_warn(p->path, no, "cannot resolve link '%.*s'", len, href);
    } else {
        md_error(p->path, no, "cannot resolve link '%.*s'", len, href);
    }

    return -1;
}
