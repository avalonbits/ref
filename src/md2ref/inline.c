/*
 * A block's text, inline Markdown, into ref's markup: code spans, emphasis
 * (strong and em alike), links resolved to topics (an external one kept as
 * its text and its URL), images as a note of what they were, line breaks,
 * footnote references, backslash escapes and a few HTML tags. Text goes
 * into the Agon's character set; runs of spaces become one.
 */
#include <string.h>

#include "md2ref.h"

#define EM_ON    0x10
#define EM_OFF   0x11
#define CODE_ON  0x12
#define CODE_OFF 0x13
#define LINK     0x14
#define LINK_END 0x15
#define BREAK    0x0B

static const char ESCAPABLE[] = "\\`*_{}[]()#+-.!|<>~\"'$&%=:;,/@^";

/* --- characters ------------------------------------------------------------- */

/* The next character of UTF-8 text at *s in the Agon's character set (its
 * system font is Windows-1252, nearly), *s moved past it. */
int agon_char(const char** s)
{
    const unsigned char* p = (const unsigned char*)*s;
    unsigned c = p[0];
    int n = 1;

    if (c >= 0x20 && c < 0x7F) {
        *s += 1;                    /* most of every page: as it is */

        return (int)c;
    }
    if (c >= 0xF0) {
        c = '?';
        n = 4;
    } else if (c >= 0xE0) {
        c = (c & 0x0F) << 12 | (p[1] & 0x3F) << 6 | (p[2] & 0x3F);
        n = 3;
    } else if (c >= 0xC0) {
        c = (c & 0x1F) << 6 | (p[1] & 0x3F);
        n = 2;
    } else if (c >= 0x80) {
        c = '?';                    /* not UTF-8: a stray byte */
    }
    while (n > 1 && !p[n - 1]) {
        n--;                        /* cut short at the end */
    }
    *s += n;
    switch (c) {
    case 0x2018:
    case 0x2019:
        return '\'';
    case 0x201C:
    case 0x201D:
        return '"';
    case 0x2013:
    case 0x2014:
        return '-';
    case 0x2022:
        return '*';
    case 0x2190:
    case 0x21D0:
        return 0x8F;                /* the font's arrows */
    case 0x2192:
    case 0x21E8:
        return 0x90;
    case 0x2191:
    case 0x21E7:
        return 0x8D;
    case 0x2193:
    case 0x21D3:
        return 'v';
    case 0xA0:
    case '\t':
    case '\n':
        return ' ';
    }
    if ((c >= 0x20 && c < 0x7F) || (c >= 0xA1 && c <= 0xFF)) {
        return (int)c;
    }

    return '?';
}

/* --- output -------------------------------------------------------------------- */

static uint8_t* out;
static int room;
static int written;
static int last_space;          /* the last byte written was a space */

static void put(int b)
{
    if (written < room) {
        out[written] = (uint8_t)b;
    }
    written++;
    if (b >= 0x20) {
        last_space = b == ' ';
    }
}

static void put_char(int c)
{
    if (c == ' ') {
        if (last_space) {
            return;                 /* runs of spaces are one */
        }
    }
    put(c);
}

static int plain_run(const char* text, int k, int n, const uint8_t* stop);
static uint8_t not_ascii[256];      /* only what agon_char changes */

static void put_text(const char* s, int len)
{
    int k = 0;

    while (k < len) {
        k = plain_run(s, k, len, not_ascii);
        if (k < len) {
            const char* p = s + k;

            put_char(agon_char(&p));
            k = (int)(p - s);
        }
    }
}

/* The bytes that are not plain text as they are: what inlines looks at,
 * controls, and what agon_char changes. */
static uint8_t special[256];

static void make_special(void)
{
    const char* m = "\\`<![*_";
    int c;

    for (c = 0; c < 256; c++) {
        not_ascii[c] = special[c] = (uint8_t)(c < 0x20 || c >= 0x7F);
    }
    while (*m) {
        special[(uint8_t)*m++] = 1;
    }
}

/* Text from k on, to the next byte stop says is not plain: most of every
 * page, put here in a tight loop -- a space after a space is dropped, as
 * put_char does -- and not a call or two a byte. Where it stops. */
