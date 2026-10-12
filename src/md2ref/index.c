/*
 * The keyword and full-text indexes (docs/FORMAT.md).
 *
 * Without rules of its own, md2ref keys every heading by what it says: each
 * code span in it, the VDU sequence it names (in its code, or as its text,
 * cut at its first named parameter), and the words of its title. Keys are
 * normalised by the reader core's own ref_normalize, with this document's
 * settings -- no hex-after words, no skips -- so they are what ref will
 * look up. The full-text index takes every word of three letters or more,
 * less the stop words, from every topic.
 */
#include <stdlib.h>
#include <string.h>

#include "md2ref.h"
#include "refcore.h"
#include "refint.h"

#define BLOCK 64
#define WORD_MAX 255

/* The full-text index's stop words: ref's builder's, sorted. */
static const char* const STOP[] = {
    "a", "about", "above", "after", "again", "all", "also", "am", "an", "and", "any", "are",
    "as", "at", "be", "because", "been", "before", "being", "below", "between", "both", "but",
    "by", "can", "could", "did", "do", "does", "doing", "down", "during", "each", "few", "for",
    "from", "further", "had", "has", "have", "having", "he", "her", "here", "hers", "him",
    "his", "how", "i", "if", "in", "into", "is", "it", "its", "itself", "just", "me", "more",
    "most", "my", "no", "nor", "not", "now", "of", "off", "on", "once", "only", "or", "other",
    "our", "out", "over", "own", "same", "she", "should", "so", "some", "such", "than", "that",
    "the", "their", "them", "then", "there", "these", "they", "this", "those", "through", "to",
    "too", "under", "until", "up", "very", "was", "we", "were", "what", "when", "where",
    "which", "while", "who", "whom", "why", "will", "with", "would", "you", "your",
};
#define NSTOP (int)(sizeof STOP / sizeof STOP[0])

section keys_section;
section words_section;
const uint8_t* norm_settings;
unsigned norm_len;

static ref_file settings;           /* only what ref_normalize reads */

static int is_stop(const char* w, int len)
{
    int k;

    for (k = 0; k < NSTOP; k++) {
        if ((int)strlen(STOP[k]) == len && memcmp(STOP[k], w, (size_t)len) == 0) {
            return 1;
        }
    }

    return 0;
}

/* The settings section: no hex-after words, no skips, the stop words. */
static void make_settings(void)
{
    unsigned n = 3;
    uint8_t* s;
    int k;

    for (k = 0; k < NSTOP; k++) {
        n += 1 + (unsigned)strlen(STOP[k]);
    }
    s = arena_alloc(n);
    s[0] = 0;
    s[1] = 0;
    s[2] = (uint8_t)NSTOP;
    n = 3;
    for (k = 0; k < NSTOP; k++) {
        unsigned l = (unsigned)strlen(STOP[k]);

        s[n++] = (uint8_t)l;
        memcpy(s + n, STOP[k], l);
        n += l;
    }
    norm_settings = s;
    norm_len = n;
    settings.norm = s;
    settings.norm_skips = 1;
    settings.norm_stops = 2;
}

/* --- entries ------------------------------------------------------------------------ */

/* Each key once, as every entry for it points to it -- with the entry it
 * last had, so a key said again in the same topic adds nothing: a word's
 * every use in the text would otherwise be one entry each, which is more
 * than the Agon has room for. */
typedef struct interned interned;
struct interned {
    interned* next;
    unsigned last;          /* its latest entry + 1; 0 for none yet */
    char key[1];
};

typedef struct {
    const char* key;
    uint16_t topic;
    uint8_t in_title;
} entry;

#define BUCKETS 1024

static interned* bucket[BUCKETS];
static entry* entries;
static unsigned nentries;
static unsigned max_entries;

