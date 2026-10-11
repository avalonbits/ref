/*
 * ref's reader core: ref.dat, read with seeks.
 *
 * Portable C -- stdio, string and stdlib, nothing of the Agon's -- so it
 * builds for the Agon with agondev and with acc, and on a PC for tests.
 * The format is described in build/refbuild/refdat.py; normalising in
 * build/refbuild/normalize.py; the layout in build/refbuild/layout.py. Each
 * of those is the reference this code is tested against.
 *
 * Every buffer the core needs is allocated by ref_open, sized from the
 * file's header, and freed by ref_close. Nothing here keeps a big local:
 * agondev's frames should stay under 128 bytes.
 */
#ifndef REFCORE_H
#define REFCORE_H

#include <stdint.h>
#include <stdio.h>

#define REF_NONE 0xFFFFu

/* ref_open's results. */
#define REF_OK         0
#define REF_ENOFILE    1    /* cannot open the file */
#define REF_ENOTREF    2    /* not a ref.dat */
#define REF_EVERSION   3    /* a format this ref cannot read */
#define REF_ENOMEM     4
#define REF_EIO        5    /* short read, or a section out of bounds */

#define REF_KEY_MAX    255  /* the longest key or normalised query */
#define REF_TITLE_MAX  255

typedef struct {
    uint32_t title;         /* into the titles section */
    uint32_t text;          /* from the text section */
    uint16_t packed;        /* compressed length */
    uint16_t length;
    uint16_t parent;        /* or REF_NONE */
    uint16_t span;          /* topics after it shown with it */
} ref_topic;

typedef struct {
    uint32_t title;
    uint16_t topic;         /* or REF_NONE: a nav section */
    uint16_t child;         /* first child, or REF_NONE */
    uint16_t next;          /* next sibling, or REF_NONE */
} ref_node;

typedef struct {
    FILE* f;
    char commit[13];
    char date[11];
    uint16_t ntopics;
    uint16_t nnodes;
    uint16_t nkb;           /* key blocks */
    uint16_t nwb;           /* word blocks */
    uint32_t nkeys;
    uint32_t nwords;
    uint32_t o_nodes, o_topics, o_titles, o_text, o_keys, o_keydir;
    uint32_t o_words, o_worddir, o_norm, o_dict;
    uint16_t dict_len;
    uint16_t view_size;
    uint16_t max_packed;
    uint32_t size;

    uint8_t* topics;        /* ntopics records of 16 bytes */
    uint8_t* dict;
    uint8_t* packed;        /* max_packed bytes */
    uint8_t* view;          /* view_size bytes: a topic and what joins it */
    uint8_t* keydir;        /* the key directory, as stored */
    uint16_t* keyblk;       /* where each block's entry starts in keydir */
    uint8_t* worddir;
    uint16_t* wordblk;
    uint8_t* norm;          /* the normalising settings, as stored */
    uint8_t* scores;        /* 2 * ntopics: search counts */
    uint8_t* bits;          /* 2 * bitmap: a search word's topics */
} ref_file;

int  ref_open(ref_file* r, const char* path);
void ref_close(ref_file* r);

void ref_get_topic(const ref_file* r, unsigned num, ref_topic* t);
int  ref_get_node(ref_file* r, unsigned num, ref_node* n);

/* A title into buf, NUL-terminated; its length, or -1. */
int ref_title(ref_file* r, uint32_t off, char* buf, int size);
int ref_topic_title(ref_file* r, unsigned topic, char* buf, int size);

/* Topic num's text, and its span's after it, into r->view; the length, or
 * -1. The text stays good until the next call. */
int ref_view(ref_file* r, unsigned num);

/* LZ4 block in src into dst, matches reaching back into dict before dst;
 * the length written, or -1 if it is malformed or would pass dst_size. */
int ref_lz4(const uint8_t* src, unsigned src_len, uint8_t* dst, unsigned dst_size,
            const uint8_t* dict, unsigned dict_len);

/* --- keys ------------------------------------------------------------------- */

/* text normalised as the builder normalises keys, into out (REF_KEY_MAX + 1
 * bytes); its length. */
int ref_normalize(const ref_file* r, const char* text, char* out);

#define REF_LOOK_NONE   0
#define REF_LOOK_EXACT  1   /* the query is a key */
#define REF_LOOK_PREFIX 2   /* the longest key the query starts with */
#define REF_LOOK_LIST   3   /* keys that start with the query */

typedef struct {
    char key[REF_KEY_MAX + 1];
    uint16_t topic;
} ref_match;

/* What a query finds, as build/refbuild/keys.py's lookup: its kind, and up
 * to max matches in out (key and topic). *count is how many there were in
 * all, which may be more than max. */
int ref_lookup(ref_file* r, const char* query, ref_match* out, int max, int* count);

/* --- search ---------------------------------------------------------------- */

typedef struct {
    uint16_t topic;
    uint8_t title;          /* query words in its title */
    uint8_t words;          /* query words it has */
} ref_hit;

/* Topics for a full-text query, ranked; up to max in hits. The number found
 * in all. */
int ref_search(ref_file* r, const char* query, ref_hit* hits, int max);

/* --- layout ---------------------------------------------------------------- */

#define REF_PARA 1
#define REF_HEAD 2
#define REF_PRE  3
#define REF_RULE 4
#define REF_SPACE 0x40
#define REF_NOTE  0x80

#define REF_EM_ON    0x10
#define REF_EM_OFF   0x11
#define REF_CODE_ON  0x12
#define REF_CODE_OFF 0x13
#define REF_LINK     0x14
#define REF_LINK_END 0x15
#define REF_BREAK    0x0B
#define REF_END      0x00

/* The style in force where a line starts. */
#define REF_ST_EM   1
#define REF_ST_CODE 2

typedef struct {
    uint16_t start;         /* offsets into the text */
    uint16_t end;
    uint16_t link;          /* the link open where it starts, or REF_NONE */
    uint8_t kind;           /* REF_PARA ...; 0 a blank line */
    uint8_t x;              /* the column its text starts at */
    uint8_t note;
    uint8_t level;          /* a heading's */
    uint8_t style;          /* REF_ST_* in force at start */
} ref_line;

/* text laid out at width into lines, as build/refbuild/layout.py lays it
 * out; how many lines it takes, which may be more than max (only max are
 * stored). */
int ref_layout(const uint8_t* text, unsigned len, int width, ref_line* lines, int max);

/* The offset after the item at k: a link's start is three bytes. */
#define REF_SKIP(t, k) ((t)[k] == REF_LINK ? (k) + 3 : (k) + 1)

#endif
