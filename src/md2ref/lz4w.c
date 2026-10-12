/*
 * LZ4 compression, as LZ4's block format, greedy: at each position the
 * longest match a hash of four bytes leads to, within 64 KB back. Small
 * and quick rather than tight; there is no dictionary.
 */
#include <string.h>

#include "md2ref.h"

#define HASH_BITS 12
#define MIN_MATCH 4

static uint16_t head[1 << HASH_BITS];      /* positions + 1; 0 for none */

/* Shifted while still 32 bits: unsigned is 24 bits on the eZ80, and the
 * top byte is the part kept. */
static unsigned hash(const uint8_t* p)
{
    uint32_t v = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16
                 | (uint32_t)p[3] << 24;

    return (unsigned)(((v * 2654435761UL) & 0xFFFFFFFFUL) >> (32 - HASH_BITS));
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

/* One sequence: literals, then a match (none when len is 0). */
static unsigned sequence(uint8_t* o, const uint8_t* lit, unsigned nlit, unsigned off,
                         unsigned mlen)
{
    unsigned k = 0;
    unsigned ml = mlen ? mlen - MIN_MATCH : 0;

    o[k++] = (uint8_t)((nlit < 15 ? nlit : 15) << 4 | (ml < 15 ? ml : 15));
    if (nlit >= 15) {
        k += length_bytes(o + k, nlit - 15);
    }
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

unsigned lz4_compress(const uint8_t* src, unsigned n, uint8_t* dst, unsigned room)
{
    unsigned i = 0;
    unsigned lit = 0;
    unsigned o = 0;

    (void)room;                 /* the caller gives n + n / 255 + 16 */
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
        }
        if (mlen >= MIN_MATCH) {
            o += sequence(dst + o, src + lit, i - lit, i - (cand - 1), mlen);
            i += mlen;
            lit = i;
        } else {
            i++;
        }
    }
    o += sequence(dst + o, src + lit, n - lit, 0, 0);

    return o;
}