static interned* intern(const char* key, int len)
{
    unsigned h = 0;
    interned* w;
    int k;

    for (k = 0; k < len; k++) {
        h = h * 31 + (uint8_t)key[k];
    }
    h %= BUCKETS;
    for (w = bucket[h]; w; w = w->next) {
        if (strncmp(w->key, key, (size_t)len) == 0 && w->key[len] == 0) {
            return w;
        }
    }
    w = arena_alloc((unsigned)(sizeof(interned) + len));
    memcpy(w->key, key, (size_t)len);
    w->next = bucket[h];
    bucket[h] = w;

    return w;
}

/* Entries start again, and the keys with them. */
static void new_entries(void)
{
    memset(bucket, 0, sizeof bucket);
    nentries = 0;
}

static void add_entry(const char* key, int len, int topic, int in_title)
{
    interned* w;

    if (len <= 0 || len > WORD_MAX) {
        return;
    }
    w = intern(key, len);
    if (w->last && entries[w->last - 1].topic == topic) {
        entries[w->last - 1].in_title |= (uint8_t)in_title;

        return;
    }
    if (nentries == max_entries) {
        max_entries = max_entries ? max_entries * 2 : 1024;
        entries = md_realloc(entries, max_entries * (unsigned)sizeof(entry));
    }
    entries[nentries].key = w->key;
    entries[nentries].topic = (uint16_t)topic;
    entries[nentries].in_title = (uint8_t)in_title;
    nentries++;
    w->last = nentries;
}

static int by_key(const void* a, const void* b)
{
    const entry* x = a;
    const entry* y = b;
    int c = strcmp(x->key, y->key);

    return c ? c : (int)x->topic - (int)y->topic;
}

/* The entries sorted and each (key, topic) once -- in_title if any was. */
static void sort_entries(void)
{
    unsigned k;
    unsigned o = 0;

    if (nentries == 0) {
        return;                             /* no keys: every word a stop word */
    }
    qsort(entries, nentries, sizeof(entry), by_key);
    for (k = 0; k < nentries; k++) {
        if (o && strcmp(entries[o - 1].key, entries[k].key) == 0
            && entries[o - 1].topic == entries[k].topic) {
            entries[o - 1].in_title |= entries[k].in_title;
        } else {
            entries[o++] = entries[k];
        }
    }
    nentries = o;
}

static uint8_t* grow(uint8_t* p, unsigned* max, unsigned need)
{
    if (need > *max) {
        while (*max < need) {
            *max = *max ? *max * 2 : 4096;
        }
        p = md_realloc(p, *max);
    }

    return p;
}

static unsigned varint(uint8_t* o, unsigned v)
{
    unsigned n = 0;

    while (v >= 0x80) {
        o[n++] = (uint8_t)(v | 0x80);
        v >>= 7;
    }
    o[n++] = (uint8_t)v;

    return n;
}

/* Front-coded blocks of keys from the sorted entries: with words, each key
 * once with its topics; else each (key, topic) with its topic. */
/* The keys section: each (key, topic), front-coded in blocks of BLOCK. */
static void key_blocks(section* s)
{
    unsigned max = 0;
    unsigned dmax = 0;
    uint8_t* data = NULL;
    uint8_t* dir = NULL;
    const char* prev = "";
    unsigned count = 0;
    unsigned k = 0;

    s->len = s->dir_len = 0;
    while (k < nentries) {
        const char* key = entries[k].key;
        unsigned klen = (unsigned)strlen(key);
        unsigned shared = 0;

        if (count % BLOCK == 0) {
            dir = grow(dir, &dmax, s->dir_len + 5 + klen);
            unsigned long at = s->len;      /* unsigned is 24 bits on the eZ80 */

            dir[s->dir_len++] = (uint8_t)(at & 0xFF);
            dir[s->dir_len++] = (uint8_t)(at >> 8);
            dir[s->dir_len++] = (uint8_t)(at >> 16);
            dir[s->dir_len++] = (uint8_t)(at >> 24);
            dir[s->dir_len++] = (uint8_t)klen;
            memcpy(dir + s->dir_len, key, klen);
            s->dir_len += klen;
            prev = "";
        }
        while (shared < 255 && prev[shared] && prev[shared] == key[shared]) {
            shared++;
        }
        data = grow(data, &max, (unsigned)s->len + 2 + klen + 2);
        data[s->len++] = (uint8_t)shared;
        data[s->len++] = (uint8_t)(klen - shared);
        memcpy(data + s->len, key + shared, klen - shared);
        s->len += klen - shared;
        data[s->len++] = (uint8_t)(entries[k].topic & 0xFF);
        data[s->len++] = (uint8_t)(entries[k].topic >> 8);
        prev = key;
        count++;
        k++;
    }
    s->data = data;
    s->dir = dir;
    s->count = count;
    s->blocks = (count + BLOCK - 1) / BLOCK;
}

