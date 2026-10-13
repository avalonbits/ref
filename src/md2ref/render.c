/*
 * Each topic's markup (docs/FORMAT.md): its heading, its blocks, and --
 * when it is shown without its descendants -- links to its children.
 * Tables are laid out here as ruled columns no wider than TABLE_WIDTH,
 * cells wrapped within them, a cell's styles and links kept on every line
 * of it.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "md2ref.h"
#include "refcore.h"

#define END       0x00
#define SPACE     0x40
#define NOTE      0x80
#define EM_ON     0x10
#define EM_OFF    0x11
#define CODE_ON   0x12
#define CODE_OFF  0x13
#define LINK      0x14
#define LINK_END  0x15
#define BREAK     0x0B
#define NO_LINK   0xFFFF

#define TABLE_WIDTH 76
#define JOIN_LIMIT  16384           /* the most a topic shows with its descendants */
#define TOPIC_MAX   65535
#define MAX_COLS    32
#define CELL_MAX    1024


/* The topic being rendered: grown as it needs, to TOPIC_MAX; and room to
 * compress it. */
static uint8_t* buf;
static unsigned cap;
static unsigned len;
static uint8_t* packing;
static unsigned packing_size;

/* A link's placeholders in the topic being rendered: where, and which. */
typedef struct {
    unsigned pos;
    int link;
} mark;

static mark* marks;
static int nmarks;
static int max_marks;
static unsigned inline_at;          /* where put_inline's markup starts */

/* In a table's cell, which is rendered to styled characters first: its
 * placeholders, and the links they stand for, kept with the characters. */
static int in_cell;
static unsigned cell_pos[64];
static int cell_link[64];
static int ncell;
static int cell_links[256];         /* the topic's: styled.link is 0x8000 | k */
static int ncell_links;

static void add_mark(unsigned pos, int link)
{
    if (nmarks == max_marks) {
        max_marks = max_marks ? max_marks * 2 : 32;
        marks = md_realloc(marks, (unsigned)max_marks * (unsigned)sizeof(mark));
    }
    marks[nmarks].pos = pos;
    marks[nmarks].link = link;
    nmarks++;
}

void markup_pending(int at)
{
    if (in_cell) {
        if (ncell < 64) {
            cell_pos[ncell] = (unsigned)at;
            cell_link[ncell] = last_pending();
            ncell++;
        }
    } else {
        add_mark(inline_at + (unsigned)at, last_pending());
    }
}

/* Room for need bytes: grown 4 KB at a time, as the largest topic sets it
 * for the rest -- doubling would leave as much again unused. */
static __attribute__((noinline)) void room_for(unsigned need)
{
    unsigned more;

    if (need <= cap) {
        return;
    }
    more = (need + 4095) & ~4095u;
    if (more > TOPIC_MAX + 1) {
        more = TOPIC_MAX + 1;
    }
    buf = md_realloc(buf, more);
    cap = more;
}

/* Inlined where it is called: a table is written a byte at a time, and
 * a call and its frame for each byte was a sixth of laying one out. */
static inline __attribute__((always_inline)) void put(int b)
{
    if (len < TOPIC_MAX) {
        if (len >= cap) {
            room_for(len + 1);          /* rarely: a call, and a frame, only then */
        }
        buf[len] = (uint8_t)b;
    }
    len++;
}

static void block_head(int kind, int space, int note, int a, int b)
{
    put(kind | (space ? SPACE : 0) | (note ? NOTE : 0));
    put(a > 255 ? 255 : a);
    put(b > 255 ? 255 : b);
}

/* Inline Markdown's markup is never longer than its text and a little: a
 * link's target is three bytes for at least four of Markdown. Room for
 * twice that. */
static void put_inline(const page* p, int no, const char* s, int words)
{
    unsigned at = len < TOPIC_MAX ? len : TOPIC_MAX;
    unsigned want = at + 2 * (unsigned)strlen(s) + 16;
    int n;

    room_for(want < TOPIC_MAX + 1 ? want : TOPIC_MAX + 1);
    inline_at = at;
    n = inline_markup(p, no, s, buf + at, (int)(cap - at), resolve_link, words);
    if (n > (int)(cap - at)) {
        n = (int)(cap - at);                /* only past TOPIC_MAX: too big, said below */
        len = TOPIC_MAX + 1;
    }
    len += (unsigned)n;
}

