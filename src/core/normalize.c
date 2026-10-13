/*
 * Normalising what the user types, exactly as build/refbuild/normalize.py
 * normalises keys. Its docstring is the specification; tests/normalize.json
 * the cases both must pass. At most TOKENS tokens are read.
 */
#include <string.h>

#include "refcore.h"
#include "refint.h"

#define TOKENS 32
#define POOL   (2 * (REF_KEY_MAX + 1))

/* Eight bytes, so that tok[k] is a shift: at five, every one was a call
 * to multiply on the eZ80, which md2ref makes for every key it normalises. */
typedef struct {
    uint16_t at;            /* into the pool */
    uint8_t len;
    uint8_t wide;
    uint8_t num;
    uint8_t pad[3];
} token;

/* Statics, not locals: agondev frames stay small. One normalisation at a
 * time. */
static token tok[TOKENS + 1];
static char pool[POOL];
static char out_pool[POOL];
static unsigned ntok;

static const char STRIP[] = "()[]{}<>\"'`:*.!?|";

static int is_digit(int c)
{
    return c >= '0' && c <= '9';
}

static int digit_value(int c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 10;
    }

    return 99;
}

/* s[0..len) as digits in base, into *v; 0 if they are not, or too big. */
static int digits(const char* s, unsigned len, unsigned base, uint32_t* v)
{
    unsigned k;

    if (len == 0) {
        return 0;
    }
    *v = 0;
    for (k = 0; k < len; k++) {
        unsigned d = (unsigned)digit_value((unsigned char)s[k]);

        if (d >= base) {
            return 0;
        }
        *v = *v * base + d;
        if (*v > 0xFFFFFFu) {
            return 0;
        }
    }

    return 1;
}

/* The value of a number token, as normalize.py's number(): 1 if it is one. */
static int number(const char* s, unsigned len, int hex_context, uint32_t* v)
{
    unsigned k;

    if (digits(s, len, hex_context ? 16 : 10, v)) {
        return 1;                           /* all digits; after rst, hex digits */
    }
    if (len >= 2 && s[0] == '&') {
        return digits(s + 1, len - 1, 16, v);
    }
    if (len >= 3 && s[0] == '0' && s[1] == 'x') {
        return digits(s + 2, len - 2, 16, v);
    }
    if (len >= 2 && is_digit((unsigned char)s[0]) && s[len - 1] == 'h') {
        return digits(s, len - 1, 16, v);
    }
    for (k = 0; k < len && is_digit((unsigned char)s[k]); k++) {
    }
    if (k > 0 && k < len - 1 && s[k] == '_') {
        uint32_t base;

        if (digits(s, k, 10, &base) && base >= 2 && base <= 36) {
            unsigned j;

            /* The digits after the base must be letters or digits... */
            for (j = k + 1; j < len; j++) {
                if (digit_value((unsigned char)s[j]) > 35) {
                    return 0;
                }
            }

            /* ...and all of them digits in it. */
            return digits(s + k + 1, len - k - 1, (unsigned)base, v);
        }
    }

    return 0;
}

static void end_token(unsigned start, unsigned* used, int wide)
{
    if (*used > start) {
        if (ntok < TOKENS) {
            tok[ntok].at = (uint16_t)start;
            tok[ntok].len = (uint8_t)(*used - start);
            tok[ntok].wide = (uint8_t)wide;
            ntok++;
        } else {
            *used = start;                  /* past TOKENS: dropped */
        }
    } else if (wide && ntok > 0) {
        tok[ntok - 1].wide = 1;             /* `n ;` */
    }
}

/* Steps 1-3: tokens, cleaned. */
static void tokenise(const char* text)
{
    unsigned used = 0;
    unsigned start = 0;
    unsigned k;
    unsigned j;
    const char* c;

    ntok = 0;
    for (c = text; *c; c++) {
        int ch = (unsigned char)*c;

        if (ch >= 'A' && ch <= 'Z') {
            ch += 32;
        }
        if (ch == ' ' || ch == '\t' || ch == ',') {
            end_token(start, &used, 0);
            start = used;
        } else if (ch == ';') {
            end_token(start, &used, 1);
            start = used;
        } else if (used < REF_KEY_MAX) {
            pool[used++] = (char)ch;
        }
    }
    end_token(start, &used, 0);

    /* Inner placeholders out, the strip set off both ends, empties gone. */
    j = 0;
    for (k = 0; k < ntok; k++) {
        char* s = pool + tok[k].at;
        unsigned len = tok[k].len;
        unsigned a;

        for (a = 1; a < len; a++) {
            if (s[a] == '<') {
                unsigned e;

                for (e = a + 1; e < len && s[e] != '>'; e++) {
                }
                if (e >= len) {
                    break;
                }
                memmove(s + a, s + e + 1, len - e - 1);
                len -= e + 1 - a;
                a--;
            }
        }
        a = 0;
        while (a < len && strchr(STRIP, s[a])) {
            a++;
        }
        while (len > a && strchr(STRIP, s[len - 1])) {
            len--;
        }
        if (len > a) {
            tok[j].at = (uint16_t)(tok[k].at + a);
            tok[j].len = (uint8_t)(len - a);
            tok[j].wide = tok[k].wide;
            j++;
        }
    }
    ntok = j;
}

