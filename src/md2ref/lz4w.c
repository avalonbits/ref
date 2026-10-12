/*
 * LZ4 compression, as LZ4's block format, greedy: at each position the
 * longest match a hash of four bytes leads to, within 64 KB back. Small
 * and quick rather than tight; there is no dictionary.
 *
 * A link's placeholder -- two bytes patched once its topic is known -- is
 * left as literals: no match covers it, and none copies from it, so the
 * bytes in the file are the bytes in the markup.
 */
#include <string.h>

#include "md2ref.h"
#include "refcore.h"

#define HASH_BITS 12
#define MIN_MATCH 4

/* Positions + 1, each above lz4_head_base, which is where the calls before
 * this one left off: an entry at or below it is from them, and is none.
 * So the table is cleared only when the positions would pass 16 bits,
 * not for every topic -- clearing 8 KB was a tenth of compressing. */
uint16_t* lz4_head;          /* made when first compressing: only -z does */
unsigned lz4_head_base;

/* The hash of four bytes, a byte of it at a time from tables of random
 * bytes: each half (8 bits, and 5) is rotated a bit and the next byte's
 * entry exclusive-ored in. So the next position's is this one's rotated,
 * with the byte leaving taken out -- its entry rotated four, from a table
 * of those -- and the byte coming in put in: what the Agon's scan does,
 * rolling, in two lookups where hashing afresh is eight. Pages: T and U
 * the halves' tables, R and V theirs rotated four. Aligned, so that the
 * Agon's scan finds an entry by setting a register's low byte. */
uint8_t lz4_tab[4][256] __attribute__((aligned(1024)));   /* in one 64 KB page */
#define T_PAGE 0
#define R_PAGE 1
#define U_PAGE 2
#define V_PAGE 3
static int mixed;

static uint8_t rol(uint8_t x)
{
    return (uint8_t)(x << 1 | x >> 7);
}

static void make_mix(void)
{
    uint32_t v = 1;
    unsigned b;

    for (b = 0; b < 256; b++) {
        uint8_t t;
        uint8_t u;

        v = v * 1103515245UL + 12345;
        t = (uint8_t)(v >> 16);
        v = v * 1103515245UL + 12345;
        u = (uint8_t)(v >> 16);
        lz4_tab[T_PAGE][b] = t;
        lz4_tab[U_PAGE][b] = u;
        lz4_tab[R_PAGE][b] = rol(rol(rol(rol(t))));
        lz4_tab[V_PAGE][b] = rol(rol(rol(rol(u))));
    }
}

static unsigned length_bytes(uint8_t* o, unsigned n)
{
    unsigned k = 0;

    while (n >= 255) {
        o[k++] = 255;
        n -= 255;
    }
    o[k++] = (uint8_t)n;

    return k;
}

/* One sequence: literals, then a match (none when len is 0). Where the
 * literals start goes in lit_at. */
static unsigned sequence(uint8_t* o, const uint8_t* lit, unsigned nlit, unsigned off,
                         unsigned mlen, unsigned* lit_at)
{
    unsigned k = 0;
    unsigned ml = mlen ? mlen - MIN_MATCH : 0;

    o[k++] = (uint8_t)((nlit < 15 ? nlit : 15) << 4 | (ml < 15 ? ml : 15));
    if (nlit >= 15) {
        k += length_bytes(o + k, nlit - 15);
    }
    *lit_at = k;
    memcpy(o + k, lit, nlit);
    k += nlit;
    if (mlen) {
        o[k++] = (uint8_t)(off & 0xFF);
        o[k++] = (uint8_t)(off >> 8);
        if (ml >= 15) {
            k += length_bytes(o + k, ml - 15);
        }
    }

    return k;
}

unsigned lz4_literals(uint8_t* o, unsigned n)
{
    unsigned k = 1;

    o[0] = (uint8_t)((n < 15 ? n : 15) << 4);
    if (n >= 15) {
        k += length_bytes(o + 1, n - 15);
    }

    return k;
}

/* The first placeholder that ends at or after p: its index, or n. */
static int fixed_from(const unsigned* fixed, int n, unsigned p)
{
    int lo = 0;
    int hi = n;

    while (lo < hi) {
        int mid = (int)((unsigned)(lo + hi) >> 1);

        if (fixed[mid] + 1 < p) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }

    return lo;
}

#if defined(AGONDEV)
/* lz4scan.s: scan and extend, by hand. */
unsigned lz4_scan(const uint8_t* src, unsigned n, unsigned* pi, unsigned* pcand);
unsigned lz4_extend(const uint8_t* a, const uint8_t* b, unsigned max);
#define SCAN lz4_scan
#define EXTEND lz4_extend
#else
#define SCAN scan
#define EXTEND extend

/* How many bytes from a and b are the same, to max. */
static unsigned extend(const uint8_t* a, const uint8_t* b, unsigned max)
{
    unsigned k = 0;

    while (k < max && a[k] == b[k]) {
        k++;
    }

    return k;
}