static void put_ascii(const char* s)
{
    while (*s) {
        put((unsigned char)*s++);
    }
}

/* --- tables ------------------------------------------------------------------------ */

/* A cell's character, and its style and link: four bytes, and a cell
 * eight, so that a subscript of either is a shift -- at five and six
 * bytes, each was a call to multiply, and laying out agon-docs' tables
 * made a hundred thousand of them. */
#define ST_EM   1
#define ST_CODE 2

typedef struct {
    uint8_t ch;
    uint8_t style;          /* ST_EM, ST_CODE */
    uint16_t link;
} styled;

/* A cell's characters, and what laying it out asks of them, found as they
 * are made: its widest line, its longest word, and whether a line break
 * is in it. Sixteen bytes. */
typedef struct {
    styled* c;
    int n;
    uint16_t widest;
    uint16_t longest;
    uint8_t broken;
    uint8_t pad[5];
} cell;

/* A cell's inline Markdown as styled characters. */
static cell styled_cell(const page* p, int no, const char* s, int slen)
{
    static uint8_t m[CELL_MAX];
    char* text = source_strndup(s, (unsigned)slen);
    int n;
    cell out;
    int em = 0;
    int code = 0;
    uint16_t link = NO_LINK;
    unsigned line_run = 0;
    unsigned word_run = 0;
    int k;

    in_cell = 1;
    ncell = 0;
    n = inline_markup(p, no, text, m, CELL_MAX, resolve_link, WORDS_NONE);
    in_cell = 0;
    if (n > CELL_MAX) {
        n = CELL_MAX;
    }
    out.c = source_alloc((unsigned)(n + 1) * sizeof(styled));
    out.n = 0;
    out.widest = 0;
    out.longest = 0;
    out.broken = 0;
    for (k = 0; k < n; k++) {
        int b = m[k];

        if (b == EM_ON || b == EM_OFF) {
            em = b == EM_ON;
        } else if (b == CODE_ON || b == CODE_OFF) {
            code = b == CODE_ON;
        } else if (b == LINK && k + 2 < n) {
            int j;

            link = (uint16_t)(m[k + 1] | m[k + 2] << 8);
            for (j = 0; j < ncell; j++) {
                if (cell_pos[j] == (unsigned)k + 1 && ncell_links < 256) {
                    cell_links[ncell_links] = cell_link[j];
                    link = (uint16_t)(0x8000 | ncell_links++);
                }
            }
            k += 2;
        } else if (b == LINK_END) {
            link = NO_LINK;
        } else if (b >= 0x20 || b == BREAK) {
            styled* c = &out.c[out.n++];

            c->ch = (uint8_t)(b == BREAK ? '\n' : b);
            c->style = (uint8_t)((em ? ST_EM : 0) | (code ? ST_CODE : 0));
            c->link = link;
            if (b == BREAK) {
                line_run = word_run = 0;
                out.broken = 1;
            } else {
                if (++line_run > out.widest) {
                    out.widest = (uint16_t)line_run;
                }
                if (b == ' ') {
                    word_run = 0;
                } else if (++word_run > out.longest) {
                    out.longest = (uint16_t)word_run;
                }
            }
        }
    }

    return out;
}

/* A cell wrapped to width: where each line starts and ends, into from/to;
 * how many lines. Breaks at spaces; a word wider than the column is cut. */