static int in_hex_after(const ref_file* r, const char* s, unsigned len)
{
    const uint8_t* w = r->norm;
    unsigned n = w[0];
    unsigned at = 1;

    for (; n > 0; n--) {
        if (w[at] == len && memcmp(w + at + 1, s, len) == 0) {
            return 1;
        }
        at = at + 1 + w[at];
    }

    return 0;
}

static int tok_is(unsigned k, const char* s, unsigned len)
{
    return tok[k].len == len && memcmp(out_pool + tok[k].at, s, len) == 0;
}

/* Does the normalised prefix p (plen bytes, tokens split by spaces) match
 * the tokens' start? How many tokens it has, or 0. */
static unsigned prefix_match(const uint8_t* p, unsigned plen)
{
    unsigned k = 0;
    unsigned a = 0;

    while (a < plen) {
        unsigned e = a;

        while (e < plen && p[e] != ' ') {
            e++;
        }
        if (k >= ntok || !tok_is(k, (const char*)p + a, e - a)) {
            return 0;
        }
        k++;
        a = e + 1;
    }

    return k;
}

int ref_normalize(const ref_file* r, const char* text, char* out)
{
    unsigned used = 0;
    unsigned k;
    unsigned n;
    unsigned best = 0;
    const uint8_t* best_units = NULL;
    unsigned nbest_units = 0;
    const uint8_t* p;
    unsigned at;
    int hex_next;

    tokenise(text);

    /* Step 4: numbers in decimal, into out_pool, which the rest works on.
     * Whether a token follows `rst` is decided from the token before as it
     * was written, so it is noted before that token is rewritten. */
    hex_next = 0;
    for (k = 0; k < ntok; k++) {
        const char* s = pool + tok[k].at;
        uint32_t v;
        int hex = hex_next;
        unsigned at = used;

        hex_next = in_hex_after(r, s, tok[k].len);

        if (number(s, tok[k].len, hex, &v)) {
            /* In decimal; after rst in hex, so a key normalises to itself. */
            unsigned base = hex ? 16 : 10;
            char digit[12];
            int d = 0;

            do {
                digit[d++] = "0123456789abcdef"[v % base];
                v /= base;
            } while (v);
            while (d) {
                out_pool[used++] = digit[--d];
            }
            tok[k].num = 1;
        } else {
            memcpy(out_pool + used, s, tok[k].len);
            used += tok[k].len;
            tok[k].num = 0;
        }
        tok[k].len = (uint8_t)(used - at);
        tok[k].at = (uint16_t)at;
    }

    /* Step 5: a leading number is a VDU sequence. */
    if (ntok > 0 && tok[0].num) {
        memmove(tok + 1, tok, ntok * sizeof tok[0]);
        memcpy(out_pool + used, "vdu", 3);
        tok[0].at = (uint16_t)used;
        tok[0].len = 3;
        tok[0].wide = 0;
        tok[0].num = 0;
        used += 3;
        ntok++;
    }

    /* Step 6: the longest skip whose prefix the tokens start with. */
    p = r->norm;
    at = r->norm_skips;
    n = p[at];
    at = at + 1;
    for (; n > 0; n--) {
        unsigned plen = p[at];
        unsigned m = prefix_match(p + at + 1, plen);

        at = at + 1 + plen;                 /* now at the count of units */
        if (m > best) {
            best = m;
            best_units = p + at + 1;
            nbest_units = p[at];
        }
        at = at + 1 + p[at];
    }
    if (best) {
        unsigned src = best;
        unsigned dst = best;
        unsigned u;

        for (u = 0; u < nbest_units && src < ntok; u++) {
            if (best_units[u] == 2 && tok[src].num && !tok[src].wide && src + 1 < ntok
                && tok[src + 1].num) {
                src += 2;
            } else {
                src += 1;
            }
            out_pool[used] = '_';
            tok[dst].at = (uint16_t)used;
            tok[dst].len = 1;
            tok[dst].num = 0;
            tok[dst].wide = 0;
            used++;
            dst++;
        }
        while (src < ntok) {
            tok[dst++] = tok[src++];
        }
        ntok = dst;
    }

    /* Step 7: no trailing placeholders. */
    while (ntok > 0 && tok_is(ntok - 1, "_", 1)) {
        ntok--;
    }

    /* Step 8: joined with single spaces. */
    n = 0;
    for (k = 0; k < ntok; k++) {
        if (k && n < REF_KEY_MAX) {
            out[n++] = ' ';
        }
        if (n + tok[k].len > REF_KEY_MAX) {
            break;
        }
        memcpy(out + n, out_pool + tok[k].at, tok[k].len);
        n += tok[k].len;
    }
    out[n] = 0;

    return (int)n;
}
