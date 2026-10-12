/*
 * The topic being read, laid out into lines, and questions about it. See
 * doc.h. Every question walks a line's markup the same way: a link's start
 * is three bytes, controls take no room, and every other byte is a column.
 */
#include <string.h>

#include "doc.h"
#include "ui.h"

ref_line doc_lines[DOC_MAX_LINES];
int doc_nlines;

int doc_load(unsigned num, int width)
{
    int n = ref_view(&book, num);

    if (n < 0) {
        return -1;
    }
    doc_nlines = ref_layout(book.view, (unsigned)n, width, doc_lines, DOC_MAX_LINES);
    if (doc_nlines > DOC_MAX_LINES) {
        doc_nlines = DOC_MAX_LINES;
    }

    return 0;
}

static uint16_t link_target(const uint8_t* t, unsigned k)
{
    return (uint16_t)(t[k + 1] | t[k + 2] << 8);
}

int doc_first_col(int i)
{
    return doc_lines[i].x;
}

int doc_last_col(int i)
{
    const ref_line* ln = &doc_lines[i];
    const uint8_t* t = book.view;
    unsigned k;
    int n = 0;

    if (ln->kind == 0 || ln->kind == REF_RULE) {
        return ln->x;
    }
    for (k = ln->start; k < ln->end; k = REF_SKIP(t, k)) {
        if (t[k] >= 0x20) {
            n++;
        }
    }

    return ln->x + (n > 0 ? n - 1 : 0);
}

int doc_clamp(int i, int col)
{
    int last = doc_last_col(i);

    if (col > last) {
        col = last;
    }

    return col < doc_lines[i].x ? doc_lines[i].x : col;
}

/* --- links --------------------------------------------------------------------- */

/* The link at column col of line i, on that line alone: its target and the
 * column it starts at there (a link wrapped onto the line starts at the
 * line's first column). */
static int link_on_line(int i, int col, uint16_t* target, int* from)
{
    const ref_line* ln = &doc_lines[i];
    const uint8_t* t = book.view;
    unsigned k;
    int x = ln->x;
    uint16_t link = ln->link;
    int start = x;

    if (i < 0 || i >= doc_nlines || ln->kind == 0) {
        return 0;
    }
    for (k = ln->start; k < ln->end; k = REF_SKIP(t, k)) {
        if (t[k] == REF_LINK) {
            link = link_target(t, k);
            start = x;
        } else if (t[k] == REF_LINK_END) {
            if (link != REF_NONE && col >= start && col < x) {
                break;
            }
            link = REF_NONE;
        } else if (t[k] >= 0x20) {
            x++;
        }
    }
    if (link == REF_NONE || col < start || col >= x) {
        return 0;
    }
    *target = link;
    *from = start;

    return 1;
}

/* The column of the last link to target that opens on line i: the one a
 * link wrapped onto the next line started with. */
static int last_opening(int i, uint16_t target)
{
    const ref_line* ln = &doc_lines[i];
    const uint8_t* t = book.view;
    unsigned k;
    int x = ln->x;
    int at = ln->x;

    for (k = ln->start; k < ln->end; k = REF_SKIP(t, k)) {
        if (t[k] == REF_LINK && link_target(t, k) == target) {
            at = x;
        } else if (t[k] >= 0x20) {
            x++;
        }
    }

    return at;
}

int doc_link_at(int i, int col, doc_link* link)
{
    uint16_t target;
    int from;
    int first = i;
    int last = i;

    if (!link_on_line(i, col, &target, &from)) {
        return 0;
    }
    /* Back to the line it starts on, if it was wrapped onto this one. */
    if (from == doc_lines[i].x && doc_lines[i].link == target) {
        while (first > 0 && doc_lines[first].link == target) {
            first--;
        }
        from = last_opening(first, target);
    }
    /* On to the line it ends on. */
    while (last + 1 < doc_nlines && doc_lines[last + 1].link == target) {
        last++;
    }
    link->target = target;
    link->first = first;
    link->first_col = from;
    link->last = last;

    return 1;
}

/* The links that start on line i (not those wrapped onto it): the column
 * of each, up to max. How many. */
