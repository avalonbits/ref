/*
 * The contents: the nav's sections, their pages and the headings under
 * them, as a tree that opens and closes. Enter on a topic opens it; Esc
 * goes back to the topic it was opened from, or quits if there is none.
 * CTRL+F finds words in the titles, closed sections' too, as a topic
 * finds them in its text; CTRL+N and CTRL+P the next and the one before.
 */
#include <stdlib.h>
#include <string.h>

#include "script.h"
#include "ui.h"

#ifdef ZMALLOC
#include "zmalloc.h"
#endif

typedef struct {
    uint32_t title;
    uint16_t topic;
    uint16_t child;
    uint16_t next;
    uint16_t parent;
    uint8_t depth;
    uint8_t open;
} node;

static node* nodes;
static uint16_t* rows;              /* the nodes shown, in order */
static int nrows;
static int cursor;                  /* a row */
static int top;

void toc_reset(void)
{
    free(nodes);
    free(rows);
    nodes = NULL;
    rows = NULL;
    nrows = 0;
}

static int load(void)
{
    unsigned k;

    if (nodes) {
        return 0;
    }
#ifdef ZMALLOC
    z_site = "the contents";
#endif
    nodes = malloc(book.nnodes * sizeof *nodes);
    rows = malloc(book.nnodes * sizeof *rows);
    if (!nodes || !rows) {
        return -1;
    }
    for (k = 0; k < book.nnodes; k++) {
        ref_node n;

        if (ref_get_node(&book, k, &n) != 0) {
            return -1;
        }
        nodes[k].title = n.title;
        nodes[k].topic = n.topic;
        nodes[k].child = n.child;
        nodes[k].next = n.next;
        nodes[k].parent = REF_NONE;
        nodes[k].depth = 0;
        nodes[k].open = 0;
    }
    /* Nodes are in preorder: a parent comes before its children. */
    for (k = 0; k < book.nnodes; k++) {
        unsigned c;

        for (c = nodes[k].child; c != REF_NONE && c < book.nnodes; c = nodes[c].next) {
            nodes[c].parent = (uint16_t)k;
            nodes[c].depth = (uint8_t)(nodes[k].depth + 1);
        }
    }
    /* The sections start open, showing their pages. */
    for (k = 0; k != REF_NONE && k < book.nnodes; k = nodes[k].next) {
        nodes[k].open = 1;
    }

    return 0;
}

static void walk(unsigned k)
{
    for (; k != REF_NONE && k < book.nnodes; k = nodes[k].next) {
        rows[nrows++] = (uint16_t)k;
        if (nodes[k].open) {
            walk(nodes[k].child);
        }
    }
}

static void rebuild(void)
{
    nrows = 0;
    walk(0);
}

static int row_of(unsigned k)
{
    int r;

    for (r = 0; r < nrows; r++) {
        if (rows[r] == k) {
            return r;
        }
    }

    return 0;
}

/* Show the node for topic: its ancestors open, the cursor on it. */
static void reveal(int topic)
{
    unsigned k;

    for (k = 0; k < book.nnodes; k++) {
        if (nodes[k].topic == (uint16_t)topic) {
            unsigned p;

            for (p = nodes[k].parent; p != REF_NONE; p = nodes[p].parent) {
                nodes[p].open = 1;
            }
            rebuild();
            cursor = row_of(k);

            return;
        }
    }
    rebuild();
}

/* --- finding ------------------------------------------------------------------- */

static int lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

/* Whether node k's title has the text, ignoring case. */
static int has_text(unsigned k, const char* want, unsigned wl)
{
    const char* t = ui_title_at(nodes[k].title);
    unsigned n = (unsigned)strlen(t);
    unsigned x;

    for (x = 0; x + wl <= n; x++) {
        unsigned j;

        for (j = 0; j < wl && lower((unsigned char)t[x + j]) == want[j]; j++) {
        }
        if (j == wl) {
            return 1;
        }
    }

    return 0;
}

/* ui_find_text in the titles, in the contents' order, all round: from the
 * cursor's node itself (dir 0, a new search), or from the one after (1)
 * or before (-1). The first found is shown -- the sections it is in
 * opened -- with the cursor on it: 1. Else "Not found", and 0. */
static int find(int dir)
{
    static char want[UI_FIND_MAX + 1];
    unsigned wl = (unsigned)strlen(ui_find_text);
    unsigned n = book.nnodes;
    unsigned from = nrows ? rows[cursor] : 0;
    unsigned i;
    unsigned j;

    for (j = 0; j <= wl; j++) {
        want[j] = (char)lower((unsigned char)ui_find_text[j]);
    }
    for (i = 0; wl && i < n; i++) {
        unsigned step = dir == 0 ? i : i + 1;
        unsigned k = dir < 0 ? (from + n - step % n) % n : (from + step) % n;

        if (has_text(k, want, wl)) {
            unsigned p;

            for (p = nodes[k].parent; p != REF_NONE; p = nodes[p].parent) {
                nodes[p].open = 1;
            }
            rebuild();
            cursor = row_of(k);
            LOG("find '%s' in the contents at node %u", ui_find_text, k);

            return 1;
        }
    }
    ui_not_found();

    return 0;
}

