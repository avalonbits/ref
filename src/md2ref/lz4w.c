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

#define HASH_BITS 12
#define MIN_MATCH 4

static uint16_t head[1 << HASH_BITS];      /* positions + 1; 0 for none */

/* The hash of four bytes, from a table of random bytes for each byte and
 * each half of the result: 8-bit lookups and exclusive ors, which the eZ80
 * does in line, where the multiply and the 32-bit shifts of the usual
 * hash are calls -- and this runs at every byte of every topic. */
static uint8_t mix_lo[4][256];
static uint8_t mix_hi[4][256];
static int mixed;

static void make_mix(void)
{
    uint32_t v = 1;
    unsigned k;
    unsigned b;

    for (k = 0; k < 4; k++) {
        for (b = 0; b < 256; b++) {
            v = v * 1103515245UL + 12345;
            mix_lo[k][b] = (uint8_t)(v >> 16);
            v = v * 1103515245UL + 12345;
            mix_hi[k][b] = (uint8_t)(v >> 16 & ((1 << (HASH_BITS - 8)) - 1));
        }
    }
}

static unsigned hash(const uint8_t* p)
{
    uint8_t lo = mix_lo[0][p[0]] ^ mix_lo[1][p[1]] ^ mix_lo[2][p[2]] ^ mix_lo[3][p[3]];
    uint8_t hi = mix_hi[0][p[0]] ^ mix_hi[1][p[1]] ^ mix_hi[2][p[2]] ^ mix_hi[3][p[3]];

    return (unsigned)hi << 8 | lo;
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

/* The first placeholder that ends at or after p: its index, or n. */
static int fixed_from(const unsigned* fixed, int n, unsigned p)
{
    int lo = 0;
    int hi = n;

    while (lo < hi) {
        int mid = (lo + hi) / 2;

        if (fixed[mid] + 1 < p) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }

    return lo;
}

/* How much of a match of mlen from p it may take: up to the first
 * placeholder byte in it. */
static unsigned clear_of(const unsigned* fixed, int n, unsigned p, unsigned mlen)
{
    int f = fixed_from(fixed, n, p);

    if (f < n && fixed[f] < p + mlen) {
        return fixed[f] > p ? fixed[f] - p : 0;
    }

    return mlen;
}

unsigned lz4_compress(const uint8_t* src, unsigned n, uint8_t* dst, unsigned room,
                      const unsigned* fixed, int nfixed, unsigned* fixed_at)
{
    unsigned i = 0;
    unsigned lit = 0;
    unsigned o = 0;
    unsigned at;
    int f = 0;                  /* the first placeholder not yet placed */

    (void)room;                 /* the caller gives n + n / 255 + 16 */
    if (!mixed) {
        make_mix();
        mixed = 1;
    }
    memset(head, 0, sizeof head);
    while (i + MIN_MATCH <= n) {
        unsigned h = hash(src + i);
        unsigned cand = head[h];
        unsigned mlen = 0;

        head[h] = (uint16_t)(i + 1 > 0xFFFF ? 0 : i + 1);
        if (cand && i - (cand - 1) <= 0xFFFF && i + 1 <= 0xFFFF) {
            const uint8_t* a = src + cand - 1;
            const uint8_t* b = src + i;

            while (i + mlen < n && a[mlen] == b[mlen]) {
                mlen++;
            }
            if (nfixed) {
                mlen = clear_of(fixed, nfixed, i, mlen);
                mlen = clear_of(fixed, nfixed, cand - 1, mlen);
            }
        }
        if (mlen >= MIN_MATCH) {
            unsigned start = o;

            /* Placeholders are literals: among these, before i. */
            o += sequence(dst + o, src + lit, i - lit, i - (cand - 1), mlen, &at);
            for (; f < nfixed && fixed[f] < i; f++) {
                fixed_at[f] = start + at + fixed[f] - lit;
            }
            i += mlen;
            lit = i;
        } else {
            i++;
        }
    }
    {
        unsigned start = o;

        o += sequence(dst + o, src + lit, n - lit, 0, 0, &at);
        for (; f < nfixed; f++) {
            fixed_at[f] = start + at + fixed[f] - lit;
        }
    }

    return o;
}