static int links_starting(int i, int* cols, int max)
{
    const ref_line* ln = &doc_lines[i];
    const uint8_t* t = book.view;
    unsigned k;
    int col = ln->x;
    int n = 0;

    for (k = ln->start; k < ln->end; k = REF_SKIP(t, k)) {
        if (t[k] == REF_LINK) {
            if (n < max) {
                cols[n++] = col;
            }
        } else if (t[k] >= 0x20) {
            col++;
        }
    }

    return n;
}

int doc_step_link(int dir, int* line, int* col)
{
    static int cols[24];
    doc_link here;
    int i = *line;
    int from = *col;

    /* On a link already: step from where it starts, so that it is not the
     * one found. */
    if (doc_link_at(i, from, &here)) {
        i = here.first;
        from = here.first_col;
    }
    for (; i >= 0 && i < doc_nlines; i += dir) {
        int n = links_starting(i, cols, (int)(sizeof cols / sizeof cols[0]));
        int k;

        if (dir > 0) {
            for (k = 0; k < n && cols[k] <= from; k++) {
            }
        } else {
            for (k = n - 1; k >= 0 && cols[k] >= from; k--) {
            }
        }
        if (k >= 0 && k < n) {
            *line = i;
            *col = cols[k];

            return 1;
        }
        from = dir > 0 ? -1 : 0x7fff;       /* every link on the next line */
    }

    return 0;
}

/* --- headings ----------------------------------------------------------------- */

int doc_heading(int i, int* first, int* last)
{
    if (i < 0 || i >= doc_nlines || doc_lines[i].kind != REF_HEAD) {
        return 0;
    }
    *first = i;
    while (*first > 0 && doc_lines[*first - 1].kind == REF_HEAD) {
        (*first)--;
    }
    *last = i;
    while (*last + 1 < doc_nlines && doc_lines[*last + 1].kind == REF_HEAD) {
        (*last)++;
    }

    return 1;
}

/* Does a heading start at line i? (A blank line always comes between two
 * headings, so HEAD lines in a row are one heading, wrapped.) */
static int heading_starts(int i)
{
    return doc_lines[i].kind == REF_HEAD && (i == 0 || doc_lines[i - 1].kind != REF_HEAD);
}

int doc_step_heading(int i, int dir)
{
    for (i += dir; i >= 0 && i < doc_nlines; i += dir) {
        if (heading_starts(i)) {
            return i;
        }
    }

    return -1;
}

/* --- finding -------------------------------------------------------------------- */

static int lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

/* Line i's characters, lower case, into buf: how many. */
static int line_text(int i, char* buf, int size)
{
    const ref_line* ln = &doc_lines[i];
    const uint8_t* t = book.view;
    unsigned k;
    int n = 0;

    for (k = ln->start; k < ln->end && n < size; k = REF_SKIP(t, k)) {
        if (t[k] >= 0x20) {
            buf[n++] = (char)lower(t[k]);
        }
    }

    return n;
}

int doc_find(const char* text, int dir, int line, int from, int* at_line, int* at)
{
    static char buf[512];
    static char want[64];
    int wl = (int)strlen(text);
    int k;
    int tries;

    if (wl == 0 || wl >= (int)sizeof want || doc_nlines == 0) {
        return 0;
    }
    for (k = 0; k <= wl; k++) {
        want[k] = (char)lower((unsigned char)text[k]);
    }
    for (tries = 0; tries <= doc_nlines; tries++) {
        int n = line_text(line, buf, (int)sizeof buf);
        int x;

        if (dir > 0) {
            for (x = from < 0 ? 0 : from; x + wl <= n; x++) {
                if (memcmp(buf + x, want, (size_t)wl) == 0) {
                    *at_line = line;
                    *at = x;

                    return 1;
                }
            }
            line = line + 1 < doc_nlines ? line + 1 : 0;
            from = 0;
        } else {
            for (x = from > n - wl ? n - wl : from; x >= 0; x--) {
                if (memcmp(buf + x, want, (size_t)wl) == 0) {
                    *at_line = line;
                    *at = x;

                    return 1;
                }
            }
            line = line > 0 ? line - 1 : doc_nlines - 1;
            from = 0x7fff;
        }
    }

    return 0;
}