static void keep_visible(void)
{
    if (cursor < 0) {
        cursor = 0;
    }
    if (cursor >= nrows) {
        cursor = nrows - 1;
    }
    if (cursor < top) {
        top = cursor;
    }
    if (cursor >= top + scr_rows) {
        top = cursor - scr_rows + 1;
    }
}

static void draw_row(int r)
{
    int i = top + r;
    const node* n;
    int d;

    scr_begin(r);
    if (i >= nrows) {
        scr_end();

        return;
    }
    n = &nodes[rows[i]];
    scr_colour(C_TEXT);
    for (d = 0; d < n->depth; d++) {
        scr_adds("  ");
    }
    scr_colour(C_DIM);
    scr_add(n->child == REF_NONE ? ' ' : n->open ? '-' : '+');
    scr_add(' ');
    scr_colour(i == cursor ? C_SEL : n->topic == REF_NONE ? C_HEAD : C_TEXT);
    scr_adds(ui_title_at(n->title));
    scr_colour(C_TEXT);
    scr_end();
}

static void log_state(void)
{
    LOG("toc row %d node %d top %d rows %d", cursor, nrows ? rows[cursor] : -1, top, nrows);
}

static void draw(void)
{
    int r;

    for (r = 0; r < scr_rows; r++) {
        draw_row(r);
    }
    scr_begin(scr_rows);
    scr_colour(C_STATUS);
    scr_adds(" Contents   Enter: open   Right/Left: show/hide   Ctrl+F: find   Esc: back");
    scr_end();
    log_state();
}

/* The cursor moved from row was_cursor with the list's top at was_top, and
 * nothing else changed: scroll if the top moved a little, and draw only the
 * rows that changed. */
static void moved(int was_top, int was_cursor)
{
    int d = top - was_top;
    int r;

    if (d > SCROLL_MAX || d < -SCROLL_MAX) {
        draw();

        return;
    }
    scr_scroll(0, d);
    if (d > 0) {
        for (r = scr_rows - d; r < scr_rows; r++) {
            draw_row(r);
        }
    } else {
        for (r = 0; r < -d; r++) {
            draw_row(r);
        }
    }
    if (was_cursor - top >= 0 && was_cursor - top < scr_rows) {
        draw_row(was_cursor - top);
    }
    draw_row(cursor - top);
    log_state();
}

int toc_loop(int topic)
{
    if (load() != 0) {
        ui_message("The contents could not be read.");

        return TOC_BACK;
    }
    top = 0;
    cursor = 0;
    if (topic >= 0) {
        reveal(topic);
    } else {
        rebuild();
    }
    keep_visible();
    scr_cls();
    draw();
    for (;;) {
        key k = in_wait();
        int m = in_meaning(k);
        node* n = &nodes[rows[cursor]];
        int was_top = top;
        int was_cursor = cursor;
        int shape = 0;              /* the tree opened or closed: redraw it all */

        switch (m) {
        case K_UP:
            cursor--;
            break;
        case K_DOWN:
            cursor++;
            break;
        case K_PGUP:
            cursor -= scr_rows - 1;
            break;
        case K_PGDN:
            cursor += scr_rows - 1;
            break;
        case K_HOME:
            cursor = 0;
            break;
        case K_END:
            cursor = nrows - 1;
            break;
        case K_RIGHT:
            if (n->child != REF_NONE && !n->open) {
                n->open = 1;
                rebuild();
                shape = 1;
            } else if (n->child != REF_NONE) {
                cursor++;                   /* into it */
            }
            break;
        case K_LEFT:
            if (n->open) {
                n->open = 0;
                rebuild();
                shape = 1;
            } else if (n->parent != REF_NONE) {
                cursor = row_of(n->parent);
            }
            break;
        case K_ENTER:
            if (n->topic != REF_NONE) {
                return n->topic;
            }
            n->open = !n->open;
            rebuild();
            shape = 1;
            break;
        case K_FIND:
            return TOC_FIND;
        case K_FIND_HERE:
            if (ui_find_prompt()) {
                find(0);
            }
            shape = 1;                      /* the prompt, or what was found */
            break;
        case K_FIND_NEXT:
        case K_FIND_PREV:
            if (ui_find_text[0]) {
                find(m == K_FIND_NEXT ? 1 : -1);
                shape = 1;
            }
            break;
        case K_ESC:
            return TOC_BACK;
        case K_CHAR:
            if (k.ch == 'q') {
                return TOC_QUIT;
            }
            if (k.ch == '/') {
                return TOC_FIND;
            }
            if (k.ch == 't' || k.ch == 'c') {
                return TOC_BACK;
            }
            break;
        }
        keep_visible();
        if (shape) {
            draw();
        } else if (top != was_top || cursor != was_cursor) {
            moved(was_top, was_cursor);
        }
    }
}
