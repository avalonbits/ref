/*
 * A topic on the screen, read as in a text-mode browser.
 *
 * A cursor moves through the text -- by line and character with the
 * arrows, to the next heading or link with CTRL -- and the screen follows
 * it. Whatever the cursor is on, a link or a heading, is lit, every line of
 * it; anywhere else the VDP's own text cursor shows where it is. Enter
 * follows the link under the cursor. The history keeps the cursor, so going
 * back puts it on the link that was followed; and it remembers the view a
 * topic was opened from -- the contents, a list, find -- to go back to that.
 *
 * What the text holds -- where links and headings are -- is doc.c's. This
 * file is the cursor, the drawing, the keys and the history.
 */
#include <string.h>

#include "doc.h"
#include "script.h"
#include "ui.h"

#define HISTORY   32
#define HSTEP     8                 /* columns code and tables scroll sideways */

#define lines  doc_lines
#define nlines doc_nlines

static int cur = -1;                /* the topic shown, or -1 */
static int top;                     /* the line at the top of the screen */
static int hscroll;                 /* how far code and tables are scrolled */
static int cl;                      /* the cursor: its line */
static int cc;                      /* and its column, as on the screen */
static int want;                    /* the column Up and Down keep to */

static int mline = -1;              /* its match: the line, or -1 */
static int mcol;                    /* and where on it, in characters */

/* --- opening, and the history ---------------------------------------------------- */

/* A place in the history: a topic and where the screen and cursor were --
 * or, with topic REF_NONE, the view a topic was opened from (GO_TOC,
 * GO_LIST, GO_FIND, in line). */
typedef struct {
    uint16_t topic;
    uint16_t top;
    uint16_t line;
    uint8_t col;
} place;

static place back[HISTORY];
static int nback;
static place fwd[HISTORY];
static int nfwd;

void view_reset(void)
{
    cur = -1;
    nback = 0;
    nfwd = 0;
    mline = -1;
}

int view_current(void)
{
    return cur;
}

int view_has(void)
{
    return cur >= 0;
}

/* --- the cursor ------------------------------------------------------------------ */

static void scroll_to(int t)
{
    int last = nlines - scr_rows;

    top = t > last ? last : t;
    if (top < 0) {
        top = 0;
    }
}

static void cursor_to(int line, int col)
{
    cl = line < 0 ? 0 : line >= nlines ? nlines - 1 : line;
    cc = doc_clamp(cl, col);
}

/* Keep the cursor's line on the screen -- and its column, on a line of
 * code or a table, inside the sideways scroll. */
static void follow_cursor(void)
{
    if (cl < top) {
        top = cl;
    } else if (cl >= top + scr_rows) {
        top = cl - scr_rows + 1;
    }
    if (lines[cl].kind == REF_PRE) {
        int at = cc - lines[cl].x;

        if (at < hscroll) {
            hscroll = at - at % HSTEP;
        } else if (cc - hscroll >= scr_cols) {
            hscroll = cc - scr_cols + HSTEP - (cc - scr_cols) % HSTEP;
        }
    }
}

/* Up and down n lines, keeping to the column; a page moves the text too. */
static void cursor_lines(int n, int page)
{
    if (page) {
        scroll_to(top + n);
    }
    cursor_to(cl + n, want);
    follow_cursor();
}

/* The cursor somewhere new, which Up and Down then keep to. */
static void cursor_jump(int line, int col)
{
    cursor_to(line, col);
    want = cc;
    follow_cursor();
}

/* --- what the cursor lights -------------------------------------------------------- */

#define LIT_NONE 0
#define LIT_HEAD 1
#define LIT_LINK 2

static int lit_kind;
static int lit_first;               /* its first line */
static int lit_last;                /* its last */
static int lit_col;                 /* a link: the column it starts at, on its first line */

/* Worked out after the cursor moves; read by draw_line. */
static void light(void)
{
    doc_link link;

    lit_kind = LIT_NONE;
    if (nlines == 0) {
        return;
    }
    if (doc_link_at(cl, cc, &link)) {
        lit_kind = LIT_LINK;
        lit_first = link.first;
        lit_last = link.last;
        lit_col = link.first_col;
    } else if (doc_heading(cl, &lit_first, &lit_last)) {
        lit_kind = LIT_HEAD;
    }
}

