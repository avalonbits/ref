/*
 * A topic on the screen: its text laid out to the width, scrolled a line,
 * a page or sideways (for code and tables wider than the screen), its links
 * chosen with Tab and followed with Enter, and a history to go back and
 * forward through.
 */
#include <string.h>

#include "ui.h"

#define MAX_LINES 2000
#define HISTORY   32
#define HSTEP     8

typedef struct {
    uint16_t topic;
    uint16_t top;
} place;

static ref_line lines[MAX_LINES];
static int nlines;
static int cur = -1;
static int top;
static int hscroll;
static int sel = -1;                /* the selected link: its line, or -1 */
static int sel_col;                 /* and the column it starts at there */
static uint16_t sel_target;

static place back[HISTORY];
static int nback;
static place fwd[HISTORY];
static int nfwd;

int view_current(void)
{
    return cur;
}

int view_has(void)
{
    return cur >= 0;
}

static int load(unsigned num)
{
    int n = ref_view(&book, num);

    if (n < 0) {
        return -1;
    }
    nlines = ref_layout(book.view, (unsigned)n, scr_cols, lines, MAX_LINES);
    if (nlines > MAX_LINES) {
        nlines = MAX_LINES;
    }
    cur = (int)num;
    top = 0;
    hscroll = 0;
    sel = -1;

    return 0;
}

static void push(place* stack, int* n, place p)
{
    if (*n == HISTORY) {
        memmove(stack, stack + 1, (HISTORY - 1) * sizeof *stack);
        (*n)--;
    }
    stack[(*n)++] = p;
}

int view_open(unsigned num)
{
    if (cur >= 0) {
        place p;

        p.topic = (uint16_t)cur;
        p.top = (uint16_t)top;
        push(back, &nback, p);
        nfwd = 0;
    }

    return load(num);
}

/* --- drawing ------------------------------------------------------------------ */

/* Is the link that starts this line, carried from the line before, the
 * selected one? It is when every line back to the selection carries it. */
static int carried_selected(int idx)
{
    int j;

    if (sel < 0 || idx <= sel) {
        return 0;
    }
    for (j = sel + 1; j <= idx; j++) {
        if (lines[j].link != sel_target) {
            return 0;
        }
    }

    return 1;
}

static void draw_line(int row, int idx)
{
    const ref_line* ln;
    const uint8_t* t = book.view;
    unsigned k;
    int col;
    int skip;
    uint8_t style;
    uint16_t link;
    int lit;                        /* the link being drawn is the selected one */

    scr_begin(row);
    if (idx >= nlines) {
        scr_end();                  /* past the end: blank */

        return;
    }
    ln = &lines[idx];
    if (ln->kind == 0) {
        scr_end();

        return;
    }
    scr_colour(C_TEXT);
    for (col = 0; col < ln->x; col++) {
        if (ln->note && col == ln->x - 2) {
            scr_colour(C_NOTE);
            scr_add('|');
            scr_colour(C_TEXT);
        } else {
            scr_add(' ');
        }
    }
    if (ln->kind == REF_RULE) {
        scr_colour(C_DIM);
        while (col++ < scr_cols) {
            scr_add('-');
        }
        scr_end();

        return;
    }
    style = ln->style;
    link = ln->link;
    lit = link != REF_NONE && carried_selected(idx);
    skip = ln->kind == REF_PRE ? hscroll : 0;
    for (k = ln->start; k < ln->end; k = REF_SKIP(t, k)) {
        unsigned c = t[k];
        int role;

        switch (c) {
        case REF_EM_ON:
            style |= REF_ST_EM;
            continue;
        case REF_EM_OFF:
            style &= (uint8_t)~REF_ST_EM;
            continue;
        case REF_CODE_ON:
            style |= REF_ST_CODE;
            continue;
        case REF_CODE_OFF:
            style &= (uint8_t)~REF_ST_CODE;
            continue;
        case REF_LINK:
            link = (uint16_t)(t[k + 1] | t[k + 2] << 8);
            lit = sel == idx && col == sel_col;
            continue;
        case REF_LINK_END:
            link = REF_NONE;
            lit = 0;
            continue;
        }
        if (c < 0x20) {
            continue;
        }
        if (skip > 0) {
            skip--;
            continue;
        }
        if (link != REF_NONE) {
            role = lit ? C_SEL : C_LINK;
        } else if (ln->kind == REF_HEAD) {
            role = C_HEAD;
        } else if (style & REF_ST_CODE) {
            role = C_CODE;
        } else if (style & REF_ST_EM) {
            role = C_EM;
        } else {
            role = C_TEXT;
        }
        scr_colour(role);
        scr_add((int)c);
        col++;
    }
    scr_end();
}

