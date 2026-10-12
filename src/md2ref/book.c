/*
 * The pages as a book, built as they are read: topics split at headings,
 * each holding its own text up to the next heading, with its descendants
 * numbered after it; anchors worked out as Python-Markdown makes them, so
 * links written for MkDocs resolve; and links resolved to topics -- or, to
 * a page not yet read, left pending until the end, when every anchor is
 * known.
 *
 * A page that opens with its only level-1 heading has that heading as its
 * top topic. Any other page's top topic is the text before its first
 * heading, titled from its file name, and every heading sits under it --
 * made late, in front of the others, if a second level-1 heading shows the
 * first was not the only one.
 */
#include <stdio.h>
#include <string.h>

#include "md2ref.h"

#define TITLE_MAX 256
#define DEPTH     8

topic* topics;
int ntopics;
const char* book_title;

static int max_topics;
static int last_root = -1;
static int made;
static int top_is_heading;          /* this page's top is its level-1 heading, so far */
int* final_of;

/* --- anchors ------------------------------------------------------------------ */

typedef struct {
    int page;
    const char* name;
    int topic;
} anchor;

static anchor* anchors;
static int nanchors;
static int max_anchors;

/* The {#id}s set on a page's paragraphs and cells, as it is read. */
typedef struct {
    int topic;
    const char* name;
} id_set;

static id_set* ids;
static int nids;
static int max_ids;

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

/* --- topics ------------------------------------------------------------------ */

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

/* The page being read: its open topics, outermost first. */
static int stack[DEPTH];
static int depth;

int book_starts_topic(const block* b)
{
    return b->kind == B_HEAD && !b->note && !b->indent;
}

/* A topic, under parent, its heading kept: the page it is from will not
 * be. -1 if there are too many. */
static int new_topic(const char* title, const block* b, int page, int level, int parent)
{
    topic* t;

    if (ntopics >= MAX_TOPICS) {
        md_error(pages[page].path, b ? b->no : 0, "too many topics (at most %d)", MAX_TOPICS);

        return -1;
    }
    if (ntopics == max_topics) {
        max_topics = max_topics ? max_topics + max_topics / 2 : 64;
        topics = md_realloc(topics, (unsigned)max_topics * (unsigned)sizeof(topic));
    }
    t = &topics[ntopics];
    memset(t, 0, sizeof *t);
    t->title = title;
    t->heading = b ? arena_strdup(b->text) : NULL;
    t->id = b && b->id ? arena_strdup(b->id) : NULL;
    t->page = page;
    t->made = made++;
    t->level = level;
    t->parent = parent;
    t->first_child = t->last_child = t->next = -1;
    if (parent >= 0) {
        topic* p = &topics[parent];

        if (p->last_child >= 0) {
            topics[p->last_child].next = ntopics;
        } else {
            p->first_child = ntopics;
        }
        p->last_child = ntopics;
        p->nchildren++;
    } else {
        if (last_root >= 0) {
            topics[last_root].next = ntopics;
        }
        last_root = ntopics;
    }
    pages[page].ntopics++;

    return ntopics++;
}

void book_page(int pi)
{
    pages[pi].first_topic = ntopics;
    pages[pi].ntopics = 0;
    depth = 0;
    nids = 0;
    top_is_heading = 0;
}

static void shift(int16_t* t, int from)
{
    if (*t >= from) {
        (*t)++;
    }
}

/* A second level-1 heading: the page's top, titled by its name, made
 * before its first, which goes under it with everything else. */
static int make_top(int pi)
{
    int f = pages[pi].first_topic;
    int k;

    if (ntopics >= MAX_TOPICS) {
        md_error(pages[pi].path, 0, "too many topics (at most %d)", MAX_TOPICS);

        return -1;
    }
    if (ntopics == max_topics) {
        max_topics += max_topics / 2;
        topics = md_realloc(topics, (unsigned)max_topics * (unsigned)sizeof(topic));
    }
    memmove(&topics[f + 1], &topics[f], (unsigned)(ntopics - f) * sizeof(topic));
    ntopics++;
    for (k = 0; k < ntopics; k++) {
        if (k == f) {
            continue;
        }
        shift(&topics[k].parent, f);
        shift(&topics[k].first_child, f);
        shift(&topics[k].last_child, f);
        shift(&topics[k].next, f);
    }
    for (k = 0; k < nids; k++) {
        if (ids[k].topic >= f) {
            ids[k].topic++;
        }
    }
    /* The root before the page's leads to the new top, not its heading. */
    for (k = 0; k < f; k++) {
        if (topics[k].next == f + 1) {
            topics[k].next = f;
        }
    }
    memset(&topics[f], 0, sizeof(topic));
    topics[f].title = title_from_name(pages[pi].name);
    topics[f].page = pi;
    topics[f].made = made++;
    topics[f].level = 0;
    topics[f].parent = -1;
    topics[f].first_child = topics[f].last_child = f + 1;
    topics[f].nchildren = 1;
    topics[f].next = -1;
    topics[f + 1].parent = f;
    topics[f + 1].next = -1;
    last_root = f;
    pages[pi].ntopics++;
    stack[0] = f;
    depth = 1;
    top_is_heading = 0;
    render_top(f);

    return 0;
}

int book_top(int pi, const block* first)
{
    int t;

    if (first && book_starts_topic(first) && first->level == 1) {
        top_is_heading = 1;                 /* the heading is the top, so far */

        return -1;
    }
    t = new_topic(title_from_name(pages[pi].name), NULL, pi, 0, -1);
    if (t >= 0) {
        stack[0] = t;
        depth = 1;
    }

    return t;
}