static int wrap_cell(const cell* c, int width, uint16_t* from, uint16_t* to, int max)
{
    int lines = 0;
    int k = 0;

    while (k <= c->n && lines < max) {
        int start;
        int last_space = -1;
        int j;

        while (k < c->n && c->c[k].ch == ' ') {
            k++;
        }
        start = k;
        for (j = k; j < c->n && c->c[j].ch != '\n' && j - start < width; j++) {
            if (c->c[j].ch == ' ') {
                last_space = j;
            }
        }
        if (j < c->n && c->c[j].ch != '\n' && c->c[j].ch != ' ' && last_space > start) {
            j = last_space;                 /* break at the last space */
        }
        from[lines] = (uint16_t)start;
        to[lines] = (uint16_t)j;
        while (to[lines] > from[lines] && c->c[to[lines] - 1].ch == ' ') {
            to[lines]--;
        }
        lines++;
        if (j >= c->n) {
            break;
        }
        k = c->c[j].ch == '\n' ? j + 1 : j;
    }

    return lines ? lines : 1;
}

/* Styled characters from..to as markup, every style closed at the end. */
static void emit_styled(const cell* c, int from, int to, int force_em)
{
    int em = 0;
    int code = 0;
    uint16_t link = NO_LINK;
    int k;

    for (k = from; k < to; k++) {
        const styled* s = &c->c[k];
        int e = (s->style & ST_EM) || force_em;
        int sc = (s->style & ST_CODE) != 0;

        if (s->link != link) {
            if (code) {
                put(CODE_OFF);
                code = 0;
            }
            if (em) {
                put(EM_OFF);
                em = 0;
            }
            if (link != NO_LINK) {
                put(LINK_END);
            }
            if (s->link != NO_LINK && (s->link & 0x8000)) {
                put(LINK);                  /* a placeholder, again */
                add_mark(len, cell_links[s->link & 0x7FFF]);
                put(NONE & 0xFF);
                put(NONE >> 8);
            } else if (s->link != NO_LINK) {
                put(LINK);
                put(s->link & 0xFF);
                put(s->link >> 8);
            }
            link = s->link;
        }
        if (e != em) {
            put(e ? EM_ON : EM_OFF);
            em = e;
        }
        if (sc != code) {
            put(sc ? CODE_ON : CODE_OFF);
            code = sc;
        }
        put(s->ch);
    }
    if (code) {
        put(CODE_OFF);
    }
    if (em) {
        put(EM_OFF);
    }
    if (link != NO_LINK) {
        put(LINK_END);
    }
}

/* The columns' widths narrowed to fit room, as narrowing the widest that
 * can still give, a character at a time, would leave them -- the first of
 * those as wide when they tie -- but found directly: the level h such that
 * every column at most h + 1 (and none under its minimum) is too wide and
 * at most h is not, then as many as it takes of those at h + 1 to h, the
 * first first. A binary search for h, where a character at a time was
 * the excess times the columns. */
static int capped(const int* w, const int* min, int n, int h)
{
    int total = 0;
    int c;

    for (c = 0; c < n; c++) {
        total += w[c] < h ? w[c] : min[c] > h ? min[c] : h;
    }

    return total;
}