static int plain_run(const char* text, int k, int n, const uint8_t* stop)
{
    const uint8_t* s = (const uint8_t*)text + k;
    const uint8_t* e = (const uint8_t*)text + n;
    uint8_t* o;
    uint8_t* end;
    uint8_t sp = (uint8_t)last_space;

    if ((unsigned)written + (unsigned)(n - k) > (unsigned)room) {
        /* Past the room -- a topic too big, which is an error anyway: as
         * put_char, a byte at a time, counting what does not fit. */
        while (s < e && !stop[*s]) {
            uint8_t c = *s++;

            if (c == ' ') {
                if (sp) {
                    continue;
                }
                sp = 1;
            } else {
                sp = 0;
            }
            if (written < room) {
                out[written] = c;
            }
            written++;
        }
        last_space = sp;

        return (int)((const char*)s - text);
    }
    /* Room for all of it: through pointers, compared unsigned -- an int's
     * < is a call to fix its flags, on every byte. */
    o = out + written;
    end = o;
    while (s < e && !stop[*s]) {
        uint8_t c = *s++;

        if (c == ' ') {
            if (sp) {
                continue;
            }
            sp = 1;
        } else {
            sp = 0;
        }
        *end++ = c;
    }
    written += (int)(end - o);
    last_space = sp;

    return (int)((const char*)s - text);
}

/* --- scanning --------------------------------------------------------------------- */

static int run_of(const char* s, int k, int n, char c)
{
    int r = 0;

    while (k + r < n && s[k + r] == c) {
        r++;
    }

    return r;
}

/* --- an index of the text ---------------------------------------------------- */

/* What the scans below would otherwise look for again from every opener:
 * the text's backtick runs, by length and position; and which ] closes
 * each [, and which ) each (, as the scans pair them. So a scan is a
 * binary search, not the rest of the text -- each [, * and ` that never
 * closes was a scan to the end, and a block of them the block squared. */
typedef struct {
    uint16_t len;
    uint16_t pos;
} tick_run;

#define NO_PAIR 0xFFFF

static const char* ix_base;         /* the text indexed */
static int ix_n;
static const char* ix_text;         /* the text to index, should the scans need it */
static int ix_text_n;
static long scan_left;              /* bytes the scans may yet look at before it is indexed */
static tick_run* ticks;             /* by length, then position */
static int nticks;
static uint16_t* br_open;           /* each [ in order, and its ] or NO_PAIR */
static uint16_t* br_close;
static int nbr;
static uint16_t* par_open;          /* each ( in order, and its ) or NO_PAIR */
static uint16_t* par_close;
static int npar;
static uint16_t* stack;
static int ticks_cap;               /* each grown as the text has more, not to its length */
static int br_cap;
static int par_cap;

static int closing_ticks(const char* s, int k, int n, int r);

static int by_tick(const void* a, const void* b)
{
    const tick_run* x = a;
    const tick_run* y = b;

    return x->len != y->len ? (int)x->len - (int)y->len : (int)x->pos - (int)y->pos;
}

/* The ordinal of the pair whose opener is at pos, in open[0..n), or -1. */
static int pair_of(const uint16_t* open, int n, unsigned pos)
{
    int lo = 0;
    int hi = n;

    while (lo < hi) {
        int mid = lo + (int)((unsigned)(hi - lo) >> 1);

        if (open[mid] < pos) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }

    return lo < n && open[lo] == pos ? lo : -1;
}

static uint8_t ix_kind[256];
static uint16_t* pstack;

/* A ( or ) at j, into the parens' pairs. */
static void paren(char c, int j, int* depth)
{
    if (c == '(') {
        if (npar == par_cap) {
            par_cap = par_cap ? par_cap * 2 : 32;
            par_open = md_realloc(par_open, (unsigned)par_cap * (unsigned)sizeof(uint16_t));
            par_close = md_realloc(par_close, (unsigned)par_cap * (unsigned)sizeof(uint16_t));
            pstack = md_realloc(pstack, (unsigned)par_cap * (unsigned)sizeof(uint16_t));
        }
        par_open[npar] = (uint16_t)j;
        par_close[npar] = NO_PAIR;
        pstack[(*depth)++] = (uint16_t)npar++;
    } else if (c == ')' && *depth > 0) {
        par_close[pstack[--*depth]] = (uint16_t)j;
    }
}

