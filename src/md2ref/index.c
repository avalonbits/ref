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

/* For each byte, 0 if it is not in a word, else the byte lower-cased; and
 * the stop words by first letter, STOP[stop_at[c - 'a']] on to
 * STOP[stop_at[c - 'a' + 1]], with their lengths. Every word of every
 * topic passes through these. */
#define NOT_DIGIT 1
#define REAL      2         /* not only '_' and '$' */

static uint8_t word_char[256];
static uint8_t word_kind[256];      /* NOT_DIGIT | REAL, for a byte in a word */
static uint8_t word_as_is[256];     /* in a word as it is: a-z, 0-9, _ and $ */
static uint8_t half_of[256];        /* n / 2, as a byte: a shift of an int is a call */
/* The stop words packed, each its length and its letters, those of a
 * first letter together from stop_off[c - 'a']; and for each first
 * letter, a bit for each length one of them has. */
static uint8_t stop_pack[1024];
static uint16_t stop_off[27];
static uint8_t stop_lens[26];
static const uint8_t len_bit[8] = { 0, 1, 2, 4, 8, 16, 32, 64 };

static void make_tables(void)
{
    int c;
    int k = 0;
    unsigned n = 0;

    for (c = 0; c < 256; c++) {
        half_of[c] = (uint8_t)(c / 2);
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '$') {
            word_char[c] = (uint8_t)c;
        } else if (c >= 'A' && c <= 'Z') {
            word_char[c] = (uint8_t)(c + 32);
        }
        word_as_is[c] = (uint8_t)(c && word_char[c] == c);
        word_kind[c] = (uint8_t)((c >= '0' && c <= '9' ? 0 : NOT_DIGIT)
                                 | (c != '_' && c != '$' ? REAL : 0));
    }
    for (c = 0; c < 26; c++) {
        stop_off[c] = (uint16_t)n;
        while (k < NSTOP && STOP[k][0] == 'a' + c) {
            unsigned l = (unsigned)strlen(STOP[k]);

            stop_pack[n++] = (uint8_t)l;
            memcpy(stop_pack + n, STOP[k], l);
            n += l;
            stop_lens[c] |= len_bit[l];
            k++;
        }
    }
    stop_off[26] = (uint16_t)n;
}

#define STOP_MAX 7          /* the longest stop word */

/* Every word of every topic is asked: most are longer than any stop word,
 * or no stop word starts with their letter and has their length, which
 * byte operations say; the rest are looked for among the few that do. */