/* --- keys --------------------------------------------------------------------------- */

static void add_key(const char* text, int topic)
{
    static char norm[REF_KEY_MAX + 1];
    int n = ref_normalize(&settings, text, norm);

    add_entry(norm, n, topic, 0);
}

/* The key a VDU sequence gives: normalised, cut at its first named
 * parameter. Into out; 0 if it is not one. */
static int vdu_key(const char* text, char* out)
{
    int n = ref_normalize(&settings, text, out);
    int k;
    int end = 3;

    if (n < 5 || memcmp(out, "vdu ", 4) != 0 || out[4] < '0' || out[4] > '9') {
        return 0;
    }
    for (k = 4; k <= n; k++) {
        if (k == n || out[k] == ' ') {
            int start = end + 1;
            int j;

            for (j = start; j < k; j++) {
                if (out[j] < '0' || out[j] > '9') {
                    out[end] = 0;

                    return end;
                }
            }
            end = k;
        }
    }
    out[end] = 0;

    return end;
}

static int is_word_char(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '$';
}

/* The words of s (lower-cased first): each with fn. */
static void each_word(const char* s, int min, void (*fn)(const char* w, int len, int topic, int arg),
                      int topic, int arg)
{
    static char w[WORD_MAX + 1];

    while (*s) {
        int n = 0;
        int digits = 1;
        int real = 0;

        while (*s && !is_word_char(*s >= 'A' && *s <= 'Z' ? *s + 32 : *s)) {
            s++;
        }
        while (*s) {
            int c = *s >= 'A' && *s <= 'Z' ? *s + 32 : *s;

            if (!is_word_char(c)) {
                break;
            }
            if (n < WORD_MAX) {
                w[n++] = (char)c;
            }
            digits &= c >= '0' && c <= '9';
            real |= c != '_' && c != '$';
            s++;
        }
        if (n >= min && !digits && real && !is_stop(w, n)) {
            fn(w, n, topic, arg);
        }
    }
}

static int is_number_word(const char* w, int len)
{
    static char t[WORD_MAX + 1];
    static char norm[REF_KEY_MAX + 1];

    memcpy(t, w, (size_t)len);
    t[len] = 0;
    ref_normalize(&settings, t, norm);

    return strncmp(norm, "vdu ", 4) == 0;   /* a leading number becomes vdu */
}

static void title_word(const char* w, int len, int topic, int arg)
{
    static char t[WORD_MAX + 1];

    (void)arg;
    if (is_number_word(w, len)) {
        return;                             /* 0x16 is not a word: it would be vdu 22 */
    }
    memcpy(t, w, (size_t)len);
    t[len] = 0;
    add_key(t, topic);
}

