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

text* markup;
unsigned view_size;

/* The topic being rendered: grown as it needs, to TOPIC_MAX; and room to
 * compress it. */
static uint8_t* buf;
static unsigned cap;
static unsigned len;
static uint8_t* packing;
static unsigned packing_size;

static void room_for(unsigned need)
{
    unsigned more = cap ? cap : 4096;

    if (need <= cap) {
        return;
    }
    while (more < need) {
        more *= 2;
    }
    if (more > TOPIC_MAX + 1) {
        more = TOPIC_MAX + 1;
    }
    buf = md_realloc(buf, more);
    cap = more;
}

static void put(int b)
{
    if (len < TOPIC_MAX) {
        room_for(len + 1);
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

/* Inline Markdown's markup is about as long as its text: room for that, and
 * if it took more, more room and again -- quietly, as anything wrong in it
 * was said the first time. */
static void put_inline(const page* p, int no, const char* s)
{
    unsigned at = len < TOPIC_MAX ? len : TOPIC_MAX;
    int n;

    room_for(at + (unsigned)strlen(s) + 16);
    n = inline_markup(p, no, s, buf + at, (int)(cap - at), resolve_link);
    if (n > (int)(cap - at) && cap < TOPIC_MAX + 1) {
        int quiet = md_quiet;

        room_for(at + (unsigned)n);
        md_quiet = 1;
        n = inline_markup(p, no, s, buf + at, (int)(cap - at), resolve_link);
        md_quiet = quiet;
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

typedef struct {
    uint8_t ch;
    uint8_t em;
    uint8_t code;
    uint16_t link;
} styled;

typedef struct {
    styled* c;
    int n;
} cell;

/* A cell's inline Markdown as styled characters. */
static cell styled_cell(const page* p, int no, const char* s, int slen)
{
    static uint8_t m[CELL_MAX];
    char* text = arena_strndup(s, (unsigned)slen);
    int n = inline_markup(p, no, text, m, CELL_MAX, resolve_link);
    cell out;
    int em = 0;
    int code = 0;
    uint16_t link = NO_LINK;
    int k;

    if (n > CELL_MAX) {
        n = CELL_MAX;
    }
    out.c = arena_alloc((unsigned)(n + 1) * sizeof(styled));
    out.n = 0;
    for (k = 0; k < n; k++) {
        int b = m[k];

        if (b == EM_ON || b == EM_OFF) {
            em = b == EM_ON;
        } else if (b == CODE_ON || b == CODE_OFF) {
            code = b == CODE_ON;
        } else if (b == LINK && k + 2 < n) {
            link = (uint16_t)(m[k + 1] | m[k + 2] << 8);
            k += 2;
        } else if (b == LINK_END) {
            link = NO_LINK;
        } else if (b >= 0x20 || b == BREAK) {
            styled* c = &out.c[out.n++];

            c->ch = (uint8_t)(b == BREAK ? '\n' : b);
            c->em = (uint8_t)em;
            c->code = (uint8_t)code;
            c->link = link;
        }
    }

    return out;
}

static int longest_word(const cell* c)
{
    int best = 0;
    int run = 0;
    int k;

    for (k = 0; k < c->n; k++) {
        if (c->c[k].ch == ' ' || c->c[k].ch == '\n') {
            run = 0;
        } else if (++run > best) {
            best = run;
        }
    }

    return best;
}

static int widest_line(const cell* c)
{
    int best = 0;
    int run = 0;
    int k;

    for (k = 0; k < c->n; k++) {
        if (c->c[k].ch == '\n') {
            run = 0;
        } else if (++run > best) {
            best = run;
        }
    }

    return best;
}

/* A cell wrapped to width: where each line starts and ends, into from/to;
 * how many lines. Breaks at spaces; a word wider than the column is cut. */
static int wrap_cell(const cell* c, int width, int* from, int* to, int max)
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
        from[lines] = start;
        to[lines] = j;
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
        int e = s->em || force_em;

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
            if (s->link != NO_LINK) {
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
        if (s->code != code) {
            put(s->code ? CODE_ON : CODE_OFF);
            code = s->code;
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

static void table(const page* p, const block* b)
{
    static const char* cells[MAX_COLS];
    static int lens[MAX_COLS];
    static int natural[MAX_COLS];
    static int minimum[MAX_COLS];
    static int widths[MAX_COLS];
    static int from[MAX_COLS][64];
    static int to[MAX_COLS][64];
    static int nlines[MAX_COLS];
    const char* s = b->text;
    int nrows = 0;
    int ncols = 0;
    cell* grid;
    int r;
    int c;
    int room;
    int wrapped = 0;

    for (r = 0; s[r]; r++) {
        nrows += s[r] == '\n';
    }
    grid = arena_alloc((unsigned)(nrows * MAX_COLS + 1) * sizeof(cell));
    for (r = 0; r < nrows; r++) {
        const char* e = strchr(s, '\n');
        int n = split_row(s, (int)(e - s), cells, lens, MAX_COLS);

        if (r == 0) {
            ncols = n;
        }
        for (c = 0; c < ncols; c++) {
            grid[r * MAX_COLS + c] = c < n ? styled_cell(p, b->no + (r ? r + 1 : 0), cells[c], lens[c])
                                           : styled_cell(p, b->no, "", 0);
        }
        s = e + 1;
    }

    /* Widths: natural, then the widest column gives way first, down to its
     * longest word (at most 20) or 10. */
    for (c = 0; c < ncols; c++) {
        int longest = 1;

        natural[c] = 1;
        for (r = 0; r < nrows; r++) {
            int w = widest_line(&grid[r * MAX_COLS + c]);
            int lw = longest_word(&grid[r * MAX_COLS + c]);

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
    room = TABLE_WIDTH - 3 * (ncols - 1);
    for (;;) {
        int total = 0;
        int k = -1;

        for (c = 0; c < ncols; c++) {
            total += widths[c];
            if (widths[c] > minimum[c] && (k < 0 || widths[c] > widths[k])) {
                k = c;
            }
        }
        if (total <= room || k < 0) {
            break;
        }
        widths[k]--;
    }

    /* Any body row wrapped? Then rows are ruled apart. */
    for (r = 1; r < nrows; r++) {
        for (c = 0; c < ncols; c++) {
            if (wrap_cell(&grid[r * MAX_COLS + c], widths[c], from[c], to[c], 64) > 1) {
                wrapped = 1;
            }
        }
    }

    for (r = 0; r < nrows; r++) {
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
            nlines[c] = wrap_cell(&grid[r * MAX_COLS + c], widths[c], from[c], to[c], 64);
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
                    emit_styled(&grid[r * MAX_COLS + c], from[c][line], to[c][line], r == 0);
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

/* --- blocks and topics ---------------------------------------------------------------- */

static void code(const char* s)
{
    while (*s) {
        if (*s == '\n') {
            put('\n');
            s++;
        } else {
            put(agon_char(&s));
        }
    }
    put('\n');
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
        put_inline(p, b->no, b->text);
        if (b->em) {
            put(EM_OFF);
        }
        break;
    case B_HEAD:
        block_head(B_HEAD, b->space, b->note, b->level, 0);
        put_inline(p, b->no, b->text);
        break;
    case B_PRE:
        block_head(B_PRE, b->space, b->note, b->indent, 0);
        code(b->text);
        break;
    case B_TABLE:
        block_head(B_PRE, b->space, b->note, b->indent, 1);
        table(p, b);
        break;
    case B_RULE:
        block_head(B_RULE, b->space, b->note, b->indent, b->indent);
        break;
    }
    put(END);
}

/* What is in buf kept as topic t's markup, compressed -- unless it is too
 * big. Compressed, the markup of a document fits the Agon. */
static void keep(int t)
{
    unsigned room = len + len / 255 + 16;
    unsigned n;

    if (len > TOPIC_MAX) {
        md_error(pages[topics[t].page].path, 0, "topic '%s' is too big (%u bytes, at most %u)",
                 topics[t].title, len, (unsigned)TOPIC_MAX);
        len = 0;
    }
    if (room > packing_size) {
        packing_size = room;
        packing = md_realloc(packing, packing_size);
    }
    n = lz4_compress(buf, len, packing, room);
    markup[t].packed = arena_alloc(n);
    memcpy(markup[t].packed, packing, n);
    markup[t].packed_len = n;
    markup[t].len = len;
}

void unpack(int t, uint8_t* out)
{
    ref_lz4(markup[t].packed, markup[t].packed_len, out, markup[t].len, NULL, 0);
}

/* A topic's own markup: its heading, and its blocks. */
static void render_topic(int t)
{
    const topic* tp = &topics[t];
    const page* p = &pages[tp->page];
    arena_mark scratch = arena_here();      /* tables' cells, styled */
    int k;

    len = 0;
    block_head(B_HEAD, 1, 0, tp->level >= 1 ? (tp->level > 4 ? 4 : tp->level) : 1, 0);
    if (tp->heading) {
        put_inline(p, p->blocks[tp->first_block - 1].no, tp->heading);
    } else {
        const char* s = tp->title;

        while (*s) {
            put(agon_char(&s));
        }
    }
    put(END);
    for (k = tp->first_block; k < tp->end_block; k++) {
        one_block(p, &p->blocks[k]);
    }
    arena_back(scratch);
    keep(t);
}

/* Links to a topic's children, after its markup: for a topic too big to
 * show with them. */
static void add_menu(int t)
{
    const topic* tp = &topics[t];
    int k;

    len = 0;
    room_for(markup[t].len + 1);
    unpack(t, buf);
    len = markup[t].len;
    block_head(B_PARA, 1, 0, 0, 0);
    put(EM_ON);
    put_ascii("In this section:");
    put(EM_OFF);
    put(END);
    for (k = 0; k < tp->nchildren; k++) {
        int c = tp->children[k];
        const char* s = topics[c].title;

        block_head(B_PARA, k == 0, 0, 2, 4);
        put_ascii("- ");
        put(LINK);
        put(c & 0xFF);
        put(c >> 8);
        while (*s) {
            put(agon_char(&s));
        }
        put(LINK_END);
        put(END);
    }
    keep(t);
}

void render_page(int pi)
{
    const page* p = &pages[pi];
    int t;

    if (!markup) {
        markup = arena_alloc((unsigned)ntopics * sizeof(text));
    }
    for (t = p->first_topic; t < p->first_topic + p->ntopics; t++) {
        render_topic(t);
    }
}

int render_done(void)
{
    unsigned* total = md_realloc(NULL, (unsigned)ntopics * (unsigned)sizeof(unsigned));
    int* descendants = md_realloc(NULL, (unsigned)ntopics * (unsigned)sizeof(int));
    int t;

    /* Each subtree's size, children before parents (they come after them). */
    for (t = 0; t < ntopics; t++) {
        total[t] = markup[t].len;
        descendants[t] = 0;
    }
    for (t = ntopics - 1; t >= 0; t--) {
        int k;

        for (k = 0; k < topics[t].nchildren; k++) {
            int c = topics[t].children[k];

            total[t] += total[c];
            descendants[t] += descendants[c] + 1;
        }
    }
    for (t = 0; t < ntopics; t++) {
        topics[t].span = total[t] <= JOIN_LIMIT ? descendants[t] : 0;
        if (topics[t].span == 0 && topics[t].nchildren) {
            add_menu(t);
        }
    }
    free(total);
    free(descendants);
    free(buf);
    buf = NULL;
    cap = 0;
    free(packing);
    packing = NULL;
    packing_size = 0;

    view_size = 0;
    for (t = 0; t < ntopics; t++) {
        unsigned v = 0;
        int k;

        for (k = t; k <= t + topics[t].span; k++) {
            v += markup[k].len;
        }
        if (v > view_size) {
            view_size = v;
        }
    }
    if (view_size > 65535) {
        md_error(pages[0].path, 0, "a topic shows too much (%u bytes)", view_size);

        return -1;
    }

    return md_errors ? -1 : 0;
}

unsigned markup_plain(const uint8_t* m, unsigned n, char* out, unsigned size)
{
    unsigned k = 0;
    unsigned o = 0;

    while (k < n && o + 1 < size) {
        k += 3;                             /* the block's head */
        while (k < n && m[k] != END && o + 1 < size) {
            uint8_t c = m[k];

            if (c == LINK) {
                k += 3;
                continue;
            }
            if (c == BREAK) {
                out[o++] = ' ';
            } else if (c >= 0x20 || c == '\n') {
                out[o++] = (char)c;
            }
            k++;
        }
        k++;
        out[o++] = '\n';
    }
    out[o] = 0;

    return o;
}
