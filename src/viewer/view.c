/*
 * A topic on the screen, read as in a text-mode browser: a cursor moves
 * through the text -- by line and character with the arrows, to the next
 * heading or link with CTRL -- and the screen follows it. Whatever is under
 * the cursor, a link or a heading, is lit in the selection's colour on that
 * line alone; anywhere else the VDP's own text cursor shows where it is.
 * Enter follows the link under the cursor, and the history keeps the
 * cursor, so going back puts it on the link that was followed.
 */
#include <string.h>

#include "ui.h"

#define MAX_LINES 2000
#define HISTORY   32
#define HSTEP     8

typedef struct {
    uint16_t topic;
    uint16_t top;
    uint16_t line;          /* the cursor's */
    uint8_t col;
} place;

static ref_line lines[MAX_LINES];
static int nlines;
static int cur = -1;
static int top;
static int hscroll;
static int cl;                      /* the cursor: its line */
static int cc;                      /* and its column, as on the screen */
static int want;                    /* the column Up and Down keep to */
static int mline = -1;              /* the find's match: its line, or -1 */
static int mcol;                    /* where it starts there, in visible characters */
static int mlen;
#define FIND_MAX 40
static char ftext[FIND_MAX + 1];    /* the last thing found, kept between topics */

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
    cl = 0;
    cc = nlines ? lines[0].x : 0;
    want = cc;
    mline = -1;

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
    return view_open_from(num, 0);
}

int view_open_from(unsigned num, int from)
{
    if (cur >= 0) {
        place p;

        p.topic = (uint16_t)cur;
        p.top = (uint16_t)top;
        p.line = (uint16_t)cl;
        p.col = (uint8_t)cc;
        push(back, &nback, p);
        nfwd = 0;
    }
    if (from) {
        /* Where it was opened from: the contents, a list or find, which
         * Backspace returns to. A mark, not a topic. */
        place p;

        p.topic = REF_NONE;
        p.top = 0;
        p.line = (uint16_t)from;
        p.col = 0;
        push(back, &nback, p);
        nfwd = 0;
    }

    return load(num);
}

/* --- drawing ------------------------------------------------------------------ */

/* The link at column col of line i, if there is one: its target, and the
 * columns it covers on that line. A link wrapped onto the line counts from
 * the line's start. */
