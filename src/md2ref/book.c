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
#include <stdlib.h>
#include <string.h>

#include "md2ref.h"

#define TITLE_MAX 256
#define DEPTH     8

topic* topic_block[(MAX_TOPICS + 1 + TOPIC_BLOCK - 1) / TOPIC_BLOCK];
int ntopics;
const char* book_title;

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

/* The anchors by page and name, through a hash: each anchor added and
 * each link resolved looks at the few that share its hash, not at all of
 * them. Slots hold an anchor's index + 1, 0 for none; there are always at
 * least twice as many as anchors. */
static int* anchor_slot;
static unsigned anchor_slots;               /* a power of two */

static unsigned anchor_hash(int page, const char* name)
{
    return md_hash((unsigned)page, name, (unsigned)strlen(name));
}

/* The anchor's slot: where it is, or the empty slot it would go in. */
static unsigned anchor_find(int page, const char* name)
{
    unsigned mask = anchor_slots - 1;
    unsigned h = anchor_hash(page, name) & mask;

    while (anchor_slot[h]) {
        const anchor* a = &anchors[anchor_slot[h] - 1];

        if (a->page == page && strcmp(a->name, name) == 0) {
            break;
        }
        h = (h + 1) & mask;
    }

    return h;
}

static void more_anchor_slots(void)
{
    int k;

    anchor_slots = anchor_slots ? anchor_slots * 2 : 512;
    free(anchor_slot);
    anchor_slot = md_realloc(NULL, anchor_slots * (unsigned)sizeof(int));
    memset(anchor_slot, 0, anchor_slots * sizeof(int));
    for (k = 0; k < nanchors; k++) {
        anchor_slot[anchor_find(anchors[k].page, anchors[k].name)] = k + 1;
    }
}

static void add_anchor(int page, const char* name, int t)
{
    unsigned h;

    if ((unsigned)(nanchors + 1) * 2 > anchor_slots) {
        more_anchor_slots();
    }
    h = anchor_find(page, name);
    if (anchor_slot[h]) {
        return;                             /* the first keeps it */
    }
    if (nanchors == max_anchors) {
        max_anchors = max_anchors ? max_anchors * 2 : 256;
        anchors = md_realloc(anchors, (unsigned)max_anchors * (unsigned)sizeof(anchor));
    }
    anchors[nanchors].page = page;
    anchors[nanchors].name = name;
    anchors[nanchors].topic = t;
    nanchors++;
    anchor_slot[h] = nanchors;
}