/* The VDP's text cursor, where the cursor is -- unless the cursor is on
 * something lit, which shows it already. */
static void place_cursor(void)
{
    int x = lines[cl].kind == REF_PRE ? cc - hscroll : cc;

    if (nlines == 0 || lit_kind != LIT_NONE || cl < top || cl >= top + scr_rows) {
        scr_cursor(0, 0, 0);
    } else {
        scr_cursor(x, cl - top, 1);
    }
}

/* --- drawing --------------------------------------------------------------------- */

/* The colour a character is drawn in. */
static int role_of(const ref_line* ln, int lit, int found, uint16_t link, uint8_t style)
{
    if (lit || found) {
        return C_SEL;
    }
    if (link != REF_NONE) {
        return C_LINK;
    }
    if (ln->kind == REF_HEAD) {
        return C_HEAD;
    }
    if (style & REF_ST_CODE) {
        return C_CODE;
    }

    return (style & REF_ST_EM) ? C_EM : C_TEXT;
}

/* Line idx of the text on screen row row. */
static void draw_line(int row, int idx)
{
    const ref_line* ln;
    const uint8_t* t = book.view;
    unsigned k;
    int col;
    int skip;                       /* characters scrolled off to the left */
    int vis = 0;                    /* characters so far on the line */
    uint8_t style;
    uint16_t link;
    int in_lit;
    int head_lit;                   /* a heading's line, lit to the edge */
    int lit;                        /* the text being drawn is lit */

    scr_begin(row);
    if (idx >= nlines || lines[idx].kind == 0) {
        scr_end();                  /* a blank line, or past the end */

        return;
    }
    ln = &lines[idx];

    /* The indent, with a note's bar in it. */
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
    for (k = ln->start; k < ln->end; k = REF_SKIP(t, k)) {
        unsigned c = t[k];
        int here;
        int found;

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
            lit = head_lit;
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
        found = idx == mline && here >= mcol && here < mcol + (int)strlen(ui_find_text);
        scr_colour(role_of(ln, lit, found, link, style));
        scr_add((int)c);
        col++;
    }
    /* A heading under the cursor is a bar to the edge; a link or a match
     * that ends the line is not. */
    scr_colour(head_lit ? C_SEL : C_TEXT);
    scr_end();
}

/* The status line: the topic's place in the tree -- as much of the end of
 * it as fits -- and how far down the screen is. */
static void draw_status(void)
{
    static char trail[160];
    static char pct_text[8];
    static unsigned chain[8];
    int n = 0;
    unsigned t = (unsigned)cur;
    int len = 0;
    int room = scr_status_cols - 7;
    int i;
    int pct;
    ref_topic tp;

    for (;;) {
        chain[n++] = t;
        ref_get_topic(&book, t, &tp);
        if (tp.parent == REF_NONE || n == 8) {
            break;
        }
        t = tp.parent;
    }
    trail[0] = 0;
    for (i = n - 1; i >= 0; i--) {
        const char* s = ui_title(chain[i]);
        int sl = (int)strlen(s);

        if (len + sl + 4 >= (int)sizeof trail) {
            break;
        }
        if (i != n - 1) {
            memcpy(trail + len, " > ", 3);
            len += 3;
        }
        memcpy(trail + len, s, (size_t)sl + 1);
        len += sl;
    }
    pct = nlines <= scr_rows ? 100 : (top + scr_rows) * 100 / nlines;
    sprintf(pct_text, "%d%%", pct > 100 ? 100 : pct);

    scr_begin(scr_rows);
    scr_colour(C_STATUS);
    scr_add(' ');
    if (len > room) {
        scr_adds("...");
        scr_adds(trail + len - (room - 3));
    } else {
        scr_adds(trail);
    }
    while (scr_x() < scr_status_cols - (int)strlen(pct_text) - 1) {
        scr_add(' ');
    }
    scr_adds(pct_text);
    scr_end();
}

static void log_view(int scrolled)
{
    if (scrolled) {
        LOG("view topic %d top %d lines %d cur %d:%d scrolled %d", cur, top, nlines, cl, cc,
            scrolled);
    } else {
        LOG("view topic %d top %d lines %d cur %d:%d", cur, top, nlines, cl, cc);
    }
}