/* Spaces up to column x on the row being drawn. */
static void status_pad(int x)
{
    while (scr_x() < x) {
        scr_add(' ');
    }
}

/* Where am I: the topic's ancestors and its title, as much as fits, and how
 * far down. */
static void draw_status(void)
{
    static char buf[160];
    static unsigned chain[8];
    int n = 0;
    unsigned t = (unsigned)cur;
    int len = 0;
    int i;
    int pct;
    ref_topic tp;

    while (n < 8) {
        chain[n++] = t;
        ref_get_topic(&book, t, &tp);
        if (tp.parent == REF_NONE) {
            break;
        }
        t = tp.parent;
    }
    buf[0] = 0;
    for (i = n - 1; i >= 0; i--) {
        const char* s = ui_title(chain[i]);
        int sl = (int)strlen(s);

        if (len + sl + 4 >= (int)sizeof buf) {
            break;
        }
        if (i != n - 1) {
            memcpy(buf + len, " > ", 3);
            len += 3;
        }
        memcpy(buf + len, s, (size_t)sl);
        len += sl;
        buf[len] = 0;
    }
    pct = nlines <= scr_rows ? 100 : (top + scr_rows) * 100 / nlines;
    if (pct > 100) {
        pct = 100;
    }
    scr_begin(scr_rows);
    scr_colour(C_STATUS);
    {
        /* The end of the trail, after "...", if all of it does not fit. */
        int room = scr_cols - 7;
        const char* s = buf;

        scr_add(' ');
        if (len > room) {
            scr_adds("...");
            s = buf + len - (room - 3);
        }
        scr_adds(s);
    }
    {
        /* The percentage, right-aligned. */
        static char p[8];
        int pl = 0;

        if (pct >= 100) {
            p[pl++] = '1';
            p[pl++] = '0';
            p[pl++] = '0';
        } else {
            if (pct >= 10) {
                p[pl++] = (char)('0' + pct / 10);
            }
            p[pl++] = (char)('0' + pct % 10);
        }
        p[pl++] = '%';
        p[pl] = 0;
        status_pad(scr_cols - pl - 1);
        scr_adds(p);
    }
    scr_end();
}

static void draw(void)
{
    int r;

    for (r = 0; r < scr_rows; r++) {
        draw_line(r, top + r);
    }
    draw_status();
#ifdef REF_SCRIPT
    script_log("view topic %d top %d lines %d sel %d:%d", cur, top, nlines, sel, sel_col);
#endif
}

/* --- links -------------------------------------------------------------------- */

/* The links that start on a line (not those carried onto it): each one's
 * column, up to max. How many. */
static int line_links(int idx, int* cols, uint16_t* targets, int max)
{
    const ref_line* ln = &lines[idx];
    const uint8_t* t = book.view;
    unsigned k;
    int col = ln->x;
    int n = 0;

    for (k = ln->start; k < ln->end; k = REF_SKIP(t, k)) {
        if (t[k] == REF_LINK) {
            if (n < max) {
                cols[n] = col;
                targets[n] = (uint16_t)(t[k + 1] | t[k + 2] << 8);
            }
            n++;
        } else if (t[k] >= 0x20) {
            col++;
        }
    }

    return n < max ? n : max;
}

#define LINE_LINKS 24
static int lcols[LINE_LINKS];
static uint16_t ltargets[LINE_LINKS];

/* Select the next link (dir 1) or the one before (-1) from the selection,
 * or from the top of the screen. 0 if there is none that way. */
