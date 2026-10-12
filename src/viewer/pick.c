/*
 * Lists to choose a topic from: the matches of a lookup, the results of a
 * search, and search as you type -- each key typed narrows the list, keys
 * first, then the full-text results. Enter opens the chosen topic.
 */
#include <string.h>

#include "script.h"
#include "ui.h"

#define MAX_ROWS  160
#define KEY_ROWS  40
#define HIT_ROWS  120
#define QUERY_MAX 60

typedef struct {
    const char* label;      /* a key, or NULL */
    uint16_t topic;
} row;

static row rows[MAX_ROWS];
static int nrows;
static int total;           /* how many there were, before the list was cut */
static int cursor;
static int top;

static ref_match keys[KEY_ROWS];
static ref_hit hits[HIT_ROWS];
static char query[QUERY_MAX + 1];

/* The page a topic is on: its topmost ancestor. */
static unsigned page_of(unsigned t)
{
    ref_topic tp;

    for (;;) {
        ref_get_topic(&book, t, &tp);
        if (tp.parent == REF_NONE) {
            return t;
        }
        t = tp.parent;
    }
}

static void draw_row(int first, int r, int input)
{
    int i = top + r;
    const row* w;
    unsigned page;

    scr_begin(first + r);
    if (i >= nrows) {
        if (i == 0 && input && query[0]) {
            scr_colour(C_DIM);
            scr_adds("  nothing found");
        }
        scr_end();

        return;
    }
    w = &rows[i];
    scr_colour(i == cursor ? C_SEL : C_TEXT);
    scr_add(' ');
    if (w->label) {
        scr_colour(i == cursor ? C_SEL : C_CODE);
        scr_adds(w->label);
        scr_colour(i == cursor ? C_SEL : C_DIM);
        scr_adds("  ");
    }
    scr_colour(i == cursor ? C_SEL : C_TEXT);
    scr_adds(ui_title(w->topic));
    page = page_of(w->topic);
    if (page != w->topic) {
        scr_colour(C_DIM);
        scr_adds("  - ");
        scr_adds(ui_title(page));
    }
    scr_colour(C_TEXT);
    scr_end();
}

static void log_state(void)
{
    LOG("pick row %d of %d top %d query '%s'", cursor, nrows, top, query);
}

static void draw(const char* heading, int first, int input)
{
    int r;
    int shown = scr_rows - first;

    if (input) {
        scr_begin(0);
        scr_colour(C_HEAD);
        scr_adds("Find: ");
        scr_colour(C_TEXT);
        scr_adds(query);
        scr_end();
    }
    for (r = 0; r < shown; r++) {
        draw_row(first, r, input);
    }
    scr_begin(scr_rows);
    scr_colour(C_STATUS);
    scr_add(' ');
    scr_adds(heading);
    if (total > nrows) {
        scr_adds("  (the first of many)");
    }
    scr_end();
    if (input) {
        scr_cursor(6 + (int)strlen(query), 0, 1);
    }
    log_state();
}

/* The cursor moved, nothing else: scroll the list if its top moved a
 * little, and draw only the rows that changed. */
static void moved(const char* heading, int first, int input, int was_top, int was_cursor)
{
    int shown = scr_rows - first;
    int d = top - was_top;
    int r;

    if (d > SCROLL_MAX || d < -SCROLL_MAX) {
        draw(heading, first, input);

        return;
    }
    scr_scroll(first, d);
    if (d > 0) {
        for (r = shown - d; r < shown; r++) {
            draw_row(first, r, input);
        }
    } else {
        for (r = 0; r < -d; r++) {
            draw_row(first, r, input);
        }
    }
    if (was_cursor - top >= 0 && was_cursor - top < shown) {
        draw_row(first, was_cursor - top, input);
    }
    draw_row(first, cursor - top, input);
    if (input) {
        scr_cursor(6 + (int)strlen(query), 0, 1);
    }
    log_state();
}

static void keep_visible(int shown)
{
    if (cursor >= nrows) {
        cursor = nrows - 1;
    }
    if (cursor < 0) {
        cursor = 0;
    }
    if (cursor < top) {
        top = cursor;
    }
    if (cursor >= top + shown) {
        top = cursor - shown + 1;
    }
}

/* Moves the cursor for a key; 1 if it was one that moves it. */
static int move(int m, int shown)
{
    switch (m) {
    case K_UP:
        cursor--;
        break;
    case K_DOWN:
        cursor++;
        break;
    case K_PGUP:
        cursor -= shown - 1;
        break;
    case K_PGDN:
        cursor += shown - 1;
        break;
    case K_HOME:
        cursor = 0;
        break;
    case K_END:
        cursor = nrows - 1;
        break;
    default:
        return 0;
    }
    keep_visible(shown);

    return 1;
}

/* The last list chosen from, kept for Backspace: find reuses rows. */
static row list_rows[MAX_ROWS];
static int list_n;
static int list_total;
static int list_cursor;
static int list_top;
static char list_heading[80];
static int find_cursor;
static int find_top;

static int choose_at(const char* heading, int at, int at_top);