static void index_text(const char* s, int n)
{
    int depth = 0;
    int pdepth = 0;
    int j;

    ix_base = NULL;                 /* closing_ticks scans while it is built */
    if (n > 0xFFFE) {
        return;                     /* too long to index: scanned as before */
    }
    nticks = nbr = npar = 0;
    for (j = 0; j < n;) {               /* the backtick runs, found by memchr: the eZ80's cpir */
        const char* t = memchr(s + j, '`', (size_t)(n - j));
        int r;

        if (!t) {
            break;
        }
        j = (int)(t - s);
        r = run_of(s, j, n, '`');
        if (nticks == ticks_cap) {
            ticks_cap = ticks_cap ? ticks_cap * 2 : 32;
            ticks = md_realloc(ticks, (unsigned)ticks_cap * (unsigned)sizeof(tick_run));
        }
        ticks[nticks].len = (uint16_t)r;
        ticks[nticks++].pos = (uint16_t)j;
        j += r;
    }
    if (nticks > 16) {
        md_sort(ticks, (unsigned)nticks, (unsigned)sizeof(tick_run), by_tick);
    } else {
        int i;

        for (i = 1; i < nticks; i++) {  /* few: in place, by length, positions kept in order */
            tick_run x = ticks[i];
            int k = i;

            while (k > 0 && ticks[k - 1].len > x.len) {
                ticks[k] = ticks[k - 1];
                k--;
            }
            ticks[k] = x;
        }
    }
    ix_base = s;
    ix_n = n;

    /* [ and ] paired as link_at's scan pairs them -- past a backslash's
     * byte, and past a code span from the backticks it meets -- and ( and
     * ) as its second scan does, every one counting: in one pass, the
     * bytes that are none of these passed over by table. */
    if (!ix_kind['[']) {
        ix_kind['\\'] = 1;
        ix_kind['`'] = 2;
        ix_kind['['] = 3;
        ix_kind[']'] = 4;
        ix_kind['('] = 5;
        ix_kind[')'] = 6;
    }
    for (j = 0; j < n;) {
        int r;
        int c;

        switch (ix_kind[(uint8_t)s[j]]) {
        case 0:
            j++;
            break;
        case 1:                         /* the next byte is no bracket, but may be a ( */
            if (j + 1 < n) {
                paren(s[j + 1], j + 1, &pdepth);
            }
            j += 2;
            break;
        case 2:
            r = run_of(s, j, n, '`');
            c = closing_ticks(s, j + r, n, r);
            if (c >= 0) {
                int k;

                for (k = j + r; k < c; k++) {   /* a code span: its ( and ) still count */
                    paren(s[k], k, &pdepth);
                }
                j = c + r;
            } else {
                j++;
            }
            break;
        case 3:
            if (nbr == br_cap) {
                br_cap = br_cap ? br_cap * 2 : 32;
                br_open = md_realloc(br_open, (unsigned)br_cap * (unsigned)sizeof(uint16_t));
                br_close = md_realloc(br_close, (unsigned)br_cap * (unsigned)sizeof(uint16_t));
                stack = md_realloc(stack, (unsigned)br_cap * (unsigned)sizeof(uint16_t));
            }
            br_open[nbr] = (uint16_t)j;
            br_close[nbr] = NO_PAIR;
            stack[depth++] = (uint16_t)nbr++;
            j++;
            break;
        case 4:
            if (depth > 0) {
                br_close[stack[--depth]] = (uint16_t)j;
            }
            j++;
            break;
        default:
            paren(s[j], j, &pdepth);
            j++;
            break;
        }
    }
}

/* Where s is in the indexed text, or -1 if it is not, when the scans
 * should go there. They scan the text as it is -- a link's ] is a few
 * bytes on, and indexing a block costs more than finding it -- until they
 * have looked at twice the text, which only opener after opener that
 * never closes makes them do; then the text is indexed, and they look
 * there. So a block costs at most twice itself and its index. */
static long indexed_at(const char* s, int n)
{
    if (ix_text && scan_left < 0) {
        const char* t = ix_text;

        ix_text = NULL;
        index_text(t, ix_text_n);
    }

    return ix_base && s >= ix_base && s + n <= ix_base + ix_n ? (long)(s - ix_base) : -1;
}

