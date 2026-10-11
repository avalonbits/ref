/*
 * The full-text search build/refbuild/fulltext.py specifies: every indexed
 * word that starts with each word of the query, and the topics ranked by
 * query words in their titles, then query words they have, then book order.
 */
#include <string.h>

#include "refcore.h"
#include "refint.h"

#define MIN_QUERY 2
#define QWORDS    8

static ref_reader rd;
static char cur[REF_KEY_MAX + 1];
static char qbuf[REF_KEY_MAX + 1];
static uint8_t qat[QWORDS];
static uint8_t qlen[QWORDS];

int ref_stop_word(const ref_file* r, const char* w, unsigned len)
{
    const uint8_t* s = r->norm;
    unsigned at = r->norm_stops;
    unsigned n = s[at];

    at = at + 1;
    for (; n > 0; n--) {
        if (s[at] == len && memcmp(s + at + 1, w, len) == 0) {
            return 1;
        }
        at = at + 1 + s[at];
    }

    return 0;
}

static int word_char(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '$';
}

/* The query's words, lower case, two letters or more, not stop words, each
 * once: into qbuf, at qat/qlen. How many. */
static unsigned query_words(const ref_file* r, const char* query)
{
    unsigned used = 0;
    unsigned n = 0;
    const char* c = query;

    while (*c && n < QWORDS) {
        unsigned start = used;
        unsigned k;
        int dup = 0;

        while (*c) {
            int ch = (unsigned char)*c;

            if (ch >= 'A' && ch <= 'Z') {
                ch += 32;
            }
            if (word_char(ch)) {
                break;
            }
            c++;
        }
        while (*c) {
            int ch = (unsigned char)*c;

            if (ch >= 'A' && ch <= 'Z') {
                ch += 32;
            }
            if (!word_char(ch)) {
                break;
            }
            if (used < REF_KEY_MAX) {
                qbuf[used++] = (char)ch;
            }
            c++;
        }
        if (used - start < MIN_QUERY || used - start > 255
            || ref_stop_word(r, qbuf + start, used - start)) {
            used = start;
            continue;
        }
        for (k = 0; k < n; k++) {
            if (qlen[k] == used - start && memcmp(qbuf + qat[k], qbuf + start, qlen[k]) == 0) {
                dup = 1;
            }
        }
        if (dup) {
            used = start;
            continue;
        }
        qat[n] = (uint8_t)start;
        qlen[n] = (uint8_t)(used - start);
        n++;
    }

    return n;
}

/* Is a ranked above b? */
static int better(const ref_file* r, unsigned a, unsigned b)
{
    const uint8_t* title = r->scores;
    const uint8_t* words = r->scores + r->ntopics;

    if (title[a] != title[b]) {
        return title[a] > title[b];
    }
    if (words[a] != words[b]) {
        return words[a] > words[b];
    }

    return a < b;
}

int ref_search(ref_file* r, const char* query, ref_hit* hits, int max)
{
    unsigned bitmap = (r->ntopics + 7u) / 8u;
    uint8_t* hit = r->bits;
    uint8_t* in_title = r->bits + bitmap;
    uint8_t* title = r->scores;
    uint8_t* words = r->scores + r->ntopics;
    unsigned nq = query_words(r, query);
    unsigned q;
    unsigned t;
    int found = 0;

    if (nq == 0 || r->nwords == 0) {
        return 0;
    }
    memset(r->scores, 0, 2u * r->ntopics);
    for (q = 0; q < nq; q++) {
        const char* w = qbuf + qat[q];
        unsigned wl = qlen[q];
        unsigned b = ref_find_block(r->worddir, r->wordblk, r->nwb, w, wl);
        const uint8_t* e = r->worddir + r->wordblk[b];
        uint32_t left = r->nwords - (uint32_t)REF_BLOCK * b;
        int len = 0;

        memset(hit, 0, 2u * bitmap);
        ref_reader_seek(&rd, r, r->o_words + ref_rd32(e));
        while (left--) {
            uint32_t n;
            unsigned prev = 0;
            int match;

            len = ref_reader_entry(&rd, cur, len);
            n = ref_reader_varint(&rd);
            if (rd.bad) {
                break;
            }
            match = (unsigned)len >= wl && memcmp(cur, w, wl) == 0;
            if (!match && ref_keycmp(cur, (unsigned)len, w, wl) > 0) {
                break;
            }
            while (n-- && !rd.bad) {
                uint32_t v = ref_reader_varint(&rd);

                if (match) {
                    prev += (unsigned)(v >> 1);
                    if (prev < r->ntopics) {
                        hit[prev >> 3] |= (uint8_t)(1 << (prev & 7));
                        if (v & 1) {
                            in_title[prev >> 3] |= (uint8_t)(1 << (prev & 7));
                        }
                    }
                }
            }
        }
        for (t = 0; t < r->ntopics; t++) {
            if (hit[t >> 3] & (1 << (t & 7))) {
                words[t]++;
            }
            if (in_title[t >> 3] & (1 << (t & 7))) {
                title[t]++;
            }
        }
    }

    /* The best max, by insertion: topics come in book order, so a tie keeps
     * the earlier first. */
    for (t = 0; t < r->ntopics; t++) {
        int k;

        if (!words[t]) {
            continue;
        }
        found++;
        k = found <= max ? found - 1 : max;
        while (k > 0 && better(r, t, hits[k - 1].topic)) {
            if (k < max) {
                hits[k] = hits[k - 1];
            }
            k--;
        }
        if (k < max) {
            hits[k].topic = (uint16_t)t;
            hits[k].title = title[t];
            hits[k].words = words[t];
        }
    }

    return found;
}