int book_heading(int pi, const block* b)
{
    int t;

    if (top_is_heading && b->level == 1 && pages[pi].ntopics > 0 && make_top(pi) != 0) {
        return -1;
    }
    while (depth > 0 && topics[stack[depth - 1]].level >= b->level) {
        depth--;
    }
    t = new_topic(title_of(b->text), b, pi, b->level, depth ? stack[depth - 1] : -1);
    if (t >= 0 && depth < DEPTH) {
        stack[depth++] = t;
    }

    return t;
}

void book_ids(int pi, int t, const block* b)
{
    int j;

    (void)pi;
    for (j = 0; j < b->nids; j++) {
        if (nids == max_ids) {
            max_ids = max_ids ? max_ids * 2 : 32;
            ids = md_realloc(ids, (unsigned)max_ids * (unsigned)sizeof(id_set));
        }
        ids[nids].topic = t;
        ids[nids].name = arena_strdup(b->ids[j]);
        nids++;
    }
}

/* Anchors: explicit {#id}s first, then each heading's slug, made unique as
 * Python-Markdown makes them (_1, _2 ...); then {#id}s on paragraphs and
 * table cells, leading to the topic they are in. */
void book_page_done(int pi)
{
    page* p = &pages[pi];
    int t;
    int k;

    for (t = p->first_topic; t < p->first_topic + p->ntopics; t++) {
        if (topics[t].id) {
            topics[t].anchor = topics[t].id;
            add_anchor(pi, topics[t].id, t);
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
    for (k = 0; k < nids; k++) {
        add_anchor(pi, ids[k].name, ids[k].topic);
    }
    nids = 0;
    p->done = 1;
}

void number_topics(void)
{
    int t;

    final_of = md_realloc(NULL, (unsigned)(made + 1) * (unsigned)sizeof(int));
    for (t = 0; t < ntopics; t++) {
        final_of[topics[t].made] = t;
    }
}

int book_done(const char* title)
{
    book_title = title ? agon_string(title) : ntopics ? topics[0].title : "";

    return ntopics ? 0 : -1;
}

/* --- links ------------------------------------------------------------------- */

/* A link to a page not yet read: where it is, and what it names. */
typedef struct {
    int page;               /* the page it leads to */
    const char* frag;       /* the anchor there, or NULL for its top */
    const char* href;       /* as written */
    const char* from;       /* the page it is on, and the line */
    int no;
    unsigned topic;
} pending;

static pending* links;
static int nlinks;
static int max_links;

/* The page a link names, from its file name (the pages are one folder), or
 * -1. */
static int page_named(const char* path)
{
    const char* base = strrchr(path, '/');
    int bl;
    int pi;

    base = base ? base + 1 : path;
    bl = (int)strlen(base);
    for (pi = 0; pi < npages; pi++) {
        const char* n = pages[pi].name;

        if (strcmp(n, base) == 0
            || (strncmp(n, base, (size_t)bl) == 0 && strcmp(n + bl, ".md") == 0)) {
            return pi;
        }
    }

    return -1;
}

/* The topic an anchor on a read page leads to: its top for none; -1 if it
 * has no such anchor. */
static int anchor_topic(int pi, const char* frag)
{
    int k;

    if (!frag || !*frag) {
        return pages[pi].first_topic;
    }
    for (k = 0; k < nanchors; k++) {
        if (anchors[k].page == pi && strcmp(anchors[k].name, frag) == 0) {
            return anchors[k].topic;
        }
    }

    return -1;
}

static void unresolved(const char* file, int no, const char* href, int len)
{
    if (md_keep_going) {
        md_warn(file, no, "cannot resolve link '%.*s'", len, href);
    } else {
        md_error(file, no, "cannot resolve link '%.*s'", len, href);
    }
}

int resolve_link(const page* p, int no, const char* href, int len)
{
    static char path[TITLE_MAX];
    static char frag[TITLE_MAX];
    const char* hash = memchr(href, '#', (size_t)len);
    int plen = hash ? (int)(hash - href) : len;
    int flen = hash ? len - plen - 1 : 0;
    int pi = (int)(p - pages);
    int t;

    if (plen >= TITLE_MAX || flen >= TITLE_MAX) {
        plen = flen = 0;
    }
    memcpy(path, href, (size_t)plen);
    path[plen] = 0;
    memcpy(frag, hash ? hash + 1 : "", (size_t)flen);
    frag[flen] = 0;

    if (plen) {
        pi = page_named(path);
        if (pi < 0) {
            unresolved(p->path, no, href, len);

            return -1;
        }
    }
    if (pages[pi].done) {
        t = anchor_topic(pi, flen ? frag : NULL);
        if (t < 0) {
            unresolved(p->path, no, href, len);
        }

        return t;
    }

    /* Not read yet: settled at the end, as written. */
    if (nlinks == max_links) {
        max_links = max_links ? max_links * 2 : 64;
        links = md_realloc(links, (unsigned)max_links * (unsigned)sizeof(pending));
    }
    links[nlinks].page = pi;
    links[nlinks].frag = flen ? arena_strdup(frag) : NULL;
    links[nlinks].href = arena_strndup(href, (unsigned)len);
    links[nlinks].from = p->path;
    links[nlinks].no = no;
    links[nlinks].topic = NONE;
    nlinks++;

    return PENDING;
}

int last_pending(void)
{
    return nlinks - 1;
}

int resolve_pending(void)
{
    int k;

    for (k = 0; k < nlinks; k++) {
        pending* l = &links[k];
        int t = anchor_topic(l->page, l->frag);

        if (t < 0) {
            unresolved(l->from, l->no, l->href, (int)strlen(l->href));
            l->topic = NONE;
        } else {
            l->topic = (unsigned)t;
        }
    }

    return md_errors ? -1 : 0;
}

unsigned pending_topic(int link)
{
    return links[link].topic;
}