/* The next run of exactly r backticks from k, or -1. */
static int closing_ticks(const char* s, int k, int n, int r)
{
    long at = indexed_at(s, n);

    if (at >= 0) {
        unsigned from = (unsigned)(at + k);
        int lo = 0;
        int hi = nticks;

        while (lo < hi) {               /* the first run of r at or after from */
            int mid = lo + (int)((unsigned)(hi - lo) >> 1);

            if (ticks[mid].len < r || (ticks[mid].len == r && ticks[mid].pos < from)) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        if (lo < nticks && ticks[lo].len == r && ticks[lo].pos < at + n) {
            return (int)(ticks[lo].pos - at);
        }

        return -1;
    }
    {
        int from = k;

        while (k < n) {
            int run = run_of(s, k, n, '`');

            if (run == r) {
                scan_left -= k - from;
                return k;
            }
            k += run ? run : 1;
        }
        scan_left -= k - from;
    }

    return -1;
}

/* [label](href) at s[k]: label's end and the href, or 0. */
static int link_at(const char* s, int k, int n, int* label_end, int* href, int* href_len, int* end)
{
    int depth = 0;
    int j;
    long at = indexed_at(s, n);

    if (at >= 0) {
        int b = pair_of(br_open, nbr, (unsigned)(at + k));
        int c;

        if (b < 0 || br_close[b] == NO_PAIR || br_close[b] >= at + n) {
            return 0;
        }
        j = (int)(br_close[b] - at);
        if (j + 1 >= n || s[j + 1] != '(') {
            return 0;
        }
        c = pair_of(par_open, npar, (unsigned)(at + j + 1));
        if (c < 0 || par_close[c] == NO_PAIR || par_close[c] >= at + n) {
            return 0;
        }
        *label_end = j;
        k = j + 2;
        j = (int)(par_close[c] - at);
        goto href;
    }
    for (j = k; j < n; j++) {
        if (s[j] == '\\') {
            j++;
            continue;
        }
        if (s[j] == '`') {
            int r = run_of(s, j, n, '`');
            int c = closing_ticks(s, j + r, n, r);

            if (c >= 0) {
                j = c + r - 1;
                continue;
            }
        }
        if (s[j] == '[') {
            depth++;
        } else if (s[j] == ']' && --depth == 0) {
            break;
        }
    }
    scan_left -= (j < n ? j : n) - k;
    if (j >= n || j + 1 >= n || s[j + 1] != '(') {
        return 0;
    }
    *label_end = j;
    k = j + 2;
    depth = 1;
    for (j = k; j < n; j++) {
        if (s[j] == '(') {
            depth++;
        } else if (s[j] == ')' && --depth == 0) {
            break;
        }
    }
    scan_left -= (j < n ? j : n) - k;
    if (j >= n) {
        return 0;
    }
href:
    *end = j + 1;
    while (k < j && s[k] == ' ') {
        k++;
    }
    if (k < j && s[k] == '<') {
        k++;
    }
    *href = k;
    while (k < j && s[k] != ' ' && s[k] != '>') {
        k++;
    }
    *href_len = k - *href;

    return 1;
}

/* Emphasis at s[k] (a run of * or _): where its text ends and the run's
 * length, or 0. A run of one to three opens, before a non-space; it closes
 * at a run as long after a non-space. An underscore inside a word does
 * neither. */
/* How deep inlines is in itself, and for each depth where an emphasis
 * of each kind (* or _, and its length) was looked for to the end without
 * a close: from there on there is none, in that text. */
#define INLINE_DEPTH 16

static int inline_depth;
static int emph_fail[INLINE_DEPTH + 1][2][4];

static int emphasis_at(const char* s, int k, int n, int* inner_end, int* width)
{
    char c = s[k];
    int r = run_of(s, k, n, c);
    int j;

    if (r > 3 || k + r >= n || s[k + r] == ' ' || s[k + r] == '\n') {
        return 0;
    }
    if (k >= emph_fail[inline_depth][c == '_'][r]) {
        return 0;                       /* no close after an earlier one: none after this */
    }
    if (c == '_' && k > 0 && ((s[k - 1] >= 'a' && s[k - 1] <= 'z') || (s[k - 1] >= 'A' && s[k - 1] <= 'Z')
                             || (s[k - 1] >= '0' && s[k - 1] <= '9'))) {
        return 0;
    }
    for (j = k + r; j < n; j++) {
        if (s[j] == '\\') {
            j++;
            continue;
        }
        if (s[j] == '`') {
            int t = run_of(s, j, n, '`');
            int e = closing_ticks(s, j + t, n, t);

            j = e >= 0 ? e + t - 1 : j + t - 1;
            continue;
        }
        if (s[j] == c) {
            int run = run_of(s, j, n, c);
            char after = j + run < n ? s[j + run] : ' ';
            int word_after = (after >= 'a' && after <= 'z') || (after >= 'A' && after <= 'Z')
                             || (after >= '0' && after <= '9');

            if (run >= r && s[j - 1] != ' ' && !(c == '_' && word_after)) {
                if (j == k + r) {
                    return 0;               /* nothing inside */
                }
                /* A longer run closes this one with its last r. */
                *inner_end = j + run - r;
                *width = r;

                return 1;
            }
            j += run - 1;
        }
    }
    emph_fail[inline_depth][c == '_'][r] = k;

    return 0;
}

static int is_external(const char* s, int len)
{
    int k = 0;

    while (k < len && ((s[k] >= 'a' && s[k] <= 'z') || (s[k] >= 'A' && s[k] <= 'Z'))) {
        k++;
    }

    return (k > 0 && k < len && s[k] == ':') || (len >= 2 && s[0] == '/' && s[1] == '/');
}

/* --- inline Markdown ---------------------------------------------------------------- */

static const page* cur_page;
static int cur_no;
static link_fn resolve;
static int plain_only;          /* for a title: an external link's text, not its URL */

static void inlines(const char* s, int n, int in_link);

static void link(const char* s, int k, int label_end, int href, int href_len, int in_link)
{
    if (is_external(s + href, href_len)) {
        int same = label_end - k - 1 == href_len && memcmp(s + k + 1, s + href, (size_t)href_len) == 0;

        inlines(s + k + 1, label_end - k - 1, in_link);
        if (!same && !plain_only && !(href_len >= 7 && memcmp(s + href, "mailto:", 7) == 0)) {
            put_text(" <", 2);
            put_text(s + href, href_len);
            put_text(">", 1);
        }

        return;
    }
    {
        int t = in_link ? -1 : resolve(cur_page, cur_no, s + href, href_len);

        if (t == PENDING) {
            /* The topic is not read yet: a placeholder, patched later. */
            put(LINK);
            markup_pending(written);
            put(NONE & 0xFF);
            put(NONE >> 8);
            inlines(s + k + 1, label_end - k - 1, 1);
            put(LINK_END);

            return;
        }
        if (t < 0) {
            inlines(s + k + 1, label_end - k - 1, in_link);

            return;
        }
        put(LINK);
        put(t & 0xFF);
        put(t >> 8);
        inlines(s + k + 1, label_end - k - 1, 1);
        put(LINK_END);
    }
}

static void inlines_at(const char* s, int n, int in_link);

/* Inline Markdown at a depth of its own -- an emphasis's or a link's text
 * is inlines again -- to INLINE_DEPTH, past which it is put as it is. */
static void inlines(const char* s, int n, int in_link)
{
    int c;

    if (inline_depth >= INLINE_DEPTH) {
        put_text(s, n);

        return;
    }
    inline_depth++;
    for (c = 0; c < 2; c++) {
        int r;

        for (r = 0; r < 4; r++) {
            emph_fail[inline_depth][c][r] = n;
        }
    }
    inlines_at(s, n, in_link);
    inline_depth--;
}

static void inlines_at(const char* s, int n, int in_link)
{
    int k = 0;

    while (k < n) {
        char c = s[k];
        int a;
        int b;
        int h;
        int hl;
        int e;

        if (c == '\\' && k + 1 < n && strchr(ESCAPABLE, s[k + 1])) {
            put_char((unsigned char)s[k + 1]);
            k += 2;
        } else if (c == '`') {
            int r = run_of(s, k, n, '`');
            int close = closing_ticks(s, k + r, n, r);

            if (close < 0) {
                put_text(s + k, r);
                k += r;
                continue;
            }
            a = k + r;
            b = close;
            if (b - a >= 2 && s[a] == ' ' && s[b - 1] == ' ') {
                a++;
                b--;
            }
            put(CODE_ON);
            put_text(s + a, b - a);
            put(CODE_OFF);
            k = close + r;
        } else if (c == '<') {
            const char* gt = memchr(s + k, '>', (size_t)(n - k));
            int len = gt ? (int)(gt - (s + k)) + 1 : 0;

            if (len && (strncmp(s + k, "<br>", 4) == 0 || strncmp(s + k, "<br/>", 5) == 0
                        || strncmp(s + k, "<br />", 6) == 0)) {
                put(BREAK);
                last_space = 1;
                k += len;
            } else if (len && is_external(s + k + 1, len - 2)) {
                put_text(s + k + 1, len - 2);       /* <https://...> */
                k += len;
            } else if (len && (strncmp(s + k, "<b>", 3) == 0 || strncmp(s + k, "</b>", 4) == 0
                               || strncmp(s + k, "<i>", 3) == 0 || strncmp(s + k, "</i>", 4) == 0
                               || strncmp(s + k, "<em>", 4) == 0 || strncmp(s + k, "</em>", 5) == 0
                               || strncmp(s + k, "<kbd>", 5) == 0
                               || strncmp(s + k, "</kbd>", 6) == 0)) {
                k += len;                   /* the tag goes; its text stays */
            } else {
                put_char('<');
                k++;
            }
        } else if (c == '!' && k + 1 < n && s[k + 1] == '['
                   && link_at(s, k + 1, n, &a, &h, &hl, &e)) {
            put_text("[Image: ", 8);
            put_text(s + k + 2, a - k - 2);
            put_text("]", 1);
            k = e;
        } else if (c == '[' && k + 1 < n && s[k + 1] == '^' && memchr(s + k, ']', (size_t)(n - k))) {
            const char* close = memchr(s + k, ']', (size_t)(n - k));

            put_char('[');
            put_text(s + k + 2, (int)(close - (s + k + 2)));
            put_char(']');
            k = (int)(close - s) + 1;
        } else if (c == '[' && link_at(s, k, n, &a, &h, &hl, &e)) {
            link(s, k, a, h, hl, in_link);
            k = e;
        } else if ((c == '*' || c == '_') && emphasis_at(s, k, n, &e, &a)) {
            put(EM_ON);
            inlines(s + k + a, e - k - a, in_link);
            put(EM_OFF);
            k = e + a;
        } else if (c == '*' || c == '_') {
            int r = run_of(s, k, n, c);

            put_text(s + k, r);
            k += r;
        } else {
            const char* p = s + k;

            put_char(agon_char(&p));
            k = plain_run(s, (int)(p - s), n, special);
        }
    }
}

int inline_markup(const page* p, int no, const char* s, uint8_t* o, int size, link_fn links)
{
    if (!special[0]) {
        make_special();
    }
    out = o;
    room = size;
    written = 0;
    last_space = 1;                 /* no space to start with */
    cur_page = p;
    cur_no = no;
    resolve = links;
    ix_base = NULL;
    ix_text = s;
    ix_text_n = (int)strlen(s);
    scan_left = 2L * ix_text_n + 256;
    inlines(s, ix_text_n, 0);
    ix_base = ix_text = NULL;
    /* No space to end with. */
    if (written > 0 && written <= room && out[written - 1] == ' ') {
        written--;
    }

    return written;
}

/* --- plain text, and code spans ------------------------------------------------------ */

static int no_links(const page* p, int no, const char* href, int len)
{
    (void)p;
    (void)no;
    (void)href;
    (void)len;

    return -1;
}

void inline_plain(const char* s, char* o, int size)
{
    static uint8_t buf[1024];
    int n;

    plain_only = 1;
    n = inline_markup(NULL, 0, s, buf, (int)sizeof buf, no_links);
    plain_only = 0;
    int j = 0;
    int k;

    for (k = 0; k < n && k < (int)sizeof buf && j + 1 < size; k++) {
        if (buf[k] == LINK) {
            k += 2;
        } else if (buf[k] >= 0x20) {
            o[j++] = (char)buf[k];
        } else if (buf[k] == BREAK) {
            o[j++] = ' ';
        }
    }
    o[j] = 0;
}

int inline_code(const char* s, int* at, char* o, int size)
{
    int n = (int)strlen(s);
    int k = *at;

    if (*at == 0 || (ix_base != s && ix_text != s)) {
        ix_base = NULL;                 /* afresh for each string, when first needed */
        ix_text = s;
        ix_text_n = n;
        scan_left = 2L * n + 256;
    }
    while (k < n) {
        if (s[k] == '\\') {
            k += 2;
            continue;
        }
        if (s[k] == '`') {
            int r = run_of(s, k, n, '`');
            int close = closing_ticks(s, k + r, n, r);

            if (close >= 0) {
                int a = k + r;
                int len = close - a;

                if (len >= size) {
                    len = size - 1;
                }
                memcpy(o, s + a, (size_t)len);
                o[len] = 0;
                *at = close + r;

                return 1;
            }
            k += r;
            continue;
        }
        k++;
    }
    *at = k;

    return 0;
}