/* Everything. */
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

/* Lines first to last, those of them on the screen. */
static void draw_lines(int first, int last)
{
    int j;

    for (j = first; j <= last; j++) {
        if (j >= top && j < top + scr_rows) {
            draw_line(j - top, j);
        }
    }
}

/* After the cursor moved from line was_cl and the text from line was: as
 * little as will do. A short move scrolls the rows on the VDP and draws
 * those that came into view; then the lines the cursor left and reached,
 * and what was lit and is lit now. A long move draws everything. */
static void draw_moved(int was, int was_cl)
{
    int d = top - was;
    int r;
    int old_first = lit_kind ? lit_first : -1;
    int old_last = lit_kind ? lit_last : -1;

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
    draw_lines(was_cl, was_cl);
    if (old_first >= 0) {
        draw_lines(old_first, old_last);
    }
    draw_lines(cl, cl);
    if (lit_kind) {
        draw_lines(lit_first, lit_last);
    }
    place_cursor();
    log_view(d);
}

static void push(place* stack, int* n, place p)
{
    if (*n == HISTORY) {
        memmove(stack, stack + 1, (HISTORY - 1) * sizeof *stack);
        (*n)--;
    }
    stack[(*n)++] = p;
}

static place here(void)
{
    place p;

    p.topic = (uint16_t)cur;
    p.top = (uint16_t)top;
    p.line = (uint16_t)cl;
    p.col = (uint8_t)cc;

    return p;
}

