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
        if (memcmp(w->key, key, (size_t)len) == 0 && w->key[len] == 0) {
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
static void blocks(section* s, int words)
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
        unsigned end = k + 1;

        if (words) {
            while (end < nentries && strcmp(entries[end].key, key) == 0) {
                end++;
            }
        }
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
        data = grow(data, &max, s->len + 2 + klen + 5 + (end - k) * 5);
        data[s->len++] = (uint8_t)shared;
        data[s->len++] = (uint8_t)(klen - shared);
        memcpy(data + s->len, key + shared, klen - shared);
        s->len += klen - shared;
        if (words) {
            unsigned j;
            unsigned last = 0;

            s->len += varint(data + s->len, end - k);
            for (j = k; j < end; j++) {
                s->len += varint(data + s->len,
                                 (entries[j].topic - last) << 1 | entries[j].in_title);
                last = entries[j].topic;
            }
        } else {
            data[s->len++] = (uint8_t)(entries[k].topic & 0xFF);
            data[s->len++] = (uint8_t)(entries[k].topic >> 8);
        }
        prev = key;
        count++;
        k = end;
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

static void make_keys(void)
{
    static char code[REF_KEY_MAX + 1];
    static char key[REF_KEY_MAX + 1];
    arena_mark interned_keys = arena_here();
    int t;

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
    blocks(&keys_section, 0);
    arena_back(interned_keys);
}

/* --- words -------------------------------------------------------------------------- */

static void text_word(const char* w, int len, int topic, int in_title)
{
    add_entry(w, len, topic, in_title);
}

static void make_words(void)
{
    arena_mark interned_words = arena_here();
    int t;

    new_entries();
    for (t = 0; t < ntopics; t++) {
        uint8_t* m = md_realloc(NULL, markup[t].len + 1);
        char* plain = md_realloc(NULL, markup[t].len + 2);

        unpack(t, m);
        markup_plain(m, markup[t].len, plain, markup[t].len + 2);
        free(m);
        each_word(plain, 3, text_word, t, 0);
        each_word(topics[t].title, 3, text_word, t, 1);
        free(plain);
    }
    sort_entries();
    blocks(&words_section, 1);
    arena_back(interned_words);
}

int make_indexes(void)
{
    make_settings();
    make_keys();
    make_words();
    free(entries);
    entries = NULL;
    max_entries = 0;

    return 0;
}