/* The entry of head for the four bytes at p: the low half's top seven
 * bits and the high half's low five. */
static unsigned hash(const uint8_t* p)
{
    uint8_t lo = lz4_tab[T_PAGE][p[0]];
    uint8_t hi = lz4_tab[U_PAGE][p[0]];
    int k;

    for (k = 1; k < 4; k++) {
        lo = rol(lo) ^ lz4_tab[T_PAGE][p[k]];
        hi = rol(hi) ^ lz4_tab[U_PAGE][p[k]];
    }

    return (unsigned)(hi & 0x1F) << 7 | lo >> 1;
}

/* From *pi on, each position's hash into head, until one whose latest
 * earlier position of the same hash starts with the same MIN_MATCH bytes:
 * MIN_MATCH, with the position in *pi and the earlier one + 1 in *pcand.
 * 0 when there is none before the last four bytes. This is where
 * compressing spends its time: almost every byte goes through here, and
 * only a match leaves it. */
static unsigned scan(const uint8_t* src, unsigned n, unsigned* pi, unsigned* pcand)
{
    unsigned i = *pi;

    while (i + MIN_MATCH <= n) {
        unsigned h = hash(src + i);
        unsigned e = lz4_head[h];

        lz4_head[h] = (uint16_t)(lz4_head_base + i + 1);
        if (e > lz4_head_base) {
            const uint8_t* a = src + (e - lz4_head_base - 1);
            const uint8_t* b = src + i;

            if (a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3]) {
                *pi = i;
                *pcand = e - lz4_head_base;

                return MIN_MATCH;
            }
        }
        i++;
    }
    *pi = i;

    return 0;
}
#endif

/* n is at most 65535 -- a topic's most -- so with lz4_head_base cleared to 0
 * when it must be, every position + 1 fits in head's 16 bits. */
unsigned lz4_compress(const uint8_t* src, unsigned n, uint8_t* dst, unsigned room,
                      const unsigned* fixed, int nfixed, unsigned* fixed_at)
{
    unsigned i = 0;
    unsigned lit = 0;
    unsigned o = 0;
    unsigned at;
    int f = 0;                  /* the first placeholder not yet placed */
    const unsigned* fg = fixed;     /* the first not yet behind i, walked: no index to multiply */
    const unsigned* fend = fixed + nfixed;

    (void)room;                 /* the caller gives n + n / 255 + 16 */
    if (!mixed) {
        make_mix();
        mixed = 1;
        lz4_head = md_realloc(NULL, (1u << HASH_BITS) * (unsigned)sizeof(uint16_t));
        memset(lz4_head, 0, (1u << HASH_BITS) * sizeof(uint16_t));
    }
    if (lz4_head_base + n + 1 > 0xFFFF) {
        memset(lz4_head, 0, (1u << HASH_BITS) * sizeof(uint16_t));
        lz4_head_base = 0;
    }
    /* The text between placeholders, a stretch at a time: a match starts
     * and ends inside one, so none takes a placeholder in on this side --
     * and the other side's, the earlier text's, is found before the match
     * is extended, so a match is never extended past where it must stop,
     * and then again from the next byte. */
    while (i + MIN_MATCH <= n) {
        unsigned cand;
        unsigned stop;
        unsigned limit;
        unsigned mlen;
        int k;

        while (fg < fend && *fg + 2 <= i) {
            fg++;
        }
        if (fg < fend && *fg <= i) {
            i = *fg + 2;                    /* a placeholder's bytes start nothing */
            continue;
        }
        stop = fg < fend ? *fg : n;
        if (i + MIN_MATCH > stop || !SCAN(src, stop, &i, &cand)) {
            i = stop;
            continue;
        }
        limit = stop - i;
        /* A placeholder in the earlier text's way is one between it and i
         * -- those from g on are past where this match can reach. */
        if (fg > fixed && fg[-1] + 1 >= cand) {
            k = fixed_from(fixed, (int)(fg - fixed), cand);
            if (fixed[k] - (cand - 1) < limit) {
                limit = fixed[k] - (cand - 1);
            }
        }
        if (limit < MIN_MATCH) {
            i++;
            continue;
        }
        mlen = MIN_MATCH + EXTEND(src + cand - 1 + MIN_MATCH, src + i + MIN_MATCH, limit - MIN_MATCH);
        {
            unsigned start = o;

            /* Placeholders are literals: among these, before i. */
            o += sequence(dst + o, src + lit, i - lit, i - (cand - 1), mlen, &at);
            for (; f < nfixed && fixed[f] < i; f++) {
                fixed_at[f] = start + at + fixed[f] - lit;
            }
            i += mlen;
            lit = i;
        }
    }
    {
        unsigned start = o;

        o += sequence(dst + o, src + lit, n - lit, 0, 0, &at);
        for (; f < nfixed; f++) {
            fixed_at[f] = start + at + fixed[f] - lit;
        }
    }
    lz4_head_base += n + 1;

    return o;
}