void make_keys(void)
{
    static char code[REF_KEY_MAX + 1];
    static char key[REF_KEY_MAX + 1];
    arena_mark interned_keys;
    int t;

    make_settings();                    /* first: it lasts, the keys' scratch does not */
    interned_keys = arena_here();
    new_entries();
    for (t = 0; t < ntopics; t++) {
        const char* h = topics[t].heading;
        int i;
        int got = 0;

        each_word(topics[t].title, 2, title_word, t, 0);
        if (!h) {
            continue;
        }
        for (i = 0; inline_code(h, i, code, (int)sizeof code); i++) {
            add_key(code, t);
            if (vdu_key(code, key)) {
                add_entry(key, (int)strlen(key), t, 0);
                got = 1;
            }
        }
        if (!got && (strncmp(topics[t].title, "VDU ", 4) == 0 || strncmp(topics[t].title, "vdu ", 4) == 0)
            && vdu_key(topics[t].title, key)) {
            add_entry(key, (int)strlen(key), t, 0);
        }
    }
    sort_entries();
    key_blocks(&keys_section);
    arena_back(interned_keys);
    free(entries);
    entries = NULL;
    nentries = max_entries = 0;
}

/* --- words ----------------------------------------------------------------------- */

/* Nothing of the words is kept in memory but the topic's being read: each
 * topic's words, once each, go to the words file as records -- a length,
 * the word, and the topic's number as made, its top bit set if the word is
 * in the title. At the end the file is sorted on the card, in runs that
 * fit RUN bytes, merged a small window per run. However many different
 * words a document has, the memory is the same. */

#define RUN      24576      /* records sorted in memory at once */
#define RUN_WIN  512        /* a run's window, merging */
#define MAX_RUNS 64

/* The topic's words: a flag byte, the word, a NUL, one after another. */
static char* wbuf;
static unsigned wlen;
static unsigned wcap;
static unsigned* occ;
static unsigned nocc;
static unsigned occ_cap;
static int word_topic;

static void text_word(const char* key, int len, int topic, int in_title)
{
    (void)topic;
    if (wlen + (unsigned)len + 2 > wcap) {
        wcap = (wlen + (unsigned)len + 2) * 2;
        wbuf = md_realloc(wbuf, wcap);
    }
    if (nocc == occ_cap) {
        occ_cap = occ_cap ? occ_cap * 2 : 256;
        occ = md_realloc(occ, occ_cap * (unsigned)sizeof(unsigned));
    }
    occ[nocc++] = wlen;
    wbuf[wlen++] = (char)in_title;
    memcpy(wbuf + wlen, key, (size_t)len);
    wlen += (unsigned)len;
    wbuf[wlen++] = 0;
}

void words_begin(int t)
{
    word_topic = topics[t].made;
    wlen = 0;
    nocc = 0;
}

void words_text(const char* plain, int in_title)
{
    each_word(plain, 3, text_word, word_topic, in_title);
}

static int by_occ(const void* a, const void* b)
{
    return strcmp(wbuf + *(const unsigned*)a + 1, wbuf + *(const unsigned*)b + 1);
}

void words_end(void)
{
    unsigned k = 0;

    if (nocc) {
        qsort(occ, nocc, sizeof(unsigned), by_occ);
    }
    while (k < nocc) {
        const char* key = wbuf + occ[k] + 1;
        unsigned in_title = 0;
        unsigned klen = (unsigned)strlen(key);
        unsigned tf;
        uint8_t r[3];

        while (k < nocc && strcmp(wbuf + occ[k] + 1, key) == 0) {
            in_title |= (unsigned)wbuf[occ[k]];
            k++;
        }
        tf = (unsigned)word_topic | in_title << 15;
        r[0] = (uint8_t)klen;
        words_put(r, 1);
        words_put((const uint8_t*)key, klen);
        r[1] = (uint8_t)(tf & 0xFF);
        r[2] = (uint8_t)(tf >> 8);
        words_put(r + 1, 2);
    }
    nocc = 0;
    wlen = 0;
}

/* --- sorting the words file ---------------------------------------------------- */

static uint8_t* run;                /* RUN bytes */
static unsigned* rec;               /* its records' offsets */
static unsigned nrec;
static unsigned rec_cap;

static int key_cmp(const uint8_t* a, const uint8_t* b)
{
    unsigned la = a[0];
    unsigned lb = b[0];
    int c = memcmp(a + 1, b + 1, la < lb ? la : lb);

    return c ? c : (int)la - (int)lb;
}