static int link_at(int i, int col, uint16_t* target, int* from, int* to)
{
    const ref_line* ln = &lines[i];
    const uint8_t* t = book.view;
    unsigned k;
    int x = ln->x;
    uint16_t link = ln->link;
    int start = x;

    if (i < 0 || i >= nlines || ln->kind == 0) {
        return 0;
    }
    for (k = ln->start; k < ln->end; k = REF_SKIP(t, k)) {
        if (t[k] == REF_LINK) {
            link = (uint16_t)(t[k + 1] | t[k + 2] << 8);
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
    *to = x;

    return 1;
}

/* The column of the last link to target that opens on line i: the one a
 * link wrapped onto the next line started with. */
static int last_opening(int i, uint16_t target)
{
    const ref_line* ln = &lines[i];
    const uint8_t* t = book.view;
    unsigned k;
    int x = ln->x;
    int at = ln->x;

    for (k = ln->start; k < ln->end; k = REF_SKIP(t, k)) {
        if (t[k] == REF_LINK && (uint16_t)(t[k + 1] | t[k + 2] << 8) == target) {
            at = x;
        } else if (t[k] >= 0x20) {
            x++;
        }
    }

    return at;
}

/* How far the cursor may go on line i: its indent, to its last character. */
static int last_col(int i)
{
    const ref_line* ln = &lines[i];
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

/* The first and last lines of the heading line i is part of (a heading
 * that wraps is several HEAD lines in a row), or -1 if it is not one. */
static int heading_first(int i)
{
    if (i < 0 || i >= nlines || lines[i].kind != REF_HEAD) {
        return -1;
    }
    while (i > 0 && lines[i - 1].kind == REF_HEAD) {
        i--;
    }

    return i;
}

static int heading_last(int i)
{
    while (i + 1 < nlines && lines[i + 1].kind == REF_HEAD) {
        i++;
    }

    return i;
}

static int clamp_col(int i, int col)
{
    int last = last_col(i);

    if (col > last) {
        col = last;
    }

    return col < lines[i].x ? lines[i].x : col;
}

/* The cursor's place on the screen, sideways scrolling allowed for. */
static int cursor_x(void)
{
    if (lines[cl].kind == REF_PRE) {
        return cc - hscroll;
    }

    return cc;
}

/* Is the cursor on something lit -- a link or a heading -- rather than
 * shown by the VDP's text cursor? */
static int cursor_lit(void)
{
    uint16_t t;
    int a;
    int b;

    if (nlines == 0) {
        return 0;
    }

    return lines[cl].kind == REF_HEAD || link_at(cl, cc, &t, &a, &b);
}

static void place_cursor(void)
{
    if (nlines == 0 || cursor_lit() || cl < top || cl >= top + scr_rows) {
        scr_cursor(0, 0, 0);
    } else {
        scr_cursor(cursor_x(), cl - top, 1);
    }
}

/* What the cursor lights: nothing, a heading, or a link -- every line of
 * either, when it wraps. Worked out when the cursor moves, and read by
 * draw_line. */
#define LIT_NONE 0
#define LIT_HEAD 1
#define LIT_LINK 2

static int lit_kind;
static int lit_first;               /* its first line */
static int lit_last;                /* its last */
static int lit_col;                 /* a link: the column it starts at, on lit_first */

static void light(void)
{
    uint16_t target;
    int from;
    int to;

    lit_kind = LIT_NONE;
    if (nlines == 0) {
        return;
    }
    if (link_at(cl, cc, &target, &from, &to)) {
        int j = cl;

        /* Back to the line it starts on, if it was wrapped onto this one. */
        if (from == lines[cl].x && lines[cl].link == target) {
            while (j > 0 && lines[j].link == target) {
                j--;
            }
            from = last_opening(j, target);
        }
        lit_kind = LIT_LINK;
        lit_first = j;
        lit_col = from;
        lit_last = cl;
        while (lit_last + 1 < nlines && lines[lit_last + 1].link == target) {
            lit_last++;
        }
    } else if (lines[cl].kind == REF_HEAD) {
        lit_kind = LIT_HEAD;
        lit_first = heading_first(cl);
        lit_last = heading_last(lit_first);
    }
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
    int in_lit;                     /* a line of what the cursor lights */
    int head_lit;                   /* a heading's line, lit to the edge */
    int lit;                        /* the text being drawn is lit */
    int vis;                        /* visible characters so far on the line */

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
    in_lit = idx >= lit_first && idx <= lit_last;
    head_lit = lit_kind == LIT_HEAD && in_lit;
    /* A link wrapped onto this line from the lit one is lit from the start. */
    lit = head_lit || (lit_kind == LIT_LINK && in_lit && idx > lit_first);
    skip = ln->kind == REF_PRE ? hscroll : 0;
    vis = 0;
    for (k = ln->start; k < ln->end; k = REF_SKIP(t, k)) {
        unsigned c = t[k];
        int role;
        int here;

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
            if (lit_kind == LIT_LINK && idx == lit_first && ln->x + vis == lit_col) {
                lit = 1;
            }
            continue;
        case REF_LINK_END:
            link = REF_NONE;
            if (!head_lit) {
                lit = 0;
            }
            continue;
        }
        if (c < 0x20) {
            continue;
        }
        here = vis++;
        if (skip > 0) {
            skip--;
            continue;
        }
        if (lit) {
            role = C_SEL;                   /* under the cursor */
        } else if (idx == mline && here >= mcol && here < mcol + mlen) {
            role = C_SEL;                   /* the find's match */
        } else if (link != REF_NONE) {
            role = C_LINK;
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
    /* A heading under the cursor is a bar to the edge; a link or a match
     * that ends the line is not. */
    scr_colour(head_lit ? C_SEL : C_TEXT);
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

static void log_view(int scrolled)
{
#ifdef REF_SCRIPT
    if (scrolled) {
        script_log("view topic %d top %d lines %d cur %d:%d scrolled %d", cur, top, nlines, cl, cc,
                   scrolled);
    } else {
        script_log("view topic %d top %d lines %d cur %d:%d", cur, top, nlines, cl, cc);
    }
#else
    (void)scrolled;
#endif
}

static void draw(void)
{
    int r;

    light();

    for (r = 0; r < scr_rows; r++) {
        draw_line(r, top + r);
    }
    draw_status();
    place_cursor();
    log_view(0);
}

#define SCROLLED 2

/* Lines first to last, where they are on the screen. */
static void redraw_lines(int first, int last)
{
    int j;

    for (j = first; j <= last; j++) {
        if (j >= top && j < top + scr_rows) {
            draw_line(j - top, j);
        }
    }
}

/* After the cursor moved from line was_cl, and the text from line was: a
 * short move scrolls the rows on screen and draws only those that came into
 * view, and the two lines the cursor left and reached; a long one redraws. */
static void moved(int was, int was_cl)
{
    int d = top - was;
    int r;
    int of = lit_kind ? lit_first : -1;     /* what was lit */
    int ol = lit_kind ? lit_last : -1;

    if (d > SCROLL_MAX || d < -SCROLL_MAX) {
        draw();

        return;
    }
    light();
    if (d) {
        scr_scroll(0, d);
        if (d > 0) {
            for (r = scr_rows - d; r < scr_rows; r++) {
                draw_line(r, top + r);
            }
        } else {
            for (r = 0; r < -d; r++) {
                draw_line(r, top + r);
            }
        }
        draw_status();
    }
    redraw_lines(was_cl, was_cl);
    if (of >= 0) {
        redraw_lines(of, ol);
    }
    redraw_lines(cl, cl);
    if (lit_kind) {
        redraw_lines(lit_first, lit_last);
    }
    place_cursor();
    log_view(d);
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

/* Keep the cursor's line on the screen, and its column, on a line of code
 * or a table, inside the sideways scroll. */
static void follow_cursor(void)
{
    int at;

    if (cl < top) {
        top = cl;
    } else if (cl >= top + scr_rows) {
        top = cl - scr_rows + 1;
    }
    if (lines[cl].kind == REF_PRE) {
        at = cc - lines[cl].x;
        if (at < hscroll) {
            hscroll = at - at % HSTEP;
        } else if (cc - hscroll >= scr_cols) {
            hscroll = cc - scr_cols + HSTEP - (cc - scr_cols) % HSTEP;
        }
    }
}

static void cursor_to(int line, int col)
{
    cl = line < 0 ? 0 : line >= nlines ? nlines - 1 : line;
    cc = clamp_col(cl, col);
}

/* The cursor to the next link (dir 1) or the one before (-1) -- its start,
 * not a part of it wrapped onto another line. 0 if there is none that way. */
static int step_link(int dir)
{
    int idx = cl;
    int from_col = cc;

    /* On a link already: step from where it starts, on its first line if
     * it wraps, so the link itself is not the one found. */
    light();
    if (lit_kind == LIT_LINK) {
        idx = lit_first;
        from_col = lit_col;
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
            cl = idx;
            cc = lcols[i];
            want = cc;
            follow_cursor();

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
}

/* Does a heading start at line i? (A heading that wraps is several HEAD
 * lines in a row; a blank line always comes between two headings.) */
static int heading_at(int i)
{
    return lines[i].kind == REF_HEAD && (i == 0 || lines[i - 1].kind != REF_HEAD);
}

/* The cursor to the next heading (dir 1) or the one before (-1), brought to
 * the top of the screen. 0 if there is none that way. */
static int step_heading(int dir)
{
    int i = cl + dir;

    while (i >= 0 && i < nlines && !heading_at(i)) {
        i += dir;
    }
    if (i < 0 || i >= nlines) {
        return 0;
    }
    cursor_to(i, 0);
    want = cc;
    scroll_to(i);
    follow_cursor();                        /* the last screen: still show it */

    return 1;
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
    here.line = (uint16_t)cl;
    here.col = (uint8_t)cc;
    push(to, nto, here);
    if (load(p.topic) != 0) {
        return 0;
    }
    scroll_to(p.top);
    cursor_to(p.line, p.col);
    want = cc;
    follow_cursor();

    return 1;
}

/* --- finding on the screen ------------------------------------------------------- */

/* Line i's visible characters, lower case, into buf: how many. */
static int line_text(int i, char* buf, int size)
{
    const ref_line* ln = &lines[i];
    const uint8_t* t = book.view;
    unsigned k;
    int n = 0;

    for (k = ln->start; k < ln->end && n < size; k = REF_SKIP(t, k)) {
        int c = t[k];

        if (c >= 0x20) {
            buf[n++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
        }
    }

    return n;
}

/* Find ftext from the match (or the top of the screen), forward (dir 1) or
 * back, wrapping round the topic; a match never spans lines. 1 if found. */
static int find_step(int dir)
{
    static char buf[512];
    static char want[FIND_MAX + 1];
    int wl = (int)strlen(ftext);
    int i;
    int from;
    int tries;

    if (wl == 0 || nlines == 0) {
        return 0;
    }
    for (i = 0; i <= wl; i++) {
        int c = (unsigned char)ftext[i];

        want[i] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
    }
    if (mline >= 0) {
        i = mline;
        from = dir > 0 ? mcol + 1 : mcol - 1;
    } else {
        i = dir > 0 ? top : (top + scr_rows - 1 < nlines ? top + scr_rows - 1 : nlines - 1);
        from = dir > 0 ? 0 : 0x7fff;
    }
    for (tries = 0; tries <= nlines; tries++) {
        int n = line_text(i, buf, (int)sizeof buf);
        int x;

        if (dir > 0) {
            for (x = from < 0 ? 0 : from; x + wl <= n; x++) {
                if (memcmp(buf + x, want, (size_t)wl) == 0) {
                    break;
                }
            }
            if (x + wl <= n) {
                mline = i;
                mcol = x;
                mlen = wl;

                return 1;
            }
            i = i + 1 < nlines ? i + 1 : 0;
            from = 0;
        } else {
            for (x = (from > n - wl ? n - wl : from); x >= 0; x--) {
                if (memcmp(buf + x, want, (size_t)wl) == 0) {
                    break;
                }
            }
            if (x >= 0) {
                mline = i;
                mcol = x;
                mlen = wl;

                return 1;
            }
            i = i > 0 ? i - 1 : nlines - 1;
            from = 0x7fff;
        }
    }

    return 0;
}

/* After a search: the match on screen (in the middle, if it was off it), or
 * "Not found" on the bottom row until a key. */
static void show_find(int found)
{
    if (found) {
        if (mline < top || mline >= top + scr_rows) {
            scroll_to(mline - scr_rows / 2);
        }
        cursor_to(mline, lines[mline].x + mcol);
        want = cc;
        follow_cursor();
#ifdef REF_SCRIPT
        script_log("find '%s' at %d:%d", ftext, mline, mcol);
#endif

        return;
    }
    mline = -1;
    scr_begin(scr_rows);
    scr_colour(C_STATUS);
    scr_adds(" Not found: ");
    scr_adds(ftext);
    scr_end();
#ifdef REF_SCRIPT
    script_log("find '%s' not found", ftext);
#endif
    in_wait();
}

/* CTRL+F: the prompt on the bottom row, holding the last search, as aed's.
 * Enter finds; Esc, or Enter with nothing typed, cancels. Spaces can be
 * typed: a search may be several words. */
static void find_prompt(void)
{
    static char text[FIND_MAX + 1];

    strcpy(text, ftext);
    for (;;) {
        key k;
        int m;
        int len = (int)strlen(text);

        scr_begin(scr_rows);
        scr_colour(C_STATUS);
        scr_adds(" Find: ");
        scr_adds(text);
        scr_end();
        scr_cursor(7 + len, scr_rows, 1);
        k = in_wait();
        m = in_meaning(k);
        if (m == K_ESC || (m == K_ENTER && len == 0)) {
            scr_cursor(0, 0, 0);

            return;
        }
        if (m == K_ENTER) {
            scr_cursor(0, 0, 0);
            strcpy(ftext, text);
            mline = -1;                     /* from the top of the screen */
            show_find(find_step(1));

            return;
        }
        if (m == K_BACK && len > 0) {
            text[len - 1] = 0;
        } else if ((m == K_CHAR || k.ch == ' ') && k.ch >= 0x20 && k.ch < 0x7f
                   && len < FIND_MAX) {
            text[len] = (char)k.ch;
            text[len + 1] = 0;
        }
    }
}

/* The cursor down (n > 0) or up n lines, keeping to its column; a page
 * moves the text with it. */
static void cursor_lines(int n, int page)
{
    if (page) {
        scroll_to(top + n);
    }
    cursor_to(cl + n, want);
    follow_cursor();
}

int view_loop(void)
{
    scr_cls();
    draw();
    for (;;) {
        key k = in_wait();
        int m = in_meaning(k);
        int redraw = 1;
        int was = top;              /* for a move: where the text and the */
        int was_cl = cl;            /* cursor started */
        int was_h = hscroll;

        switch (m) {
        case K_UP:
            cursor_lines(-1, 0);
            redraw = SCROLLED;
            break;
        case K_DOWN:
            cursor_lines(1, 0);
            redraw = SCROLLED;
            break;
        case K_PGUP:
            cursor_lines(-(scr_rows - 1), 1);
            redraw = SCROLLED;
            break;
        case K_PGDN:
            cursor_lines(scr_rows - 1, 1);
            redraw = SCROLLED;
            break;
        case K_HOME:
            top = 0;
            cursor_to(0, 0);
            want = cc;
            redraw = SCROLLED;
            break;
        case K_END:
            scroll_to(nlines);
            cursor_to(nlines - 1, 0);
            want = cc;
            redraw = SCROLLED;
            break;
        case K_LEFT:
            cc = clamp_col(cl, cc - 1);
            want = cc;
            follow_cursor();
            redraw = SCROLLED;
            break;
        case K_RIGHT:
            cc = clamp_col(cl, cc + 1);
            want = cc;
            follow_cursor();
            redraw = SCROLLED;
            break;
        case K_TAB:
        case K_LINK_NEXT:
            redraw = step_link(1) ? SCROLLED : 0;
            break;
        case K_BACKTAB:
        case K_LINK_PREV:
            redraw = step_link(-1) ? SCROLLED : 0;
            break;
        case K_FIND_HERE:
            find_prompt();
            break;
        case K_FIND_NEXT:
        case K_FIND_PREV:
            if (ftext[0]) {
                show_find(find_step(m == K_FIND_NEXT ? 1 : -1));
            }
            break;
        case K_HEAD_NEXT:
            redraw = step_heading(1) ? SCROLLED : 0;
            break;
        case K_HEAD_PREV:
            redraw = step_heading(-1) ? SCROLLED : 0;
            break;
        case K_ENTER: {
            uint16_t target;
            int a;
            int b;

            redraw = 0;
            if (nlines && link_at(cl, cc, &target, &a, &b) && target < book.ntopics) {
                redraw = view_open(target) == 0;
            }
            break;
        }
        case K_BACK:
        case K_HISTBACK:
            if (nback > 0 && back[nback - 1].topic == REF_NONE) {
                /* Back to the view this topic was opened from; forward
                 * comes here again. */
                place here;
                int to = back[--nback].line;

                here.topic = (uint16_t)cur;
                here.top = (uint16_t)top;
                here.line = (uint16_t)cl;
                here.col = (uint8_t)cc;
                push(fwd, &nfwd, here);
#ifdef REF_SCRIPT
                script_log("back to %s", to == GO_TOC ? "contents" : to == GO_LIST ? "list" : "find");
#endif

                return to;
            }
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
                cursor_lines(scr_rows - 1, 1);
                redraw = SCROLLED;
                break;
            case 'b':
                cursor_lines(-(scr_rows - 1), 1);
                redraw = SCROLLED;
                break;
            default:
                redraw = 0;
            }
            break;
        default:
            redraw = 0;
        }
        if (redraw == SCROLLED && hscroll == was_h && nlines > 0) {
            moved(was, was_cl);
        } else if (redraw) {
            draw();
        }
    }
}