static int is_stop(const char* word, int len)
{
    const uint8_t* w = (const uint8_t*)word;
    uint8_t c = (uint8_t)(w[0] - 'a');
    const uint8_t* p;
    const uint8_t* e;

    if (len > STOP_MAX || c >= 26 || !(stop_lens[c] & len_bit[len])) {
        return 0;
    }
    p = stop_pack + stop_off[c];
    e = stop_pack + stop_off[c + 1];
    while (p < e) {
        if (p[0] == len && (len < 2 || p[2] == w[1]) && memcmp(p + 1, w, (size_t)len) == 0) {
            return 1;
        }
        p += 1 + p[0];
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
    unsigned hash;
    unsigned last;          /* its latest entry + 1; 0 for none yet */
    char key[1];
};

/* Eight bytes: entries[k] a shift, where at six it was a call to multiply
 * at every use, building, sorting and front-coding the keys. */
typedef struct {
    const char* key;
    uint16_t topic;
    uint8_t in_title;
    uint8_t pad[2];
} entry;

/* Twice as many buckets as keys at most, so a chain is a key or two
 * however many there are. */
static interned** bucket;
static unsigned nbuckets;                   /* a power of two */
static unsigned ninterned;
static entry* entries;
static unsigned nentries;
static unsigned max_entries;

static void more_buckets(void)
{
    unsigned old = nbuckets;
    interned** was = bucket;
    unsigned k;

    nbuckets = nbuckets ? nbuckets * 2 : 1024;
    bucket = md_realloc(NULL, nbuckets * (unsigned)sizeof(interned*));
    memset(bucket, 0, nbuckets * sizeof(interned*));
    for (k = 0; k < old; k++) {
        interned* w = was[k];

        while (w) {
            interned* next = w->next;

            w->next = bucket[w->hash & (nbuckets - 1)];
            bucket[w->hash & (nbuckets - 1)] = w;
            w = next;
        }
    }
    free(was);
}

static interned* intern(const char* key, int len)
{
    unsigned h;
    interned* w;

    h = md_hash(0, key, (unsigned)len);
    for (w = bucket[h & (nbuckets - 1)]; w; w = w->next) {
        if (w->hash == h && strncmp(w->key, key, (size_t)len) == 0 && w->key[len] == 0) {
            return w;
        }
    }
    if (++ninterned > nbuckets * 2) {
        more_buckets();
    }
    w = arena_alloc((unsigned)(sizeof(interned) + len));
    memcpy(w->key, key, (size_t)len);
    w->hash = h;
    w->next = bucket[h & (nbuckets - 1)];
    bucket[h & (nbuckets - 1)] = w;

    return w;
}

/* Entries start again, and the keys with them. */
static void new_entries(void)
{
    free(bucket);
    bucket = NULL;
    nbuckets = ninterned = 0;
    more_buckets();
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
    md_sort(entries, nentries, (unsigned)sizeof(entry), by_key);
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

/* The keys go to the keys file as each topic is made -- a record each, its
 * length, the key and the topic's number as made -- and are read back at
 * the end: so a heading's Markdown is not kept to the end for them, as a
 * topic's text is not. */
static int key_made;                    /* the topic whose keys these are, as made */

static void put_key(const char* key, int len)
{
    uint8_t r[3];

    if (len <= 0 || len > WORD_MAX) {
        return;
    }
    r[0] = (uint8_t)len;
    r[1] = (uint8_t)(key_made & 0xFF);
    r[2] = (uint8_t)((unsigned)key_made >> 8);
    keys_put(r, 1);
    keys_put((const uint8_t*)key, (unsigned)len);
    keys_put(r + 1, 2);
}

static void add_key(const char* text, int topic)
{
    static char norm[REF_KEY_MAX + 1];
    int n = ref_normalize(&settings, text, norm);

    (void)topic;
    put_key(norm, n);
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

/* The words of s (lower-cased first): each with fn. A word's kind is its
 * bytes' kinds or'ed, a byte at a time: an 8-bit or is in line, where an
 * int's is a call. */
static void each_word(const char* text, int min, void (*fn)(const char* w, int len, int topic, int arg),
                      int topic, int arg)
{
    static uint8_t w[WORD_MAX + 1];
    const uint8_t* s = (const uint8_t*)text;

    if (!word_char['a']) {
        make_tables();
    }
    for (;;) {
        uint8_t* o = w;
        uint8_t kind = 0;
        uint8_t c;
        int n;

        while (!word_char[*s]) {
            if (!*s) {
                return;
            }
            s++;
        }
        while ((c = word_char[*s]) != 0) {
            if (o < w + WORD_MAX) {
                *o++ = c;
            }
            kind |= word_kind[*s];
            s++;
        }
        n = (int)(o - w);
        if (n >= min && kind == (NOT_DIGIT | REAL) && !is_stop((const char*)w, n)) {
            fn((const char*)w, n, topic, arg);
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
    /* A word as each_word gives it -- lower case, of a-z, 0-9, _ and $ --
     * that starts with a letter, _ or $ normalises to itself, with this
     * document's settings (no skips, no words that hex follows): only one
     * that starts with a digit can be a number (16, 0x16, 16h, 16_ff), so
     * only it is normalised, twice -- to say, and as its key. Every title's
     * every word was, and it was a tenth of the time. */
    if (w[0] < '0' || w[0] > '9') {
        put_key(w, len);

        return;
    }
    if (is_number_word(w, len)) {
        return;                             /* 0x16 is not a word: it would be vdu 22 */
    }
    memcpy(t, w, (size_t)len);
    t[len] = 0;
    add_key(t, topic);
}

/* A topic's keys, as it is made: the words of its title, each code span
 * in its heading, and the VDU sequence it names -- into the keys file. */
void keys_topic(int made, const char* title, const char* heading)
{
    static char code[REF_KEY_MAX + 1];
    static char key[REF_KEY_MAX + 1];
    int i;
    int got = 0;

    if (!norm_settings) {
        make_settings();                /* first: it lasts to the end */
    }
    key_made = made;
    each_word(title, 2, title_word, made, 0);
    if (!heading) {
        return;
    }
    for (i = 0; inline_code(heading, &i, code, (int)sizeof code);) {
        add_key(code, made);
        if (vdu_key(code, key)) {
            put_key(key, (int)strlen(key));
            got = 1;
        }
    }
    if (!got && (strncmp(title, "VDU ", 4) == 0 || strncmp(title, "vdu ", 4) == 0) && vdu_key(title, key)) {
        put_key(key, (int)strlen(key));
    }
}

/* The keys file read back, each key to its topic as numbered now. */
void make_keys(void)
{
    static uint8_t in[512];
    unsigned have = 0;
    unsigned at = 0;
    arena_mark interned_keys = arena_here();

    new_entries();
    if (keys_rewind() == 0) {
        for (;;) {
            unsigned rest = have - at;

            if (rest == 0 || rest < 1u + in[at] + 2u) {
                memmove(in, in + at, rest);
                have = rest + keys_get(in + rest, (unsigned)sizeof in - rest);
                at = 0;
                rest = have;
                if (rest == 0 || rest < 1u + in[0] + 2u) {
                    break;
                }
            }
            {
                unsigned len = in[at];
                unsigned made_no = in[at + 1 + len] | (unsigned)in[at + 2 + len] << 8;

                add_entry((const char*)in + at + 1, (int)len, final_of[made_no], 0);
                at += 1 + len + 2;
            }
        }
    }
    sort_entries();
    key_blocks(&keys_section);
    arena_back(interned_keys);
    free(entries);
    entries = NULL;
    free(bucket);                       /* its keys went with arena_back */
    bucket = NULL;
    nbuckets = ninterned = 0;
    nentries = max_entries = 0;
}

/* --- words ----------------------------------------------------------------------- */

/* The words go to the words file as they are found, in one of two kinds
 * of record. Most words are in the dictionary, each different word once,
 * numbered as found: each topic's use of one is a posting, a 0 then its
 * number and the topic's (as made, its top bit set if the word is in the
 * title) -- five bytes, the word itself not repeated, and nothing to sort,
 * as a topic is found once a word's last topic is not it. At the end the
 * dictionary is sorted, and the postings gathered a range of its words at
 * a time.
 *
 * The dictionary has DICT_BYTES, taken whole at the start. The words
 * found once it is full -- fewer, and rarer, as the common ones come
 * early -- go as the word:
 * its length, it, and the topic's number, each once a topic; and these
 * are sorted on the card, in runs that fit RUN bytes, merged a small
 * window per run, and woven in with the dictionary's as they are written.
 * However many different words a document has, the memory is bounded. */

#define RUN      16384      /* records sorted in memory at once: their offsets are 16 bits */
#define RUN_WIN  512        /* a run's window, merging */
#define MAX_RUNS 64

/* The topic's words, each once, as the records they go out as: one after
 * another in wbuf, found again through a hash of them. A word said again
 * adds nothing but, in the title, the title's bit. */
static uint8_t* wbuf;
static unsigned wlen;
static unsigned wcap;
static uint16_t* slot;              /* a record's offset in wbuf + 1; 0 for none */
static unsigned nslots;             /* a power of two, at least twice the words */
static unsigned slot_cap;
static unsigned nwords;
static unsigned word_topic;
static uint8_t topic_lo;            /* word_topic as bytes, for postings */
static uint8_t topic_hi;

#define FIRST_SLOTS 64

/* The dictionary. */
/* One region, taken whole before anything else (words_init), at the
 * bottom of the heap: the words up from its start and their letters down
 * from its end, full where they meet; and the table, a block of its own
 * beside it, freed once the pages are read. Grown a block or a table at
 * a time, as it was, its pieces all through the heap left no room big
 * enough for a topic's markup. agondev's realloc never makes a block
 * smaller, and makes one bigger by copying it. */
#define DICT_BYTES  (40 * 1024)     /* with -f, every word: about 2600 words */
#define DSLOTS      8192            /* the table: a power of two */
#define NAME_BYTES  (24 * 1024)     /* names only: agon-docs has about 1300 */
#define NAME_SLOTS  4096

static unsigned dict_bytes;         /* the region, and the table, as words_init sized them */
static unsigned dslots;
static unsigned dict_max;           /* words: past three quarters full, probing grows long */
#define DICT_STOP   0xFFFF          /* last, for a stop word: never a topic + 1 */
#define POSTING     0               /* a record that is a posting */

/* A word: eight bytes on the Agon, so that the many a document has fit,
 * and finding one by its number is a shift, not a multiply. Its number is
 * the slot's that finds it. */
typedef struct {
    uint8_t* s;             /* its letters */
    uint16_t last;          /* the last topic, as made, + 1; DICT_STOP for a stop word,
                               in the dictionary only to be found as one */
    uint16_t count;         /* its postings */
    uint8_t len;
} dword;

static uint8_t* dregion;
static dword* dict;                 /* the words, up from the region's start */
static uint8_t* dtop;               /* their letters, down from the table */
static unsigned ndict;
static int dict_full;
/* The words, by hash: each slot a word's number as two bytes -- its
 * block, and its place in it -- or DSLOT_NONE; probed by pointer, which
 * is no multiply or shift on the eZ80, and the hash's two bytes make its
 * offset byte by byte. Two bytes, where a pointer was four: the table is
 * the dictionary's biggest piece but its words' letters. */
typedef struct {
    uint8_t lo;
    uint8_t hi;
} dslot_t;

#define DSLOT_NONE 0xFF             /* hi: no word; dict_max keeps every number below */

static dslot_t* dslot;
static dslot_t* dslot_end;
static uint8_t dslot_hmask;         /* the offset's high byte's mask, on the Agon */

#define DW(k) (dict[k])

/* Pearson's hash of the bytes of a word that tell words apart most -- its
 * length, its first letter, its middle one and its last two -- each two
 * 8-bit lookups, not the whole word through md_hash: a word that shares
 * them all with another is told apart by the comparison after. Its last
 * two, not its first two: names that share a start (vdp_, ffs_) are
 * common, and with the first two they all fell together. */
#define ALWAYS_INLINE static inline __attribute__((always_inline))
#define COLD static __attribute__((noinline))  /* off word_seen's common path (zap's guide, 2a) */

ALWAYS_INLINE unsigned word_hash(const uint8_t* w, uint8_t len)
{
    const uint8_t* t = md_pearson;
    const uint8_t* mid = w + half_of[len];
    const uint8_t* last = w + len - 1;
    const uint8_t* before = last - 1;   /* w itself for a word of two: the stop words */
    uint8_t a = t[len];                 /* bytes throughout: an int's and is a call */
    uint8_t b = t[(uint8_t)(len + 1)];

    a = t[a ^ w[0]];
    b = t[b ^ w[0]];
    a = t[a ^ *before];
    b = t[b ^ *before];
    a = t[a ^ *mid];
    b = t[b ^ *mid];
    a = t[a ^ *last];
    b = t[b ^ *last];

    return (unsigned)b << 8 | a;
}

static void place(unsigned at)
{
    unsigned mask = nslots - 1;
    unsigned h = word_hash(wbuf + at + 1, wbuf[at]) & mask;

    while (slot[h]) {
        h = (h + 1) & mask;
    }
    slot[h] = (uint16_t)(at + 1);
}

static void more_slots(void)
{
    unsigned at;

    nslots *= 2;
    if (nslots > slot_cap) {
        slot_cap = nslots;
        slot = md_realloc(slot, slot_cap * (unsigned)sizeof(uint16_t));
    }
    memset(slot, 0, nslots * sizeof(uint16_t));
    for (at = 0; at < wlen; at += wbuf[at] ? 1u + wbuf[at] + 2u : 5u) {
        if (wbuf[at] != POSTING) {
            place(at);
        }
    }
}

static unsigned long text_records;  /* words gone as words, in all */

static void text_word(const char* word, int len, int topic, int in_title)
{
    const uint8_t* key = (const uint8_t*)word;
    unsigned mask = nslots - 1;
    unsigned h = word_hash(key, (uint8_t)len) & mask;
    unsigned at;

    (void)topic;
    while ((at = slot[h]) != 0) {
        uint8_t* r = wbuf + at - 1;

        if (r[0] == len && memcmp(r + 1, key, (size_t)len) == 0) {
            r[1 + len + 1] |= (uint8_t)(in_title << 7);

            return;
        }
        h = (h + 1) & mask;
    }
    if (wlen + (unsigned)len + 3 > wcap) {
        wcap = (wlen + (unsigned)len + 3) * 2;
        wbuf = md_realloc(wbuf, wcap);
    }
    at = wlen;
    wbuf[at] = (uint8_t)len;
    memcpy(wbuf + at + 1, key, (size_t)len);
    wbuf[at + 1 + len] = (uint8_t)(word_topic & 0xFF);
    wbuf[at + 2 + len] = (uint8_t)(word_topic >> 8 | (unsigned)in_title << 7);
    wlen += 1u + (unsigned)len + 2u;
    text_records++;
    slot[h] = (uint16_t)(at + 1);
    if (++nwords * 2 > nslots) {
        more_slots();
    }
}

/* The word in slot sp. */
ALWAYS_INLINE dword* slot_word(const dslot_t* sp)
{
#if defined(AGONDEV)
    static volatile union {         /* as dict_find_h's offset: no shift */
        unsigned v;
        uint8_t b[3];
    } k;

    k.b[0] = sp->lo;
    k.b[1] = sp->hi;
    k.b[2] = 0;

    return dict + k.v;
#else
    return dict + ((unsigned)sp->hi << 8 | sp->lo);
#endif
}

/* The slot for w, of hash h: the one it is in, or the empty one it would
 * go in. */
ALWAYS_INLINE dslot_t* dict_find_h(const uint8_t* w, uint8_t len, unsigned h)
{
    dslot_t* sp;
    dslot_t* end = dslot_end;

#if defined(AGONDEV)
    /* volatile, or the compiler joins the bytes back with a shift */
    static volatile union {
        unsigned v;
        uint8_t b[3];
    } off;

    off.b[0] = (uint8_t)h & 0xFE;
    off.b[1] = (uint8_t)(h >> 8) & dslot_hmask;
    off.b[2] = 0;
    sp = (dslot_t*)((uint8_t*)dslot + off.v);
#else
    sp = dslot + ((h >> 1) & (dslots - 1));
#endif
    while (sp->hi != DSLOT_NONE) {
        const dword* d = slot_word(sp);

        if (d->len == len && memcmp(d->s, w, len) == 0) {
            break;
        }
        if (++sp == end) {
            sp = dslot;
        }
    }

    return sp;
}

static dslot_t* dict_find(const uint8_t* w, uint8_t len)
{
    return dict_find_h(w, len, word_hash(w, len));
}

/* A new word into the dictionary, in the slot sp: it, or NULL if there is
 * no room for it. */
COLD dword* dict_add(const uint8_t* w, unsigned len, dslot_t* sp)
{
    unsigned k = ndict;

    if (k == dict_max || (uint8_t*)(dict + k + 1) + len > dtop) {
        return NULL;
    }
    dtop -= len;
    memcpy(dtop, w, len);
    DW(k).s = dtop;
    DW(k).len = (uint8_t)len;
    DW(k).last = 0;
    DW(k).count = 0;
    sp->lo = (uint8_t)k;
    sp->hi = (uint8_t)(k >> 8);
    ndict++;

    return &DW(k);
}

/* The dictionary begun with the stop words, so that a word's one look in
 * it says if it is one. 0, or -1. */
COLD int dict_begin(void)
{
    int k;

    dregion = malloc(dict_bytes);       /* not md_realloc: without it, words go as words */
    dslot = malloc(dslots * sizeof(dslot_t));
    if (!dregion || !dslot) {
        free(dregion);
        free(dslot);
        dregion = NULL;
        dslot = NULL;

        return -1;
    }
    dslot_end = dslot + dslots;
    dslot_hmask = (uint8_t)((dslots * sizeof(dslot_t) >> 8) - 1);
    memset(dslot, DSLOT_NONE, dslots * sizeof(dslot_t));
    dict = (dword*)dregion;
    dtop = dregion + dict_bytes;
    for (k = 0; k < NSTOP; k++) {
        const uint8_t* w = (const uint8_t*)STOP[k];
        uint8_t len = (uint8_t)strlen(STOP[k]);
        dword* d;

        if (len < 2) {
            continue;                   /* the hash reads two letters; no word here is one */
        }
        d = dict_add(w, len, dict_find(w, len));
        if (!d) {
            return -1;
        }
        d->last = DICT_STOP;
    }

    return 0;
}

/* A word of the topic: nothing if it is a stop word; a posting, if it is
 * the dictionary's -- or comes into it -- and the topic has not used it
 * yet; else as the word. */
static void word_seen(const uint8_t* w, uint8_t len, int in_title)
{
    unsigned h = word_hash(w, len);
    dslot_t* sp = NULL;
    dword* d = NULL;

    if (dslot) {
        sp = dict_find_h(w, len, h);
        if (sp->hi != DSLOT_NONE) {
            d = slot_word(sp);
        } else if (!dict_full && (d = dict_add(w, len, sp)) == NULL) {
            dict_full = 1;
        }
    }
    if (!d) {
        if (!is_stop((const char*)w, (int)len)) {
            text_word((const char*)w, (int)len, (int)word_topic, in_title);     /* as the word */
        }

        return;
    }
    if (d->last == DICT_STOP) {
        return;
    }
    if (d->last != word_topic + 1) {
        uint8_t* r;

        d->last = (uint16_t)(word_topic + 1);
        d->count++;
        if (wlen + 5 > wcap) {
            wcap = (wlen + 5) * 2;
            wbuf = md_realloc(wbuf, wcap);
        }
        r = wbuf + wlen;
        r[0] = POSTING;
        r[1] = sp->lo;                  /* its number, as bytes: no shifts here */
        r[2] = sp->hi;
        r[3] = topic_lo;
        r[4] = in_title ? topic_hi | 0x80 : topic_hi;
        wlen += 5;
    }
}

/* Not all digits, and not only _ and $: the kinds of its letters -- at
 * once for most, a word starting with a letter being both. */
ALWAYS_INLINE int real_word(const uint8_t* w, unsigned len)
{
    uint8_t kind = 0;

    if (word_kind[*w] == (NOT_DIGIT | REAL)) {
        return 1;
    }
    while (len--) {
        kind |= word_kind[*w++];
    }

    return kind == (NOT_DIGIT | REAL);
}

void words_init(void)
{
    if (!word_char['a']) {
        make_tables();
    }
    dict_bytes = md_full_text ? DICT_BYTES : NAME_BYTES;
    dslots = md_full_text ? DSLOTS : NAME_SLOTS;
    dict_max = dslots / 4 * 3;
    if (dict_begin() != 0) {
        dict_full = 1;
    }
}

void words_begin(int t)
{
    if (!word_char['a']) {
        make_tables();                  /* before words_take, which inline.c calls */
    }
    if (!slot) {
        slot_cap = FIRST_SLOTS;
        slot = md_realloc(NULL, slot_cap * (unsigned)sizeof(uint16_t));
    }
    word_topic = (unsigned)TOPIC(t).made;
    topic_lo = (uint8_t)(word_topic & 0xFF);
    topic_hi = (uint8_t)(word_topic >> 8);
    wlen = 0;
    nwords = 0;
    nslots = FIRST_SLOTS;
    memset(slot, 0, nslots * sizeof(uint16_t));
}

static void title_text_word(const char* w, int len, int topic, int in_title)
{
    (void)topic;
    word_seen((const uint8_t*)w, (uint8_t)len, in_title);
}

void words_text(const char* plain, int in_title)
{
    each_word(plain, 3, title_text_word, (int)word_topic, in_title);
}

/* The words of a topic's markup, read as it is: blocks, each a three-byte
 * head and its text to an END. The words are the ones its plain text
 * would give -- a LINK and its topic, and the other codes but a BREAK, a
 * newline and the END, are left out and what is either side joins up --
 * without making the plain text first. */
/* The scans of words_markup, each a function of its own: so that the loop
 * has its two values in registers, and not the whole of words_markup's
 * around it (zap's guide: split a frame for a hot loop's sake). */
#define NOINLINE __attribute__((noinline))

/* From s, past bytes that are in a word as they are. */
static NOINLINE const uint8_t* as_is_run(const uint8_t* s, const uint8_t* e)
{
    while (s < e && word_as_is[*s]) {
        s++;
    }

    return s;
}

/* From s, past what is between words -- a LINK with its topic -- to the
 * next word's first byte, or to its block's END, or e. */
static NOINLINE const uint8_t* between_words(const uint8_t* s, const uint8_t* e)
{
    while (s < e && !word_char[*s]) {
        if (*s == REF_END) {
            break;
        }
        s += *s == REF_LINK ? 3 : 1;
    }

    return s;
}

/* One word of markup, as inline.c writes it: taken where it lies when it
 * is as it is, len bytes from w -- lower case, nothing inside it --
 * and, from s to e, read again a letter at a time when a capital or a
 * code is inside it, as words_markup reads one. */
void words_take(const uint8_t* w, unsigned len)
{
    if (len > WORD_MAX) {
        len = WORD_MAX;
    }
    if (len >= 3 && real_word(w, len)) {
        word_seen(w, (uint8_t)len, 0);
    }
}

void words_span(const uint8_t* s, const uint8_t* e)
{
    static uint8_t w[WORD_MAX + 1];
    uint8_t* o = w;
    unsigned len;

    while (s < e) {
        uint8_t b = *s;
        uint8_t c = word_char[b];

        if (c) {
            if (o < w + WORD_MAX) {
                *o++ = c;
            }
            s++;
        } else if (b == REF_LINK) {
            s += 3;
        } else {
            s++;                            /* a code: what is either side joins up */
        }
    }
    len = (unsigned)(o - w);
    if (len >= 3 && real_word(w, len)) {
        word_seen(w, (uint8_t)len, 0);
    }
}

/* The names in a topic's markup, as inline.c takes them without -f: the
 * words inside a code span, a link's label or an emphasis, each word cut
 * where the span ends. */
void words_markup_names(const uint8_t* m, unsigned n)
{
    const uint8_t* s = m;
    const uint8_t* e = m + n;

    if (!word_char['a']) {
        make_tables();
    }
    while (s < e) {
        const uint8_t* start = NULL;
        unsigned depth = 0;

        s += 3;                             /* the block's head */
        while (s < e && *s != REF_END) {
            uint8_t b;

            if (!depth) {
                /* Outside a name -- most of a table -- only a code can
                 * matter: passed over to the next. */
                while (s < e && *s >= 0x20) {
                    s++;
                }
                if (s >= e || *s == REF_END) {
                    break;
                }
            }
            b = *s;
            if (b == REF_EM_ON || b == REF_CODE_ON || b == REF_LINK) {
                depth++;
                s += b == REF_LINK ? 3 : 1;
            } else if (b == REF_EM_OFF || b == REF_CODE_OFF || b == REF_LINK_END) {
                if (depth && !--depth && start) {
                    words_span(start, s);
                    start = NULL;
                }
                s++;
            } else if (depth && word_char[b]) {
                if (!start) {
                    start = s;
                }
                s++;
            } else {
                if (start && (b >= 0x20 || b == REF_BREAK || b == '\n')) {
                    words_span(start, s);
                    start = NULL;
                }
                s++;
            }
        }
        if (start) {
            words_span(start, s);
        }
        s++;                                /* the END */
    }
}

void words_markup(const uint8_t* m, unsigned n)
{
    static uint8_t w[WORD_MAX + 1];
    const uint8_t* s = m;
    const uint8_t* e = m + n;

    if (!word_char['a']) {
        make_tables();
    }
    while (s < e) {
        s += 3;                             /* the block's head */
        while (s < e) {
            uint8_t* o = w;
            unsigned len;

            s = between_words(s, e);
            if (s < e && *s == REF_END) {
                goto next_block;
            }
            /* Most words are the text as it is -- lower case, nothing
             * inside them -- and are taken where they are, by a loop with
             * two values to keep (bounded by e, as zap's guide says a scan
             * must be). One with a capital, or a code inside it, is read
             * again from its start, a letter at a time, into w. */
            {
                const uint8_t* start = s;

                s = as_is_run(s, e);
                if (s >= e || !(word_char[*s]           /* a capital, or a code (a LINK is one) */
                                || (*s < 0x20 && *s != REF_END && *s != REF_BREAK && *s != '\n'))) {
                    len = (unsigned)(s - start);
                    if (len > WORD_MAX) {
                        len = WORD_MAX;
                    }
                    if (len >= 3 && real_word(start, len)) {
                        word_seen(start, (uint8_t)len, 0);
                    }
                    continue;
                }
                s = start;
            }
            while (s < e) {
                uint8_t b = *s;
                uint8_t c = word_char[b];

                if (c) {
                    if (o < w + WORD_MAX) {
                        *o++ = c;
                    }
                    s++;
                } else if (b == REF_LINK) {
                    s += 3;
                } else if (b < 0x20 && b != REF_END && b != REF_BREAK && b != '\n') {
                    s++;
                } else {
                    break;
                }
            }
            len = (unsigned)(o - w);
            if (len >= 3 && real_word(w, len)) {
                word_seen(w, (uint8_t)len, 0);  /* a stop word is found as one there */
            }
        }
next_block:
        s++;                                /* the END */
    }
}

void words_end(void)
{
    words_put(wbuf, wlen);
    wlen = 0;
    nwords = 0;
}

/* --- sorting the words file ---------------------------------------------------- */

static uint8_t* run;                /* RUN bytes */
static uint8_t* sorted;             /* and they in order */

/* A run's records, to be sorted: where each is in the run. */
static uint16_t* rec;
static uint16_t* rec_tmp;
static unsigned nrec;
static unsigned rec_cap;

/* a's word after b's. */
static int rec_after(unsigned a, unsigned b)
{
    const uint8_t* p = run + a;
    const uint8_t* q = run + b;
    unsigned la = p[0];
    unsigned lb = q[0];
    int c = memcmp(p + 1, q + 1, la < lb ? la : lb);   /* the eZ80's cpi */

    return c ? c > 0 : la > lb;
}

#define RADIX_LETTERS 3

static int by_rec(const void* a, const void* b)
{
    const uint8_t* p = run + *(const uint16_t*)a;
    const uint8_t* q = run + *(const uint16_t*)b;
    int c = memcmp(p + 1, q + 1, p[0] < q[0] ? p[0] : q[0]);

    return c ? c : (int)p[0] - (int)q[0];
}

/* Two records' words the same in their first RADIX_LETTERS letters, as the
 * passes see them: a letter past a word's end is 0. */
static int same_start(const uint8_t* a, const uint8_t* b)
{
    unsigned d;

    for (d = 1; d <= RADIX_LETTERS; d++) {
        if ((a[0] >= d ? a[d] : 0) != (b[0] >= d ? b[d] : 0)) {
            return 0;
        }
    }

    return 1;
}

/* One stable counting pass on each record's letter d, 0 for a word too
 * short to have one -- as a shorter word sorts first. The counts are 16
 * bits, as indexing them is then an add, where 24 bits is a multiply. */
static void radix_pass(const uint16_t* from, uint16_t* to, unsigned d)
{
    static uint16_t pos[256];
    unsigned i;
    unsigned sum = 0;

    memset(pos, 0, sizeof pos);
    for (i = 0; i < nrec; i++) {
        const uint8_t* r = run + from[i];

        pos[r[0] >= d ? r[d] : 0]++;
    }
    for (i = 0; i < 256; i++) {
        unsigned c = pos[i];

        pos[i] = (uint16_t)sum;
        sum += c;
    }
    for (i = 0; i < nrec; i++) {
        uint16_t f = from[i];
        const uint8_t* r = run + f;

        to[pos[r[0] >= d ? r[d] : 0]++] = f;
    }
}

/* The run's records in order: by their first letters, a stable counting
 * pass each from the last back, which is linear; then each group that
 * shares them. */
static void sort_run(void)
{
    uint16_t* from = rec;
    uint16_t* to = rec_tmp;
    unsigned d;
    unsigned i;

    for (d = RADIX_LETTERS; d >= 1; d--) {
        uint16_t* t;

        radix_pass(from, to, d);
        t = from;
        from = to;
        to = t;
    }
    /* Each group that shares the first letters. */
    for (i = 0; i < nrec;) {
        const uint8_t* g = run + from[i];
        unsigned e = i + 1;

        while (e < nrec && same_start(run + from[e], g)) {
            e++;
        }
        {
            /* Inserted while that costs no more than a few moves each;
             * past that, merged, so a group is never its size squared. */
            unsigned k;
            unsigned moves = 0;

            for (k = i + 1; k < e && moves <= 4 * (e - i); k++) {
                uint16_t x = from[k];
                unsigned j = k;

                while (j > i && rec_after(from[j - 1], x)) {
                    from[j] = from[j - 1];
                    j--;
                    moves++;
                }
                from[j] = x;
            }
            if (k < e) {
                md_sort(from + i, e - i, (unsigned)sizeof(uint16_t), by_rec);
            }
        }
        i = e;
    }
    if (from != rec) {
        memcpy(rec, from, nrec * sizeof(uint16_t));
    }
}

static int key_cmp(const uint8_t* a, const uint8_t* b)
{
    unsigned la = a[0];
    unsigned lb = b[0];
    int c = memcmp(a + 1, b + 1, la < lb ? la : lb);

    return c ? c : (int)la - (int)lb;
}

typedef struct {
    unsigned long pos;      /* the next byte to read, in the runs file */
    unsigned long end;
    uint8_t buf[RUN_WIN];
    unsigned have;
    unsigned at;            /* the current record in buf */
    unsigned first;         /* its first two letters */
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

static unsigned first_of(const window* w)
{
    return (unsigned)w->buf[w->at + 1] << 8 | w->buf[w->at + 2];
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

        /* The next record, whole, into the run -- or the run out. A
         * posting is the dictionary's, and passed over. */
        if (rest == 0 || rest < (in[at] ? 1u + in[at] + 2u : 5u)) {
            memmove(in, in + at, rest);
            have = rest + words_get(in + rest, RUN_WIN - rest);
            at = 0;
            rest = have;
        }
        if (rest >= 5 && in[at] == POSTING) {
            at += 5;
            continue;
        }
        if (rest >= 3 && in[at] && rest >= 1u + in[at] + 2u && used + 1u + in[at] + 2u <= RUN) {
            unsigned size = 1u + in[at] + 2u;

            if (nrec == rec_cap) {
                rec_cap += 512;             /* not doubled: this is when memory is fullest */
                rec = md_realloc(rec, rec_cap * (unsigned)sizeof(uint16_t));
                rec_tmp = md_realloc(rec_tmp, rec_cap * (unsigned)sizeof(uint16_t));
            }
            rec[nrec++] = (uint16_t)used;
            memcpy(run + used, in + at, size);
            used += size;
            at += size;
            continue;
        }
        done = !(rest >= 3 && in[at]);
        if (nrec == 0) {
            continue;
        }
        if (nruns == MAX_RUNS) {
            md_error("md2ref", 0, "too many words: more than %u bytes of them", (unsigned)(RUN * MAX_RUNS));

            return -1;
        }
        sort_run();
        starts[nruns++] = runs_size();
        {
            unsigned o = 0;

            for (k = 0; k < nrec; k++) {      /* in order, then one write */
                unsigned size = 1u + run[rec[k]] + 2u;

                memcpy(sorted + o, run + rec[k], size);
                o += size;
            }
            runs_put(sorted, o);
        }
        nrec = 0;
        used = 0;
    }
    starts[nruns] = runs_size();

    return runs_reread() == 0 ? nruns : -1;
}

/* A word's topics are kept, once gathered, as each one's number twice
 * over, plus 1 if the word is in its title: so they sort as numbers, and
 * the varints a word's entry holds -- the step from the topic before,
 * doubled, plus the title's bit -- are a subtraction, with no shifting or
 * masking of 16 bits, each a call on the eZ80. twice_of has each topic's
 * number twice over, by its number as made. */
static uint16_t* twice_of;

/* A posting's two bytes, the topic as made and the title's bit on top, as
 * kept. */
ALWAYS_INLINE uint16_t as_kept(uint8_t lo, uint8_t hi)
{
    static volatile union {
        unsigned v;
        uint8_t b[3];
    } made;

    made.b[0] = lo;
    made.b[1] = hi & 0x7F;
    made.b[2] = 0;

    return (uint16_t)(twice_of[made.v] + (hi & 0x80 ? 1 : 0));
}

/* A kept topic, its title's bit dropped. */
ALWAYS_INLINE unsigned even(uint16_t v)
{
    return (unsigned)v - ((uint8_t)v & 1);
}

static int by_topic(const void* a, const void* b)
{
    unsigned x = *(const uint16_t*)a;
    unsigned y = *(const uint16_t*)b;

    return x < y ? -1 : x > y ? 1 : 0;
}

/* A word's topics, as numbered now, sorted, each once (in_title if any
 * was). */
static unsigned tidy(uint16_t* v, unsigned n)
{
    uint16_t* end = v + n;
    uint16_t* p;
    uint16_t* o;
    unsigned moves = 0;

    /* Nearly in order, as gathered: inserted, while that costs no more
     * than a few moves each; past that, merged, so never n squared. */
    for (p = v + 1; p < end && moves <= 4 * n; p++) {
        uint16_t x = *p;
        uint16_t* q = p;

        while (q > v && q[-1] > x) {
            *q = q[-1];
            q--;
            moves++;
        }
        *q = x;
    }
    if (p < end) {
        md_sort(v, n, (unsigned)sizeof(uint16_t), by_topic);
    }
    /* Each topic once, in its title if any of its postings was. */
    if (n == 0) {
        return 0;
    }
    for (o = v, p = v + 1; p < end; p++) {
        if (even(*p) == even(*o)) {
            if ((uint8_t)*p & 1) {
                *o = (uint16_t)(even(*o) + 1);  /* in the title, if either says so */
            }
        } else {
            *++o = *p;
        }
    }

    return (unsigned)(o - v) + 1;
}

/* What write_words writes, gathered: a word's entry is a dozen small
 * pieces, and each a write of its own was a call or three. */
static uint8_t stage[512];
static unsigned nstage;
static void (*stage_to)(const void* p, unsigned n);

static void stage_flush(void)
{
    if (nstage) {
        stage_to(stage, nstage);
        nstage = 0;
    }
}

/* Room for n more, n at most sizeof stage. */
static uint8_t* stage_room(unsigned n)
{
    if (nstage + n > sizeof stage) {
        stage_flush();
    }

    return stage + nstage;
}

/* --- writing the words ------------------------------------------------------------ */

static uint8_t prev_key[WORD_MAX + 1];
static unsigned dir_max;

/* One word, len-prefixed, and its topics: tidied, front-coded against the
 * one before, and staged. */
static void emit_entry(const uint8_t* key, uint16_t* v, unsigned n)
{
    unsigned klen = key[0];
    unsigned shared = 0;
    unsigned at_topic = 0;
    unsigned o;
    unsigned j;

    n = tidy(v, n);
    if (words_section.count % BLOCK == 0) {
        unsigned long at = words_section.len;   /* unsigned is 24 bits on the eZ80 */

        words_section.dir = grow(words_section.dir, &dir_max, words_section.dir_len + 5 + klen);
        words_section.dir[words_section.dir_len++] = (uint8_t)(at & 0xFF);
        words_section.dir[words_section.dir_len++] = (uint8_t)(at >> 8);
        words_section.dir[words_section.dir_len++] = (uint8_t)(at >> 16);
        words_section.dir[words_section.dir_len++] = (uint8_t)(at >> 24);
        words_section.dir[words_section.dir_len++] = (uint8_t)klen;
        memcpy(words_section.dir + words_section.dir_len, key + 1, klen);
        words_section.dir_len += klen;
        prev_key[0] = 0;
    }
    while (shared < 255 && shared < prev_key[0] && shared < klen && prev_key[1 + shared] == key[1 + shared]) {
        shared++;
    }
    {
        uint8_t* q = stage_room(2 + WORD_MAX + 4);

        q[0] = (uint8_t)shared;
        q[1] = (uint8_t)(klen - shared);
        memcpy(q + 2, key + 1 + shared, klen - shared);
        o = 2 + klen - shared;
        o += varint(q + o, n);
        nstage += o;
        words_section.len += o;
    }
    for (j = 0; j < n; j++) {
        unsigned kept = v[j];

        o = varint(stage_room(4), kept - at_topic);     /* (t - before) * 2 + title */
        nstage += o;
        words_section.len += o;
        at_topic = even((uint16_t)kept);
    }
    memcpy(prev_key, key, klen + 1);
    words_section.count++;
}

/* The words found as words, a word at a time from their sorted runs:
 * live holds the runs not yet done -- at most MAX_RUNS, so looking at each
 * for every word is a constant a word -- and first each one's current
 * word's first two letters, which settle most comparisons. Gathered in
 * the runs' order, a word's topics come nearly in order, as tidy wants. */
static window* ov_win;
static window** ov_live;
static unsigned ov_nlive;
static uint8_t ov_key[WORD_MAX + 1];
static uint16_t* ov_v;
static unsigned ov_n;
static unsigned ov_cap;
static int ov_have;             /* ov_key and its topics are the next word */

static void ov_next(void)
{
    static unsigned eq[MAX_RUNS];
    window* least;
    unsigned klen;
    unsigned neq = 0;
    unsigned j;
    unsigned k;

    ov_have = 0;
    ov_n = 0;
    if (ov_nlive == 0) {
        return;
    }
    least = ov_live[0];
    eq[neq++] = 0;
    for (k = 1; k < ov_nlive; k++) {
        window* w = ov_live[k];
        int c = w->first < least->first ? -1 : w->first > least->first ? 1
                : key_cmp(w->buf + w->at, least->buf + least->at);

        if (c < 0) {
            least = w;
            neq = 0;
            eq[neq++] = k;
        } else if (c == 0) {
            eq[neq++] = k;
        }
    }
    klen = least->buf[least->at];
    memcpy(ov_key, least->buf + least->at, klen + 1);
    for (j = 0; j < neq; j++) {
        window* w = ov_live[eq[j]];

        do {
            const uint8_t* p = w->buf + w->at + 1 + klen;

            if (ov_n == ov_cap) {
                ov_cap = ov_cap ? ov_cap * 2 : 64;
                ov_v = md_realloc(ov_v, ov_cap * (unsigned)sizeof(uint16_t));
            }
            ov_v[ov_n++] = as_kept(p[0], p[1]);
            w->at += 1 + klen + 2;
        } while (ready(w) && key_cmp(w->buf + w->at, ov_key) == 0);
        w->first = ready(w) ? first_of(w) : 0;
    }
    /* The runs that are done, out of live: from the back, so the indexes
     * in eq still hold -- the order of the rest kept. */
    for (j = neq; j-- > 0;) {
        if (!ready(ov_live[eq[j]])) {
            ov_nlive--;
            memmove(ov_live + eq[j], ov_live + eq[j] + 1, (ov_nlive - eq[j]) * sizeof(window*));
        }
    }
    ov_have = 1;
}

/* The words found as words that come before key (len-prefixed; NULL for
 * all that are left), written. */
static void ov_before(const uint8_t* key)
{
    while (ov_have && (!key || key_cmp(ov_key, key) < 0)) {
        emit_entry(ov_key, ov_v, ov_n);
        ov_next();
    }
}

/* A dictionary word, to sort: walked by pointer, so no multiply finds one. */
typedef struct {
    const uint8_t* s;
    uint16_t id;
    uint8_t len;
} wref;

static int wref_before(const wref* x, const wref* y)
{
    int c;

    if (x->s[0] != y->s[0]) {
        return x->s[0] < y->s[0];
    }
    c = memcmp(x->s, y->s, x->len < y->len ? x->len : y->len);

    return c ? c < 0 : x->len < y->len;
}

/* The words in order: merged, runs of 1, 2, 4 ... through tmp. */
static wref* sort_words(wref* from, wref* to, unsigned n)
{
    unsigned width;

    for (width = 1; width < n; width *= 2) {
        unsigned lo;

        for (lo = 0; lo < n; lo += 2 * width) {
            unsigned mid = lo + width < n ? lo + width : n;
            unsigned hi = mid + width < n ? mid + width : n;
            const wref* a = from + lo;
            const wref* a_end = from + mid;
            const wref* b = a_end;
            const wref* b_end = from + hi;
            wref* o = to + lo;

            while (a < a_end && b < b_end) {
                *o++ = wref_before(b, a) ? *b++ : *a++;
            }
            while (a < a_end) {
                *o++ = *a++;
            }
            while (b < b_end) {
                *o++ = *b++;
            }
        }
        {
            wref* t = from;

            from = to;
            to = t;
        }
    }

    return from;
}

/* The postings are gathered a range of words at a time, each word's in a
 * slice of a block, no slice across two, the blocks GATHER_BLOCK each and
 * GATHER_BYTES at most: blocks, as at the end the heap is in pieces, and
 * a range as wide as fits, as each is a pass over the words file. */
#define GATHER_BLOCK 4096
#define GATHER_BYTES (48 * 1024)

/* Where each word's next posting goes, by its number -- NULL for a word
 * not in the range being gathered: one look a posting, in four-byte slots
 * as a subscript of three-byte pointers is a multiply. */
typedef union {
    uint16_t* p;
    uint8_t pad[4];
} post_at;

static post_at* post_to;

/* The dictionary's postings for the words in the range being gathered,
 * each where post_to says: one pass over the words file. 0, or -1. */
static int gather(void)
{
    static uint8_t in[RUN_WIN];
    unsigned have = 0;
    unsigned at = 0;

    if (words_rewind() != 0) {
        return -1;
    }
    for (;;) {
        unsigned rest = have - at;

        if (rest == 0 || rest < (in[at] ? 1u + in[at] + 2u : 5u)) {
            memmove(in, in + at, rest);
            have = rest + words_get(in + rest, RUN_WIN - rest);
            at = 0;
            rest = have;
            if (rest == 0 || rest < (in[0] ? 1u + in[0] + 2u : 5u)) {
                return 0;
            }
        }
        if (in[at] == POSTING) {
            post_at* q = &post_to[in[at + 1] | (unsigned)in[at + 2] << 8];

            if (q->p) {
                *q->p++ = as_kept(in[at + 3], in[at + 4]);
            }
            at += 5;
        } else {
            at += 1u + in[at] + 2u;
        }
    }
}

/* The dictionary looked in no more: its table given back. */
static void dict_shrink(void)
{
    free(dslot);
    dslot = dslot_end = NULL;
}

static void free_dictionary(void)
{
    free(dregion);
    dregion = NULL;
    dict = NULL;
    dslot = dslot_end = NULL;
    ndict = 0;
}

int write_words(void (*emit)(const void* p, unsigned n))
{
    static unsigned long starts[MAX_RUNS + 1];
    static uint8_t key[WORD_MAX + 1];
    uint16_t* ids = NULL;
    uint16_t** slice = NULL;
    uint16_t** blocks = NULL;
    unsigned nranked = 0;
    unsigned r0;
    unsigned k;
    int nruns;
    int r;
    int failed = 0;

    words_section.len = 0;
    words_section.dir_len = 0;
    words_section.dir = NULL;
    words_section.count = 0;
    prev_key[0] = 0;
    {
        int t;

        twice_of = md_realloc(NULL, (unsigned)(ntopics + 1) * (unsigned)sizeof(uint16_t));
        for (t = 0; t < ntopics; t++) {
            twice_of[TOPIC(t).made] = (uint16_t)(t + t);
        }
    }
    dir_max = 0;
    stage_to = emit;
    nstage = 0;

    dict_shrink();                      /* the dictionary is looked in no more */

    /* The words found as words, into sorted runs, made ready to merge --
     * when the dictionary was full, and any were. */
    nruns = 0;
    if (text_records) {
        run = md_realloc(NULL, RUN);
        sorted = md_realloc(NULL, RUN);
        nruns = make_runs(starts);
    }
    free(sorted);
    free(run);
    run = sorted = NULL;
    free(rec);
    free(rec_tmp);
    rec = rec_tmp = NULL;
    rec_cap = 0;
    if (nruns < 0) {
        return -1;
    }
    ov_win = md_realloc(NULL, (unsigned)(nruns + 1) * (unsigned)sizeof(window));
    ov_live = md_realloc(NULL, (unsigned)(nruns + 1) * (unsigned)sizeof(window*));
    ov_nlive = 0;
    for (r = 0; r < nruns; r++) {
        window* w = ov_win + r;

        w->pos = starts[r];
        w->end = starts[r + 1];
        w->have = w->at = 0;
        if (ready(w)) {
            ov_live[ov_nlive++] = w;
            w->first = first_of(w);
        }
    }
    ov_next();

    /* The dictionary sorted, and gathered a range of words at a time,
     * each written after the words found as words that come before it. */
    if (ndict) {
        unsigned n = 0;

        wref* w = md_realloc(NULL, ndict * (unsigned)sizeof(wref));
        wref* w2 = md_realloc(NULL, ndict * (unsigned)sizeof(wref));
        wref* o = w;
        wref* sorted_w;

        ids = md_realloc(NULL, ndict * (unsigned)sizeof(uint16_t));
        post_to = md_realloc(NULL, ndict * (unsigned)sizeof(post_at));
        for (k = 0; k < ndict; k++) {
            const dword* d = &DW(k);

            post_to[k].p = NULL;            /* in no range yet; a stop word in none */
            if (d->count) {
                o->s = d->s;
                o->len = d->len;
                o->id = (uint16_t)k;
                o++;
            }
        }
        n = (unsigned)(o - w);
        sorted_w = sort_words(w, w2, n);
        for (k = 0, o = sorted_w; k < n; k++, o++) {
            ids[k] = o->id;
        }
        free(w2);
        free(w);
        nranked = n;
        slice = md_realloc(NULL, (n + 1) * (unsigned)sizeof(uint16_t*));
        blocks = md_realloc(NULL, (n + 1) * (unsigned)sizeof(uint16_t*));
    }
    for (r0 = 0; r0 < nranked && !failed;) {
        unsigned r1 = r0;
        unsigned bytes = 0;
        unsigned left = 0;              /* in the block being filled */
        uint16_t* at = NULL;
        unsigned nblocks = 0;

        while (r1 < nranked) {
            unsigned need = DW(ids[r1]).count * (unsigned)sizeof(uint16_t);

            if (need > left) {
                unsigned size = need > GATHER_BLOCK ? need : GATHER_BLOCK;

                if (r1 > r0 && bytes + size > GATHER_BYTES) {
                    break;              /* the rest in the next range */
                }
                at = blocks[nblocks++] = md_realloc(NULL, size);
                bytes += size;
                left = size;
            }
            slice[r1 - r0] = at;
            post_to[ids[r1]].p = at;
            at += DW(ids[r1]).count;
            left -= need;
            r1++;
        }
        failed = gather() != 0;
        for (k = r0; k < r1 && !failed; k++) {
            const dword* d = &DW(ids[k]);
            uint16_t* first = slice[k - r0];

            key[0] = d->len;
            memcpy(key + 1, d->s, d->len);
            ov_before(key);
            emit_entry(key, first, (unsigned)(post_to[ids[k]].p - first));
            post_to[ids[k]].p = NULL;   /* out of the range */
        }
        while (nblocks) {
            free(blocks[--nblocks]);
        }
        r0 = r1;
    }
    ov_before(NULL);
    stage_flush();
    words_section.blocks = (words_section.count + BLOCK - 1) / BLOCK;
    free(ov_live);
    free(ov_win);
    free(ov_v);
    ov_v = NULL;
    ov_cap = 0;
    free(ids);
    free(slice);
    free(blocks);
    free(post_to);
    post_to = NULL;
    free(twice_of);
    twice_of = NULL;
    free_dictionary();

    return failed ? -1 : 0;
}
