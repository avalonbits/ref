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
    int w = written;
    int sp = last_space;

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
        if (w < room) {
            out[w] = c;
        }
        w++;
    }
    written = w;
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

/* The next run of exactly r backticks from k, or -1. */
static int closing_ticks(const char* s, int k, int n, int r)
{
    while (k < n) {
        int run = run_of(s, k, n, '`');

        if (run == r) {
            return k;
        }
        k += run ? run : 1;
    }

    return -1;
}

/* [label](href) at s[k]: label's end and the href, or 0. */
static int link_at(const char* s, int k, int n, int* label_end, int* href, int* href_len, int* end)
{
    int depth = 0;
    int j;

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
    if (j >= n) {
        return 0;
    }
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
static int emphasis_at(const char* s, int k, int n, int* inner_end, int* width)
{
    char c = s[k];
    int r = run_of(s, k, n, c);
    int j;

    if (r > 3 || k + r >= n || s[k + r] == ' ' || s[k + r] == '\n') {
        return 0;
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

static void inlines(const char* s, int n, int in_link)
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
    inlines(s, (int)strlen(s), 0);
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

int inline_code(const char* s, int i, char* o, int size)
{
    int n = (int)strlen(s);
    int k = 0;

    while (k < n) {
        if (s[k] == '\\') {
            k += 2;
            continue;
        }
        if (s[k] == '`') {
            int r = run_of(s, k, n, '`');
            int close = closing_ticks(s, k + r, n, r);

            if (close >= 0) {
                if (i-- == 0) {
                    int a = k + r;
                    int len = close - a;

                    if (len >= size) {
                        len = size - 1;
                    }
                    memcpy(o, s + a, (size_t)len);
                    o[len] = 0;

                    return 1;
                }
                k = close + r;
                continue;
            }
            k += r;
            continue;
        }
        k++;
    }

    return 0;
}