static int step_link(int dir)
{
    int idx;
    int from_col;

    if (sel >= 0) {
        idx = sel;
        from_col = sel_col;
    } else {
        idx = dir > 0 ? top : top + scr_rows - 1;
        if (idx >= nlines) {
            idx = nlines - 1;
        }
        from_col = dir > 0 ? -1 : 0x7fff;
    }
    while (idx >= 0 && idx < nlines) {
        int n = line_links(idx, lcols, ltargets, LINE_LINKS);
        int i;

        if (dir > 0) {
            for (i = 0; i < n; i++) {
                if (lcols[i] > from_col) {
                    break;
                }
            }
        } else {
            for (i = n - 1; i >= 0; i--) {
                if (lcols[i] < from_col) {
                    break;
                }
            }
        }
        if (i >= 0 && i < n) {
            sel = idx;
            sel_col = lcols[i];
            sel_target = ltargets[i];
            if (sel < top) {
                top = sel;
            } else if (sel >= top + scr_rows) {
                top = sel - scr_rows + 1;
            }

            return 1;
        }
        idx += dir;
        from_col = dir > 0 ? -1 : 0x7fff;
    }

    return 0;
}

static void scroll_to(int t)
{
    int last = nlines - scr_rows;

    if (t > last) {
        t = last;
    }
    if (t < 0) {
        t = 0;
    }
    top = t;
    if (sel >= 0 && (sel < top || sel >= top + scr_rows)) {
        sel = -1;
    }
}

static int history(place* from, int* nfrom, place* to, int* nto)
{
    place p;
    place here;

    if (*nfrom == 0) {
        return 0;
    }
    p = from[--(*nfrom)];
    here.topic = (uint16_t)cur;
    here.top = (uint16_t)top;
    push(to, nto, here);
    if (load(p.topic) != 0) {
        return 0;
    }
    scroll_to(p.top);

    return 1;
}

int view_loop(void)
{
    scr_cls();
    draw();
    for (;;) {
        key k = in_wait();
        int m = in_meaning(k);
        int redraw = 1;

        switch (m) {
        case K_UP:
            scroll_to(top - 1);
            break;
        case K_DOWN:
            scroll_to(top + 1);
            break;
        case K_PGUP:
            scroll_to(top - (scr_rows - 1));
            break;
        case K_PGDN:
            scroll_to(top + (scr_rows - 1));
            break;
        case K_HOME:
            scroll_to(0);
            break;
        case K_END:
            scroll_to(nlines);
            break;
        case K_LEFT:
            hscroll = hscroll > HSTEP ? hscroll - HSTEP : 0;
            break;
        case K_RIGHT:
            hscroll += HSTEP;
            break;
        case K_TAB:
            redraw = step_link(1);
            break;
        case K_BACKTAB:
            redraw = step_link(-1);
            break;
        case K_ENTER:
            if (sel < 0 && !step_link(1)) {
                redraw = 0;
                break;
            }
            if (sel >= 0 && sel < top + scr_rows) {
                unsigned target = sel_target;

                if (target < book.ntopics && view_open(target) == 0) {
                    break;
                }
            }
            break;
        case K_BACK:
        case K_HISTBACK:
            redraw = history(back, &nback, fwd, &nfwd);
            break;
        case K_HISTFWD:
            redraw = history(fwd, &nfwd, back, &nback);
            break;
        case K_FIND:
            return GO_FIND;
        case K_HELP:
            ui_help();
            scr_cls();
            break;
        case K_ESC:
            return GO_QUIT;
        case K_CHAR:
            switch (k.ch) {
            case 'q':
                return GO_QUIT;
            case 't':
            case 'c':
                return GO_TOC;
            case '/':
                return GO_FIND;
            case '?':
            case 'h':
                ui_help();
                scr_cls();
                break;
            case 'n': {
                ref_topic tp;
                unsigned next;

                ref_get_topic(&book, (unsigned)cur, &tp);
                next = (unsigned)cur + tp.span + 1;
                redraw = next < book.ntopics && view_open(next) == 0;
                break;
            }
            case 'p':
                redraw = cur > 0 && view_open((unsigned)cur - 1) == 0;
                break;
            case 'u': {
                ref_topic tp;

                ref_get_topic(&book, (unsigned)cur, &tp);
                redraw = tp.parent != REF_NONE && view_open(tp.parent) == 0;
                break;
            }
            case ' ':
                scroll_to(top + (scr_rows - 1));
                break;
            case 'b':
                scroll_to(top - (scr_rows - 1));
                break;
            default:
                redraw = 0;
            }
            break;
        default:
            redraw = 0;
        }
        if (redraw) {
            draw();
        }
    }
}