static int has_anchor(int page, const char* name)
{
    return anchor_slots && anchor_slot[anchor_find(page, name)] != 0;
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
/* The page being read: each of its topics' {#id}, by its place in the
 * page, wanted only for the page's anchors when it is done. */
static const char** page_id;
static int npage_id;
static int max_page_id;

/* Room for one more in page_id. */
static void page_id_room(void)
{
    if (npage_id == max_page_id) {
        max_page_id = max_page_id ? max_page_id * 2 : 64;
        page_id = md_realloc(page_id, (unsigned)max_page_id * (unsigned)sizeof(char*));
    }
}

static void page_id_add(int k, const char* id)
{
    (void)k;                        /* always the next: npage_id */
    page_id_room();
    page_id[npage_id++] = id;
}

/* A block for topic ntopics, if it is the first of one. */
static void more_topics(void)
{
    if (!topic_block[(unsigned)ntopics >> 8]) {
        topic_block[(unsigned)ntopics >> 8] = md_realloc(NULL, TOPIC_BLOCK * (unsigned)sizeof(topic));
    }
}

static int new_topic(const char* title, const block* b, int page, int level, int parent)
{
    topic* t;

    if (ntopics >= MAX_TOPICS) {
        md_error(pages[page].path, b ? b->no : 0, "too many topics (at most %d)", MAX_TOPICS);

        return -1;
    }
    more_topics();
    t = &TOPIC(ntopics);
    memset(t, 0, sizeof *t);
    t->title = title;
    t->heading = b != NULL;
    page_id_add(ntopics - pages[page].first_topic, b && b->id ? arena_strdup(b->id) : NULL);
    t->page = (uint8_t)page;
    t->made = made++;
    keys_topic(t->made, title, b ? b->text : NULL);
    t->level = level;
    t->parent = parent;
    t->first_child = t->last_child = t->next = -1;
    if (parent >= 0) {
        topic* p = &TOPIC(parent);

        if (p->last_child >= 0) {
            TOPIC(p->last_child).next = ntopics;
        } else {
            p->first_child = ntopics;
        }
        p->last_child = ntopics;
        p->nchildren++;
    } else {
        if (last_root >= 0) {
            TOPIC(last_root).next = ntopics;
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
    npage_id = 0;
    pages[pi].patches_from = patch_count();
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
    more_topics();
    for (k = ntopics; k > f; k--) {
        TOPIC(k) = TOPIC(k - 1);            /* the page's, one up: blocks are apart */
    }
    ntopics++;
    /* Only the page's topics move, and only they refer to one another:
     * those before refer to earlier topics but for the root before the
     * page's, whose next is f -- the new top now, as it should be. */
    for (k = f + 1; k < ntopics; k++) {
        shift(&TOPIC(k).parent, f);
        shift(&TOPIC(k).first_child, f);
        shift(&TOPIC(k).last_child, f);
        shift(&TOPIC(k).next, f);
    }
    for (k = 0; k < nids; k++) {
        if (ids[k].topic >= f) {
            ids[k].topic++;
        }
    }
    page_id_room();                         /* the new top is the page's first: no id */
    memmove(page_id + 1, page_id, (unsigned)npage_id * sizeof(char*));
    page_id[0] = NULL;
    npage_id++;
    memset(&TOPIC(f), 0, sizeof(topic));
    TOPIC(f).title = title_from_name(pages[pi].name);
    TOPIC(f).page = (uint8_t)pi;
    TOPIC(f).made = made++;
    keys_topic(TOPIC(f).made, TOPIC(f).title, NULL);
    TOPIC(f).level = 0;
    TOPIC(f).parent = -1;
    TOPIC(f).first_child = TOPIC(f).last_child = f + 1;
    TOPIC(f).nchildren = 1;
    TOPIC(f).next = -1;
    TOPIC(f + 1).parent = f;
    TOPIC(f + 1).next = -1;
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
    while (depth > 0 && TOPIC(stack[depth - 1]).level >= b->level) {
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
static void settle(int pi);

void book_page_done(int pi)
{
    page* p = &pages[pi];
    int t;
    int k;

    for (t = p->first_topic; t < p->first_topic + p->ntopics; t++) {
        const char* id = page_id[t - p->first_topic];

        if (id) {
            add_anchor(pi, id, t);
        }
    }
    for (t = p->first_topic; t < p->first_topic + p->ntopics; t++) {
        char* base;
        char* a;
        int n = 1;

        if (!TOPIC(t).heading || page_id[t - p->first_topic]) {
            continue;                       /* no heading, or its {#id} is its anchor */
        }
        base = slugify(TOPIC(t).title);
        a = base;
        while (has_anchor(pi, a)) {
            a = arena_alloc((unsigned)strlen(base) + 8);
            sprintf(a, "%s_%d", base, n++);
        }
        add_anchor(pi, a, t);
    }
    for (k = 0; k < nids; k++) {
        add_anchor(pi, ids[k].name, ids[k].topic);
    }
    nids = 0;
    p->done = 1;
    settle(pi);
}

void number_topics(void)
{
    int t;

    final_of = md_realloc(NULL, (unsigned)(made + 1) * (unsigned)sizeof(int));
    for (t = 0; t < ntopics; t++) {
        final_of[TOPIC(t).made] = t;
    }
}

int book_done(const char* title)
{
    book_title = title ? agon_string(title) : ntopics ? TOPIC(0).title : "";

    return ntopics ? 0 : -1;
}

/* --- links ------------------------------------------------------------------- */

/* A link to a page not yet read, kept only until that page is read --
 * then its topic goes into its patches, as acc settles a fixup as soon as
 * its symbol is known, and it is freed. What it names and where it is
 * wait on the card, in the links file, as a record: its href's length,
 * where the anchor starts in it (LINK_TOP for none), the page it is on and
 * the line, then the href. In memory, only what links it to the rest. In
 * blocks, which never move, with the free ones in a list. */
#define LINK_HEAD  8
#define LINK_TOP   0xFFFF
#define LINK_WIN   512                  /* the links file, read back a window at a time */

typedef struct {
    unsigned long at;       /* its record, in the links file */
    int patch;              /* its last placeholder's patch, or -1 until written: a
                             * link in a table's cell is written on each line it wraps
                             * to, and each patch leads to the one before */
    int next;               /* the next waiting for the same page, or the next free */
} pending;

#define LINK_BLOCK 64
#define MAX_LINK_BLOCKS 256

static pending* link_block[MAX_LINK_BLOCKS];
static int nlink_records;
static int free_link = -1;
static int last_link = -1;

#define LINK(k) (link_block[(unsigned)(k) / LINK_BLOCK][(unsigned)(k) % LINK_BLOCK])

/* A record for a link: a free one, or the next of a block. -1 if there
 * are as many waiting as there may be. */
static int new_link(void)
{
    int k = free_link;

    if (k >= 0) {
        free_link = LINK(k).next;

        return k;
    }
    if (nlink_records == LINK_BLOCK * MAX_LINK_BLOCKS) {
        return -1;
    }
    if (!link_block[nlink_records / LINK_BLOCK]) {
        link_block[nlink_records / LINK_BLOCK] = md_realloc(NULL, LINK_BLOCK * (unsigned)sizeof(pending));
    }

    return nlink_records++;
}

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
    if (!anchor_slots || !(k = anchor_slot[anchor_find(pi, frag)])) {
        return -1;
    }

    return anchors[k - 1].topic;
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

    /* Not read yet: settled when it is, waiting with that page. */
    t = new_link();
    if (t < 0) {
        md_error(p->path, no, "too many links to pages not read yet (at most %d)",
                 LINK_BLOCK * MAX_LINK_BLOCKS);

        return -1;
    }
    {
        uint8_t head[LINK_HEAD];
        unsigned frag_at = flen ? (unsigned)plen + 1 : LINK_TOP;

        head[0] = (uint8_t)((unsigned)len & 0xFF);
        head[1] = (uint8_t)((unsigned)len >> 8);
        head[2] = (uint8_t)(frag_at & 0xFF);
        head[3] = (uint8_t)(frag_at >> 8);
        head[4] = (uint8_t)(p - pages);
        head[5] = (uint8_t)((unsigned)no & 0xFF);
        head[6] = (uint8_t)((unsigned)no >> 8);
        head[7] = (uint8_t)((unsigned)no >> 16);
        LINK(t).at = links_put(head, LINK_HEAD);
        links_put((const uint8_t*)href, (unsigned)len);
    }
    LINK(t).patch = -1;
    LINK(t).next = pages[pi].waiting;
    pages[pi].waiting = t;
    last_link = t;

    return PENDING;
}

int last_pending(void)
{
    return last_link;
}

void link_placed(int link, unsigned k)
{
    if (LINK(link).patch >= 0) {
        set_patch(k, PATCH_CHAIN + (unsigned)LINK(link).patch);
    }
    LINK(link).patch = (int)k;
}

/* Page pi read: each link waiting for it settled, its topic into its
 * patch, and freed; then the page's own placeholders, its menus' links to
 * its topics, whose numbers are final now -- a page read later puts its
 * topics after these. */
static void settle(int pi)
{
    static uint8_t win[LINK_WIN];
    unsigned long win_at = 0;
    unsigned win_len = 0;
    char* href = NULL;
    unsigned href_cap = 0;
    page* p = &pages[pi];
    int k = p->waiting;
    unsigned j;
    int* at_made;
    int m0 = p->ntopics ? TOPIC(p->first_topic).made : 0;
    int t;
    int back = -1;

    /* In the order they were written -- the list is newest first -- so that
     * those near one another in the file are read a window at a time: each
     * read of the card is a call to MOS, and a page's links come together. */
    while (k >= 0) {
        int next = LINK(k).next;

        LINK(k).next = back;
        back = k;
        k = next;
    }
    k = back;
    while (k >= 0) {
        pending* l = &LINK(k);
        int next = l->next;
        uint8_t head[LINK_HEAD];
        unsigned len;
        unsigned frag_at;

        if (l->at < win_at || l->at + LINK_HEAD > win_at + win_len) {
            win_at = l->at;
            win_len = links_read(win_at, win, LINK_WIN);
        }
        memcpy(head, win + (l->at - win_at), LINK_HEAD);
        len = head[0] | (unsigned)head[1] << 8;
        frag_at = head[2] | (unsigned)head[3] << 8;
        if (len + 1 > href_cap) {
            href_cap = len + 1;
            href = md_realloc(href, href_cap);
        }
        if (l->at + LINK_HEAD + len > win_at + win_len && LINK_HEAD + len <= LINK_WIN) {
            win_at = l->at;
            win_len = links_read(win_at, win, LINK_WIN);
        }
        if (l->at + LINK_HEAD + len <= win_at + win_len) {
            memcpy(href, win + (l->at + LINK_HEAD - win_at), len);
        } else {
            links_read(l->at + LINK_HEAD, (uint8_t*)href, len);     /* longer than the window */
        }
        href[len] = 0;
        t = anchor_topic(pi, frag_at != LINK_TOP && frag_at <= len ? href + frag_at : NULL);
        if (t < 0) {
            unresolved(pages[head[4]].path,
                       (int)(head[5] | (unsigned)head[6] << 8 | (unsigned)head[7] << 16), href, (int)len);
        }
        for (j = (unsigned)l->patch; l->patch >= 0;) {
            unsigned v = patch_value(j);

            set_patch(j, t < 0 ? NONE : (unsigned)t);
            if (v < PATCH_CHAIN) {
                break;
            }
            j = v - (unsigned)PATCH_CHAIN;
        }
        l->next = free_link;
        free_link = k;
        k = next;
    }
    p->waiting = -1;
    free(href);
    if (links_failed()) {
        md_error("md2ref", 0, "the links file could not be written or read back");
    }

    /* A topic's number as made, to its number: the page's were made one
     * after another, from m0. */
    for (t = p->first_topic; t < p->first_topic + p->ntopics; t++) {
        if (TOPIC(t).made < m0) {
            m0 = TOPIC(t).made;
        }
    }
    at_made = md_realloc(NULL, (unsigned)(p->ntopics + 1) * (unsigned)sizeof(int));
    for (t = p->first_topic; t < p->first_topic + p->ntopics; t++) {
        at_made[TOPIC(t).made - m0] = t;
    }
    for (j = p->patches_from; j < patch_count(); j++) {
        unsigned v = patch_value(j);

        if (v >= PATCH_MADE && v < PATCH_CHAIN) {      /* not another page's links */
            unsigned made_no = v - (unsigned)PATCH_MADE;

            set_patch(j, made_no >= (unsigned)m0 && made_no - (unsigned)m0 < (unsigned)p->ntopics
                         ? (unsigned)at_made[made_no - (unsigned)m0] : NONE);
        }
    }
    free(at_made);
}

int resolve_pending(void)
{
    return md_errors ? -1 : 0;
}