static int load(unsigned num)
{
    if (doc_load(num, scr_cols) != 0) {
        return -1;
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

int view_open(unsigned num)
{
    return view_open_from(num, 0);
}

int view_open_from(unsigned num, int from)
{
    if (cur >= 0) {
        push(back, &nback, here());
        nfwd = 0;
    }
    if (from) {
        place mark;

        mark.topic = REF_NONE;
        mark.top = 0;
        mark.line = (uint16_t)from;
        mark.col = 0;
        push(back, &nback, mark);
        nfwd = 0;
    }

    return load(num);
}

/* Back (from the back stack to the forward one) or forward: the topic and
 * place on top of one stack, this one onto the other. 1 if it moved. */
static int travel(place* from, int* nfrom, place* to, int* nto)
{
    place p;

    if (*nfrom == 0) {
        return 0;
    }
    p = from[--(*nfrom)];
    push(to, nto, here());
    if (load(p.topic) != 0) {
        return 0;
    }
    scroll_to(p.top);
    cursor_jump(p.line, p.col);

    return 1;
}

/* Back past a mark: the view to return to, or 0 if the next step back is a
 * topic. */
static int back_to_view(void)
{
    int to;

    if (nback == 0 || back[nback - 1].topic != REF_NONE) {
        return 0;
    }
    to = back[--nback].line;
    push(fwd, &nfwd, here());
    LOG("back to %s", to == GO_TOC ? "contents" : to == GO_LIST ? "list" : "find");

    return to;
}

/* --- finding on the screen ------------------------------------------------------- */

/* Find ui_find_text, forward (dir 1) or back, from its last match -- or, the
 * first time, from the top of the screen -- and show the match, or say it
 * is not there until a key. */
static void find(int dir)
{
    int line;
    int from;
    int at_line;
    int at;

    if (mline >= 0) {
        line = mline;
        from = mcol + dir;
    } else if (dir > 0) {
        line = top;
        from = 0;
    } else {
        line = top + scr_rows - 1 < nlines ? top + scr_rows - 1 : nlines - 1;
        from = 0x7fff;
    }
    if (doc_find(ui_find_text, dir, line, from, &at_line, &at)) {
        mline = at_line;
        mcol = at;
        if (mline < top || mline >= top + scr_rows) {
            scroll_to(mline - scr_rows / 2);
        }
        cursor_jump(mline, lines[mline].x + mcol);
        LOG("find '%s' at %d:%d", ui_find_text, mline, mcol);

        return;
    }
    mline = -1;
    ui_not_found();
}

/* CTRL+F: the prompt (help.c), then the first match from the top of the
 * screen. */
static void find_prompt(void)
{
    if (ui_find_prompt()) {
        mline = -1;
        find(1);
    }
}

/* --- the keys ---------------------------------------------------------------------- */

/* What a key did: nothing to draw, the cursor or text moved, or more. */
#define DRAW_NONE  0
#define DRAW_ALL   1
#define DRAW_MOVED 2

/* The keys that move the cursor. DRAW_NONE if k is not one. */
static int move_key(int m, key k)
{
    int line = cl;
    int col = cc;

    switch (m) {
    case K_UP:
        cursor_lines(-1, 0);
        break;
    case K_DOWN:
        cursor_lines(1, 0);
        break;
    case K_PGUP:
        cursor_lines(-(scr_rows - 1), 1);
        break;
    case K_PGDN:
        cursor_lines(scr_rows - 1, 1);
        break;
    case K_HOME:
        top = 0;
        cursor_jump(0, 0);
        break;
    case K_END:
        scroll_to(nlines);
        cursor_jump(nlines - 1, 0);
        break;
    case K_LEFT:
        cursor_jump(cl, cc - 1);
        break;
    case K_RIGHT:
        cursor_jump(cl, cc + 1);
        break;
    case K_TAB:
    case K_LINK_NEXT:
    case K_BACKTAB:
    case K_LINK_PREV:
        if (!doc_step_link(m == K_TAB || m == K_LINK_NEXT ? 1 : -1, &line, &col)) {
            return DRAW_NONE;
        }
        cursor_jump(line, col);
        break;
    case K_HEAD_NEXT:
    case K_HEAD_PREV:
        line = doc_step_heading(cl, m == K_HEAD_NEXT ? 1 : -1);
        if (line < 0) {
            return DRAW_NONE;
        }
        cursor_jump(line, 0);
        scroll_to(line);                /* the heading at the top of the screen */
        follow_cursor();
        break;
    case K_CHAR:
        if (k.ch == ' ') {
            cursor_lines(scr_rows - 1, 1);
        } else if (k.ch == 'b') {
            cursor_lines(-(scr_rows - 1), 1);
        } else {
            return DRAW_NONE;
        }
        break;
    default:
        return DRAW_NONE;
    }

    return DRAW_MOVED;
}

/* n, p and u: the next topic, the one before, the section it is in. */
static int topic_key(int ch)
{
    ref_topic tp;

    ref_get_topic(&book, (unsigned)cur, &tp);
    switch (ch) {
    case 'n':
        return (unsigned)cur + tp.span + 1 < book.ntopics
               && view_open((unsigned)cur + tp.span + 1) == 0;
    case 'p':
        return cur > 0 && view_open((unsigned)cur - 1) == 0;
    case 'u':
        return tp.parent != REF_NONE && view_open(tp.parent) == 0;
    }

    return 0;
}

int view_loop(void)
{
    scr_cls();
    draw();
    for (;;) {
        key k = in_wait();
        int m = in_meaning(k);
        int was = top;
        int was_cl = cl;
        int was_h = hscroll;
        int to;
        int what = move_key(m, k);

        if (what == DRAW_NONE) {
            what = DRAW_ALL;
            switch (m) {
            case K_ENTER: {
                doc_link link;

                what = nlines && doc_link_at(cl, cc, &link) && link.target < book.ntopics
                       && view_open(link.target) == 0;
                break;
            }
            case K_BACK:
            case K_HISTBACK:
                to = back_to_view();
                if (to) {
                    return to;
                }
                what = travel(back, &nback, fwd, &nfwd);
                break;
            case K_HISTFWD:
                what = travel(fwd, &nfwd, back, &nback);
                break;
            case K_FIND_HERE:
                find_prompt();
                break;
            case K_FIND_NEXT:
            case K_FIND_PREV:
                if (ui_find_text[0]) {
                    find(m == K_FIND_NEXT ? 1 : -1);
                }
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
                case 'n':
                case 'p':
                case 'u':
                    what = topic_key(k.ch);
                    break;
                default:
                    what = DRAW_NONE;
                }
                break;
            default:
                what = DRAW_NONE;
            }
        }
        if (what == DRAW_MOVED && hscroll == was_h && nlines > 0) {
            draw_moved(was, was_cl);
        } else if (what != DRAW_NONE) {
            draw();
        }
    }
}