static int by_rec(const void* a, const void* b)
{
    return key_cmp(run + *(const unsigned*)a, run + *(const unsigned*)b);
}

typedef struct {
    unsigned long pos;      /* the next byte to read, in the runs file */
    unsigned long end;
    uint8_t buf[RUN_WIN];
    unsigned have;
    unsigned at;            /* the current record in buf */
} window;

/* The run's current record whole in its window: 1, or 0 at the run's end. */
static int ready(window* w)
{
    unsigned rest = w->have - w->at;

    if (rest == 0 || rest < 1u + w->buf[w->at] + 2u) {
        unsigned n = RUN_WIN - rest;

        memmove(w->buf, w->buf + w->at, rest);
        w->have = rest;
        w->at = 0;
        if (n > w->end - w->pos) {
            n = (unsigned)(w->end - w->pos);
        }
        if (n) {
            n = runs_get(w->pos, w->buf + w->have, n);
            w->pos += n;
            w->have += n;
        }
        rest = w->have;
    }

    return rest >= 3 && rest >= 1u + w->buf[0 + w->at] + 2u;
}

/* The words file sorted into runs in the runs file: how many, or -1. */
static int make_runs(unsigned long* starts)
{
    static uint8_t in[RUN_WIN];
    unsigned have = 0;
    unsigned at = 0;
    unsigned used = 0;
    int nruns = 0;
    int done = 0;

    if (words_rewind() != 0 || runs_open() != 0) {
        return -1;
    }
    while (!done) {
        unsigned rest = have - at;
        unsigned k;

        /* The next record, whole, into the run -- or the run out. */
        if (rest == 0 || rest < 1u + in[at] + 2u) {
            memmove(in, in + at, rest);
            have = rest + words_get(in + rest, RUN_WIN - rest);
            at = 0;
            rest = have;
        }
        if (rest >= 3 && rest >= 1u + in[at] + 2u && used + 1u + in[at] + 2u <= RUN) {
            unsigned size = 1u + in[at] + 2u;

            if (nrec == rec_cap) {
                rec_cap = rec_cap ? rec_cap * 2 : 1024;
                rec = md_realloc(rec, rec_cap * (unsigned)sizeof(unsigned));
            }
            rec[nrec++] = used;
            memcpy(run + used, in + at, size);
            used += size;
            at += size;
            continue;
        }
        done = !(rest >= 3);
        if (nrec == 0) {
            continue;
        }
        if (nruns == MAX_RUNS) {
            md_error("md2ref", 0, "too many words: more than %u bytes of them", (unsigned)(RUN * MAX_RUNS));

            return -1;
        }
        qsort(rec, nrec, sizeof(unsigned), by_rec);
        starts[nruns++] = runs_size();
        for (k = 0; k < nrec; k++) {
            runs_put(run + rec[k], 1u + run[rec[k]] + 2u);
        }
        nrec = 0;
        used = 0;
    }
    starts[nruns] = runs_size();

    return runs_reread() == 0 ? nruns : -1;
}

/* A word's topics, as numbered now, sorted, each once (in_title if any
 * was). */
static unsigned tidy(uint16_t* v, unsigned n)
{
    unsigned i;
    unsigned o = 0;

    for (i = 0; i < n; i++) {
        v[i] = (uint16_t)((unsigned)final_of[v[i] & 0x7FFF] | (v[i] & 0x8000));
    }
    for (i = 1; i < n; i++) {
        uint16_t x = v[i];
        unsigned j = i;

        while (j > 0 && (v[j - 1] & 0x7FFF) > (x & 0x7FFF)) {
            v[j] = v[j - 1];
            j--;
        }
        v[j] = x;
    }
    for (i = 0; i < n; i++) {
        if (o && (v[o - 1] & 0x7FFF) == (v[i] & 0x7FFF)) {
            v[o - 1] |= v[i] & 0x8000;
        } else {
            v[o++] = v[i];
        }
    }

    return o;
}