static int choose(const char* heading)
{
    memcpy(list_rows, rows, (size_t)nrows * sizeof rows[0]);
    list_n = nrows;
    list_total = total;
    strncpy(list_heading, heading, sizeof list_heading - 1);

    return choose_at(heading, 0, 0);
}

void pick_reset(void)
{
    nrows = 0;
    list_n = 0;
    query[0] = 0;
    find_cursor = 0;
    find_top = 0;
}

int pick_again(void)
{
    if (list_n == 0) {
        return -1;
    }
    memcpy(rows, list_rows, (size_t)list_n * sizeof rows[0]);
    nrows = list_n;
    total = list_total;

    return choose_at(list_heading, list_cursor, list_top);
}

static int choose_at(const char* heading, int at, int at_top)
{
    cursor = at;
    top = at_top;
    scr_cls();
    draw(heading, 0, 0);
    for (;;) {
        key k = in_wait();
        int m = in_meaning(k);
        int was_top = top;
        int was_cursor = cursor;

        if (move(m, scr_rows)) {
            moved(heading, 0, 0, was_top, was_cursor);
            continue;
        }
        if (m == K_ENTER && nrows > 0) {
            list_cursor = cursor;
            list_top = top;

            return rows[cursor].topic;
        }
        if (m == K_ESC || (m == K_CHAR && k.ch == 'q')) {
            return -1;
        }
    }
}

int pick_topics(const char* heading, const ref_match* m, int n, int all, int labels)
{
    int i;

    nrows = 0;
    for (i = 0; i < n && nrows < MAX_ROWS; i++) {
        rows[nrows].label = labels ? m[i].key : NULL;
        rows[nrows].topic = m[i].topic;
        nrows++;
    }
    total = all;

    return choose(heading);
}

int pick_hits(const char* heading, const ref_hit* h, int n, int all)
{
    int i;

    nrows = 0;
    for (i = 0; i < n && nrows < MAX_ROWS; i++) {
        rows[nrows].label = NULL;
        rows[nrows].topic = h[i].topic;
        nrows++;
    }
    total = all;

    return choose(heading);
}

/* The list for what has been typed: keys that start with it, then the
 * search's results, each topic once. */
static void refresh(void)
{
    int nk;
    int nh;
    int i;

    nrows = 0;
    total = 0;
    if (!query[0]) {
        return;
    }
    nk = ref_keys_from(&book, query, keys, KEY_ROWS);
    for (i = 0; i < nk && i < KEY_ROWS && nrows < MAX_ROWS; i++) {
        rows[nrows].label = keys[i].key;
        rows[nrows].topic = keys[i].topic;
        nrows++;
    }
    nh = ref_search(&book, query, hits, HIT_ROWS);
    for (i = 0; i < nh && i < HIT_ROWS && nrows < MAX_ROWS; i++) {
        int j;
        int seen = 0;

        for (j = 0; j < nrows; j++) {
            if (rows[j].topic == hits[i].topic) {
                seen = 1;
                break;
            }
        }
        if (!seen) {
            rows[nrows].label = NULL;
            rows[nrows].topic = hits[i].topic;
            nrows++;
        }
    }
    total = nk + nh;
}

int pick_find(const char* text)
{
    static const char HEADING[] = "Find   type to narrow   Enter: open   Esc: back";
    int len;

    /* NULL: resumed, by Backspace, with what was typed and where. */
    if (text) {
        strncpy(query, text, QUERY_MAX);
        query[QUERY_MAX] = 0;
        find_cursor = 0;
        find_top = 0;
    }
    refresh();
    cursor = find_cursor < nrows ? find_cursor : 0;
    top = find_top <= cursor ? find_top : cursor;
    scr_cls();
    draw(HEADING, 1, 1);
    for (;;) {
        key k = in_wait();
        int m = in_meaning(k);
        int changed = 0;
        int was_top = top;
        int was_cursor = cursor;

        len = (int)strlen(query);
        if (move(m, scr_rows - 1)) {
            moved(HEADING, 1, 1, was_top, was_cursor);
            continue;
        }
        if (m == K_ENTER) {
            if (nrows > 0) {
                scr_cursor(0, 0, 0);
                find_cursor = cursor;
                find_top = top;

                return rows[cursor].topic;
            }
            continue;
        }
        if (m == K_ESC) {
            scr_cursor(0, 0, 0);

            return -1;
        }
        if (m == K_BACK && len > 0) {
            query[len - 1] = 0;
            changed = 1;
        } else if (m == K_CHAR && len < QUERY_MAX) {
            query[len] = (char)k.ch;
            query[len + 1] = 0;
            changed = 1;
        }
        /* Keys typed faster than a search runs are all taken before it. */
        if (changed && !in_pending()) {
            cursor = 0;
            top = 0;
            refresh();
            draw(HEADING, 1, 1);
        } else if (changed) {
            scr_begin(0);
            scr_colour(C_HEAD);
            scr_adds("Find: ");
            scr_colour(C_TEXT);
            scr_adds(query);
            scr_end();
        }
    }
}