static void fit_widths(int* w, const int* min, int n, int room)
{
    int lo = 0;
    int hi = 0;
    int excess;
    int c;

    for (c = 0; c < n; c++) {
        hi = w[c] > hi ? w[c] : hi;
    }
    if (capped(w, min, n, hi) <= room) {
        return;                             /* it fits as it is */
    }
    if (capped(w, min, n, 0) > room) {
        for (c = 0; c < n; c++) {
            w[c] = min[c];                  /* it never fits: all as narrow as they go */
        }

        return;
    }
    while (lo + 1 < hi) {                   /* capped(lo) fits, capped(hi) does not */
        int mid = lo + (int)((unsigned)(hi - lo) >> 1);

        if (capped(w, min, n, mid) <= room) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    excess = capped(w, min, n, lo + 1) - room;
    for (c = 0; c < n; c++) {
        int was = w[c];

        w[c] = was < lo + 1 ? was : min[c] > lo + 1 ? min[c] : lo + 1;
        if (excess > 0 && w[c] == lo + 1 && min[c] <= lo) {
            w[c] = lo;
            excess--;
        }
    }
}

static void table(const page* p, const block* b)
{
    static const char* cells[MAX_COLS];
    static int lens[MAX_COLS];
    static int natural[MAX_COLS];
    static int minimum[MAX_COLS];
    static int widths[MAX_COLS];
    static uint16_t from[MAX_COLS][64];     /* 128 bytes a column: a shift */
    static uint16_t to[MAX_COLS][64];
    static int nlines[MAX_COLS];
    const char* s = b->text;
    int nrows = 0;
    int ncols = 0;
    cell* grid;
    cell* row;
    int r;
    int c;
    int wrapped = 0;

    for (r = 0; s[r]; r++) {
        nrows += s[r] == '\n';
    }
    /* As many columns as the first row has. */
    ncols = split_row(s, (int)(strchr(s, '\n') - s), cells, lens, MAX_COLS);
    grid = source_alloc((unsigned)(nrows * ncols + 1) * sizeof(cell));
    for (r = 0, row = grid; r < nrows; r++, row += ncols) {     /* a row at a time: no multiply */
        const char* e = strchr(s, '\n');
        int n = split_row(s, (int)(e - s), cells, lens, MAX_COLS);

        for (c = 0; c < ncols; c++) {
            row[c] = c < n ? styled_cell(p, b->no + (r ? r + 1 : 0), cells[c], lens[c])
                           : styled_cell(p, b->no, "", 0);
        }
        s = e + 1;
    }

    /* Widths: natural, then the widest column gives way first, down to its
     * longest word (at most 20) or 10. */
    for (c = 0; c < ncols; c++) {
        int longest = 1;

        natural[c] = 1;
        for (r = 0, row = grid; r < nrows; r++, row += ncols) {
            int w = row[c].widest;
            int lw = row[c].longest;

            if (w > natural[c]) {
                natural[c] = w;
            }
            if (lw > longest) {
                longest = lw;
            }
        }
        minimum[c] = longest < 20 ? longest : 20;
        if (minimum[c] < 10) {
            minimum[c] = 10;
        }
        if (minimum[c] > natural[c]) {
            minimum[c] = natural[c];
        }
        widths[c] = natural[c];
    }
    fit_widths(widths, minimum, ncols, TABLE_WIDTH - 3 * (ncols - 1));

    /* Any body row wrapped? Then rows are ruled apart. A cell wraps -- to
     * more than one line, as wrap_cell lays it out -- when it is wider
     * than its column or a line break is in it: it never starts with a
     * space, the copy of its text having dropped any. */
    for (r = 1, row = grid + ncols; r < nrows; r++, row += ncols) {
        for (c = 0; c < ncols; c++) {
            if (row[c].n > widths[c] || row[c].broken) {
                wrapped = 1;
            }
        }
    }

    for (r = 0, row = grid; r < nrows; r++, row += ncols) {
        int height = 1;
        int line;

        if (r == 1 || (wrapped && r > 1)) {
            for (c = 0; c < ncols; c++) {
                int k;

                if (c) {
                    put('-');
                    put('+');
                    put('-');
                }
                for (k = 0; k < widths[c]; k++) {
                    put('-');
                }
            }
            put('\n');
        }
        for (c = 0; c < ncols; c++) {
            nlines[c] = wrap_cell(&row[c], widths[c], from[c], to[c], 64);
            if (nlines[c] > height) {
                height = nlines[c];
            }
        }
        for (line = 0; line < height; line++) {
            unsigned line_start = len;
            int pad = 0;

            for (c = 0; c < ncols; c++) {
                int shown = line < nlines[c] ? to[c][line] - from[c][line] : 0;

                if (c) {
                    while (pad-- > 0) {
                        put(' ');
                    }
                    put(' ');
                    put('|');
                    put(' ');
                }
                if (line < nlines[c]) {
                    emit_styled(&row[c], from[c][line], to[c][line], r == 0);
                }
                pad = widths[c] - shown;
            }
            /* No spaces at the end of a line. */
            while (len > line_start && len <= TOPIC_MAX && buf[len - 1] == ' ') {
                len--;
            }
            put('\n');
        }
    }
}

/* What a block's words are, as it is written: every one, with -f; else
 * all of a heading's, and of a block emphasised whole (a note's title),
 * and only the names of the rest. */
static int words_for(const block* b)
{
    return md_full_text || b->kind == B_HEAD || b->em ? WORDS_ALL : WORDS_NAMES;
}

/* The words of the blocks rendered from start, read from their markup --
 * all, or only the names -- for those whose words are not taken as they
 * are written. */
static void words_of(unsigned start, int words)
{
    if (len <= TOPIC_MAX) {
        if (words == WORDS_ALL) {
            words_markup(buf + start, len - start);
        } else {
            words_markup_names(buf + start, len - start);
        }
    }
}

/* --- blocks and topics ---------------------------------------------------------------- */

/* A code block's text: as put_inline puts inline Markdown, its words
 * taken as it is written. Never longer than its text and a newline. */
static void code(const char* s)
{
    unsigned at = len < TOPIC_MAX ? len : TOPIC_MAX;
    unsigned want = at + (unsigned)strlen(s) + 2;
    int n;

    room_for(want < TOPIC_MAX + 1 ? want : TOPIC_MAX + 1);
    n = code_markup(s, buf + at, (int)(cap - at), md_full_text ? WORDS_ALL : WORDS_NONE);
    if (n > (int)(cap - at)) {
        n = (int)(cap - at);                /* only past TOPIC_MAX: too big, said below */
        len = TOPIC_MAX + 1;
    }
    len += (unsigned)n;
}

static void one_block(const page* p, const block* b)
{
    switch (b->kind) {
    case B_PARA:
        block_head(B_PARA, b->space, b->note, b->indent, b->hang);
        if (b->marker) {
            put_ascii(b->marker);
        }
        if (b->em) {
            put(EM_ON);
        }
        put_inline(p, b->no, b->text, words_for(b));
        if (b->em) {
            put(EM_OFF);
        }
        break;
    case B_HEAD:
        block_head(B_HEAD, b->space, b->note, b->level, 0);
        put_inline(p, b->no, b->text, WORDS_ALL);
        break;
    case B_PRE:
        block_head(B_PRE, b->space, b->note, b->indent, 0);
        code(b->text);
        break;
    case B_TABLE: {
        unsigned start = len;

        block_head(B_PRE, b->space, b->note, b->indent, 1);
        table(p, b);
        put(END);
        words_of(start, md_full_text ? WORDS_ALL : WORDS_NAMES);  /* read from it, laid out */

        return;
    }
    case B_RULE:
        block_head(B_RULE, b->space, b->note, b->indent, b->indent);
        break;
    }
    put(END);
}

/* --- topics, held until complete ------------------------------------------------- */

#define DEPTH 9

typedef struct {
    int topic;
    unsigned long total;    /* its and its descendants' markup's length */
    unsigned descendants;
    uint8_t* text;          /* its markup, as it is: packed only as it is written */
    unsigned len;
    mark* marks;            /* its placeholders: where in its markup, which link */
    int nmarks;
} held;

static held hold[DEPTH];
static int nheld;

/* The page being rendered: its topics' markup's length and how many, all a
 * top made for it later shows with it (render_top). */
static int own_page = -1;
static unsigned long page_own;
static unsigned page_topics;

unsigned view_size;
unsigned max_packed;

/* src, len bytes of it, compressed into packing (-z): its length.
 * Placeholders stay literal, and fixed_at says where each landed. */
static unsigned* fixed;
static unsigned* fixed_at;
static int max_fixed;

static unsigned pack(const uint8_t* src, const mark* m, int n)
{
    unsigned room = len + len / 255 + 16;
    int k;

    if (room > packing_size) {
        packing_size = room;
        packing = md_realloc(packing, packing_size);
    }
    if (n > max_fixed) {
        max_fixed = n + 16;
        fixed = md_realloc(fixed, (unsigned)max_fixed * (unsigned)sizeof(unsigned));
        fixed_at = md_realloc(fixed_at, (unsigned)max_fixed * (unsigned)sizeof(unsigned));
    }
    for (k = 0; k < n; k++) {
        fixed[k] = m[k].pos;
    }

    return lz4_compress(src, len, packing, room, fixed, n, fixed_at);
}

/* The words of len bytes of buf from start. */
static void take_words(int t, unsigned start, const char* title)
{
    words_begin(t);
    if (title) {
        words_text(title, 1);           /* first: a word's posting says it is in the title */
    }
    if (md_full_text) {
        words_markup(buf + start, len - start);
    } else {
        words_markup_names(buf + start, len - start);   /* links, all but its heading */
    }
    words_end();
}

static int too_big(int t)
{
    if (len > TOPIC_MAX) {
        md_error(pages[TOPIC(t).page].path, 0, "topic '%s' is too big (%u bytes, at most %u)",
                 TOPIC(t).title, len, (unsigned)TOPIC_MAX);
        len = 0;
        nmarks = 0;

        return 1;
    }

    return 0;
}

/* The topic being rendered, begun at its heading (or, for a page's top,
 * at its title), each block added as it is parsed, and ended as the next
 * topic begins: so only a run's blocks are ever kept, not a topic's. */
static int cur_topic = -1;

void render_begin(int t, const block* head)
{
    topic* tp = &TOPIC(t);

    cur_topic = t;
    len = 0;
    nmarks = 0;
    ncell_links = 0;
    words_begin(t);
    words_text(tp->title, 1);           /* first: a word's posting says it is in the title */
    block_head(B_HEAD, 1, 0, tp->level >= 1 ? (tp->level > 4 ? 4 : tp->level) : 1, 0);
    if (tp->heading) {
        put_inline(&pages[tp->page], head->no, head->text, WORDS_ALL);
        put(END);
    } else {
        const char* s = tp->title;

        while (*s) {
            put(agon_char(&s));
        }
        put(END);
        words_of(0, WORDS_ALL);
    }
}

void render_block(const block* b)
{
    one_block(&pages[TOPIC(cur_topic).page], b);
}

/* The topic rendered, held -- its markup copied, as the buffer stays as
 * big as the biggest topic has made it: grown afresh for each, it left
 * the heap in pieces -- until its descendants are read. */
void render_end(void)
{
    int t = cur_topic;
    topic* tp = &TOPIC(t);
    held* h;
    int k;

    words_end();
    too_big(t);

    /* Its size counts in its own and its ancestors', which are held, and
     * in its page's, for a top the page may yet be given. */
    if (tp->page != own_page) {
        own_page = tp->page;
        page_own = 0;
        page_topics = 0;
    }
    page_own += len;
    page_topics++;
    for (k = 0; k < nheld; k++) {
        hold[k].total += len;
        hold[k].descendants++;
    }
    h = &hold[nheld < DEPTH ? nheld++ : DEPTH - 1];
    h->topic = t;
    h->total = len;
    h->descendants = 0;
    h->text = md_realloc(NULL, len + 1);
    memcpy(h->text, buf, len);
    h->len = len;
    h->nmarks = nmarks;
    h->marks = NULL;
    if (nmarks) {
        h->marks = md_realloc(NULL, (unsigned)nmarks * (unsigned)sizeof(mark));
        memcpy(h->marks, marks, (unsigned)nmarks * sizeof(mark));
    }
    cur_topic = -1;
}

void render_topic(int t, const block* b, int n)
{
    int k;

    render_begin(t, b);
    for (k = TOPIC(t).heading ? 1 : 0; k < n; k++) {
        render_block(&b[k]);
    }
    render_end();
}

/* Links to a topic's children, after its markup: for a topic too big to
 * show with them. */
static void add_menu(int t)
{
    unsigned start = len;
    int c;

    block_head(B_PARA, 1, 0, 0, 0);
    put(EM_ON);
    put_ascii("In this section:");
    put(EM_OFF);
    put(END);
    for (c = TOPIC(t).first_child; c >= 0; c = TOPIC(c).next) {
        const char* s = TOPIC(c).title;

        block_head(B_PARA, c == TOPIC(t).first_child, 0, 2, 4);
        put_ascii("- ");
        put(LINK);
        add_mark(len, LINK_TO(TOPIC(c).made));    /* it may yet move up */
        put(NONE & 0xFF);
        put(NONE >> 8);
        while (*s) {
            put(agon_char(&s));
        }
        put(LINK_END);
        put(END);
    }
    take_words(t, start, NULL);
}

/* A held topic complete: links to its children if it is too big to show
 * with them, and written -- stored as it is, an LZ4 block of literals, or
 * with -z compressed -- its placeholders where write_book patches them. */
static void complete(held* h)
{
    topic* tp = &TOPIC(h->topic);
    const uint8_t* text = h->text;
    const mark* m = h->marks;
    int nm = h->nmarks;
    unsigned long where;
    unsigned packed;
    unsigned head = 0;
    int k;

    tp->span = h->total <= JOIN_LIMIT ? (int16_t)h->descendants : 0;
    len = h->len;
    if (tp->span == 0 && tp->nchildren) {
        room_for(len + 1);              /* none is being rendered: the buffer is free */
        memcpy(buf, h->text, len);
        nmarks = 0;
        for (k = 0; k < h->nmarks; k++) {
            add_mark(h->marks[k].pos, h->marks[k].link);
        }
        add_menu(h->topic);
        too_big(h->topic);
        text = buf;
        m = marks;
        nm = nmarks;
    }
    if (md_compress) {
        packed = pack(text, m, nm);
        where = write_text(packing, packed);
    } else {
        static uint8_t lits[2 + TOPIC_MAX / 255];

        head = lz4_literals(lits, len);
        where = write_text(lits, head);
        write_text(text, len);
        packed = head + len;
    }
    if (packed > 0xFFFF) {
        md_error(pages[tp->page].path, 0, "topic '%s' is too big", tp->title);
    }
    tp->text_at = where;
    tp->packed = (uint16_t)packed;
    tp->len = len;
    if (packed > max_packed) {
        max_packed = packed;
    }
    for (k = 0; k < nm; k++) {
        int link = m[k].link;
        unsigned at = md_compress ? fixed_at[k] : head + m[k].pos;

        if (link >= 0) {                /* waiting for its page to be read */
            link_placed(link, add_patch(where + at, PATCH_WAITING));
        } else {                        /* a topic of this page, numbered when it is read */
            add_patch(where + at, PATCH_MADE + (unsigned)(-2 - link));
        }
    }
    free(h->text);
    free(h->marks);
}

void render_top(int t)
{
    int held_now = nheld;
    int k;

    for (k = 0; k < nheld; k++) {
        if (hold[k].topic >= t) {
            hold[k].topic++;
        }
    }
    /* Rendered -- its title alone -- into a place made beneath the rest,
     * none of which it counts in, as they are its descendants; all the
     * page has rendered counts in it. */
    unsigned long own = page_own;
    unsigned topics_so_far = page_topics;

    memmove(&hold[1], &hold[0], (unsigned)nheld * sizeof(held));
    nheld = 0;
    render_topic(t, NULL, 0);
    nheld = held_now + 1;
    hold[0].total += own;
    hold[0].descendants += topics_so_far;
}

void render_complete(int parent)
{
    while (nheld > 0 && hold[nheld - 1].topic != parent) {
        complete(&hold[--nheld]);
    }
}

int render_done(void)
{
    int t;

    free(buf);
    buf = NULL;
    cap = 0;
    free(packing);
    packing = NULL;
    packing_size = 0;
    view_size = 0;
    for (t = 0; t < ntopics; t++) {
        unsigned long v = 0;
        int k;

        for (k = t; k <= t + TOPIC(t).span; k++) {
            v += TOPIC(k).len;
        }
        if (v > view_size) {
            view_size = v > 0xFFFFFUL ? 0xFFFFF : (unsigned)v;
        }
    }
    if (view_size > 65535) {
        md_error(pages[0].path, 0, "a topic shows too much (%u bytes)", view_size);

        return -1;
    }

    return md_errors ? -1 : 0;
}