int write_words(void (*emit)(const void* p, unsigned n))
{
    static unsigned long starts[MAX_RUNS + 1];
    static uint8_t key[WORD_MAX + 1];
    static uint8_t prev[WORD_MAX + 1];
    static uint8_t out[16];
    window* win;
    uint16_t* v = NULL;
    unsigned vcap = 0;
    unsigned dmax = 0;
    int nruns;
    int r;

    words_section.len = 0;
    words_section.dir_len = 0;
    words_section.dir = NULL;
    words_section.count = 0;
    run = md_realloc(NULL, RUN);
    nruns = make_runs(starts);
    free(run);
    run = NULL;
    free(rec);
    rec = NULL;
    rec_cap = 0;
    if (nruns < 0) {
        return -1;
    }
    win = md_realloc(NULL, (unsigned)(nruns + 1) * (unsigned)sizeof(window));
    for (r = 0; r < nruns; r++) {
        win[r].pos = starts[r];
        win[r].end = starts[r + 1];
        win[r].have = win[r].at = 0;
    }
    prev[0] = 0;

    /* Merged: the least word of the runs' current records, from each run
     * that has it, its topics gathered; then written. */
    for (;;) {
        int least = -1;
        unsigned n = 0;
        unsigned klen;
        unsigned shared = 0;
        unsigned o;
        unsigned j;
        unsigned at_topic = 0;

        for (r = 0; r < nruns; r++) {
            if (ready(&win[r])
                && (least < 0 || key_cmp(win[r].buf + win[r].at, win[least].buf + win[least].at) < 0)) {
                least = r;
            }
        }
        if (least < 0) {
            break;
        }
        klen = win[least].buf[win[least].at];
        memcpy(key, win[least].buf + win[least].at, klen + 1);
        for (r = 0; r < nruns; r++) {
            while (ready(&win[r]) && key_cmp(win[r].buf + win[r].at, key) == 0) {
                const uint8_t* p = win[r].buf + win[r].at + 1 + klen;

                if (n == vcap) {
                    vcap = vcap ? vcap * 2 : 64;
                    v = md_realloc(v, vcap * (unsigned)sizeof(uint16_t));
                }
                v[n++] = (uint16_t)(p[0] | p[1] << 8);
                win[r].at += 1 + klen + 2;
            }
        }
        n = tidy(v, n);

        if (words_section.count % BLOCK == 0) {
            unsigned long at = words_section.len;   /* unsigned is 24 bits on the eZ80 */

            words_section.dir = grow(words_section.dir, &dmax, words_section.dir_len + 5 + klen);
            words_section.dir[words_section.dir_len++] = (uint8_t)(at & 0xFF);
            words_section.dir[words_section.dir_len++] = (uint8_t)(at >> 8);
            words_section.dir[words_section.dir_len++] = (uint8_t)(at >> 16);
            words_section.dir[words_section.dir_len++] = (uint8_t)(at >> 24);
            words_section.dir[words_section.dir_len++] = (uint8_t)klen;
            memcpy(words_section.dir + words_section.dir_len, key + 1, klen);
            words_section.dir_len += klen;
            prev[0] = 0;
        }
        while (shared < 255 && shared < prev[0] && shared < klen && prev[1 + shared] == key[1 + shared]) {
            shared++;
        }
        out[0] = (uint8_t)shared;
        out[1] = (uint8_t)(klen - shared);
        emit(out, 2);
        emit(key + 1 + shared, klen - shared);
        words_section.len += 2 + klen - shared;
        o = varint(out, n);
        emit(out, o);
        words_section.len += o;
        for (j = 0; j < n; j++) {
            unsigned t = v[j] & 0x7FFF;

            o = varint(out, (t - at_topic) << 1 | (unsigned)(v[j] >> 15));
            emit(out, o);
            words_section.len += o;
            at_topic = t;
        }
        memcpy(prev, key, klen + 1);
        words_section.count++;
    }
    words_section.blocks = (words_section.count + BLOCK - 1) / BLOCK;
    free(win);
    free(v);

    return 0;
}
