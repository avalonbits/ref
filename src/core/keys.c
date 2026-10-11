/*
 * The keyword index: front-coded blocks of keys, found through the
 * directory kept in memory, and the lookup build/refbuild/keys.py specifies.
 */
#include <string.h>

#include "refcore.h"
#include "refint.h"

/* --- reading blocks --------------------------------------------------------- */

void ref_reader_seek(ref_reader* rd, ref_file* r, uint32_t off)
{
    rd->r = r;
    rd->pos = off;
    rd->have = 0;
    rd->at = 0;
    rd->bad = 0;
}

unsigned ref_reader_byte(ref_reader* rd)
{
    if (rd->at >= rd->have) {
        size_t n;

        rd->pos += rd->have;
        rd->at = 0;
        rd->have = 0;
        if (rd->pos >= rd->r->size || fseek(rd->r->f, (long)rd->pos, SEEK_SET) != 0) {
            rd->bad = 1;

            return 0;
        }
        n = fread(rd->buf, 1, sizeof rd->buf, rd->r->f);
        if (n == 0) {
            rd->bad = 1;

            return 0;
        }
        rd->have = (unsigned)n;
    }

    return rd->buf[rd->at++];
}

uint32_t ref_reader_varint(ref_reader* rd)
{
    uint32_t v = 0;
    unsigned shift = 0;
    unsigned b;

    do {
        b = ref_reader_byte(rd);
        if (shift < 32) {
            v |= (uint32_t)(b & 0x7F) << shift;
        }
        shift += 7;
    } while ((b & 0x80) && !rd->bad);

    return v;
}

int ref_reader_entry(ref_reader* rd, char* key, int prev_len)
{
    unsigned shared = ref_reader_byte(rd);
    unsigned rest = ref_reader_byte(rd);
    unsigned k;

    if ((int)shared > prev_len || shared + rest > REF_KEY_MAX) {
        rd->bad = 1;

        return 0;
    }
    for (k = 0; k < rest; k++) {
        key[shared + k] = (char)ref_reader_byte(rd);
    }
    key[shared + rest] = 0;

    return (int)(shared + rest);
}

int ref_keycmp(const char* a, unsigned alen, const char* b, unsigned blen)
{
    unsigned n = alen < blen ? alen : blen;
    int c = memcmp(a, b, n);

    if (c) {
        return c;
    }

    return alen < blen ? -1 : alen > blen ? 1 : 0;
}

unsigned ref_find_block(const uint8_t* dir, const uint16_t* at, unsigned blocks, const char* q,
                        unsigned qlen)
{
    unsigned lo = 0;
    unsigned hi = blocks;

    /* The first block whose first key is not below q, less one. */
    while (lo < hi) {
        unsigned mid = (lo + hi) / 2;
        const uint8_t* e = dir + at[mid];

        if (ref_keycmp((const char*)e + 5, e[4], q, qlen) < 0) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }

    return lo ? lo - 1 : 0;
}

/* --- keys -------------------------------------------------------------------- */

static ref_reader rd;
static char cur[REF_KEY_MAX + 1];
static char probe[REF_KEY_MAX + 1];

/* Where key block b starts, and how many keys are in it and after it. */
static void key_start(ref_file* r, unsigned b, uint32_t* left)
{
    const uint8_t* e = r->keydir + r->keyblk[b];

    ref_reader_seek(&rd, r, r->o_keys + ref_rd32(e));
    *left = r->nkeys - (uint32_t)REF_BLOCK * b;
}

/* The topics of key q into out (up to max): how many there are. */
static int exact(ref_file* r, const char* q, unsigned qlen, ref_match* out, int max)
{
    uint32_t left;
    int len = 0;
    int found = 0;

    if (r->nkeys == 0) {
        return 0;
    }
    key_start(r, ref_find_block(r->keydir, r->keyblk, r->nkb, q, qlen), &left);
    while (left--) {
        int c;
        unsigned topic;

        len = ref_reader_entry(&rd, cur, len);
        topic = ref_reader_byte(&rd);
        topic |= ref_reader_byte(&rd) << 8;
        if (rd.bad) {
            break;
        }
        c = ref_keycmp(cur, (unsigned)len, q, qlen);
        if (c > 0) {
            break;
        }
        if (c == 0) {
            if (found < max) {
                memcpy(out[found].key, cur, (unsigned)len + 1);
                out[found].topic = (uint16_t)topic;
            }
            found++;
        }
    }

    return found;
}

/* The keys that start with q: how many, the first max into out. */
static int starting(ref_file* r, const char* q, unsigned qlen, ref_match* out, int max)
{
    uint32_t left;
    int len = 0;
    int found = 0;

    if (r->nkeys == 0) {
        return 0;
    }
    key_start(r, ref_find_block(r->keydir, r->keyblk, r->nkb, q, qlen), &left);
    while (left--) {
        unsigned topic;

        len = ref_reader_entry(&rd, cur, len);
        topic = ref_reader_byte(&rd);
        topic |= ref_reader_byte(&rd) << 8;
        if (rd.bad) {
            break;
        }
        if ((unsigned)len >= qlen && memcmp(cur, q, qlen) == 0) {
            if (found < max) {
                memcpy(out[found].key, cur, (unsigned)len + 1);
                out[found].topic = (uint16_t)topic;
            }
            found++;
        } else if (ref_keycmp(cur, (unsigned)len, q, qlen) > 0) {
            break;
        }
    }

    return found;
}

int ref_keys_from(ref_file* r, const char* query, ref_match* out, int max)
{
    unsigned qlen = (unsigned)ref_normalize(r, query, probe);

    return qlen ? starting(r, probe, qlen, out, max) : 0;
}

int ref_lookup(ref_file* r, const char* query, ref_match* out, int max, int* count)
{
    unsigned qlen = (unsigned)ref_normalize(r, query, probe);
    unsigned last;
    unsigned k;
    int n;

    *count = 0;
    if (qlen == 0) {
        return REF_LOOK_NONE;
    }
    n = exact(r, probe, qlen, out, max);
    if (n) {
        *count = n;

        return REF_LOOK_EXACT;
    }

    /* The prefixes it may start with, longest first: a name up to its first
     * `$` (hotkey$12 is hotkey$), then its leading tokens -- never `vdu`
     * alone, which every sequence starts with. */
    for (last = qlen; last > 0 && probe[last - 1] != ' '; last--) {
    }
    for (k = last; k + 1 < qlen; k++) {
        if (probe[k] == '$') {
            n = exact(r, probe, k + 1, out, max);
            if (n) {
                *count = n;

                return REF_LOOK_PREFIX;
            }
            break;
        }
    }
    for (k = qlen - 1; k > 0; k--) {
        if (probe[k] != ' ' || (k == 3 && memcmp(probe, "vdu", 3) == 0)) {
            continue;
        }
        n = exact(r, probe, k, out, max);
        if (n) {
            *count = n;

            return REF_LOOK_PREFIX;
        }
    }

    n = starting(r, probe, qlen, out, max);
    if (n) {
        *count = n;

        return REF_LOOK_LIST;
    }

    return REF_LOOK_NONE;
}
