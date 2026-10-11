/*
 * What the core's files share, and nobody else needs.
 */
#ifndef REFINT_H
#define REFINT_H

#include <stdint.h>

#include "refcore.h"

/* Keys and words a block: refdat.py's KEY_BLOCK and WORD_BLOCK. */
#define REF_BLOCK 64

uint16_t ref_rd16(const uint8_t* p);
uint32_t ref_rd32(const uint8_t* p);
int ref_read_at(ref_file* r, uint32_t off, void* buf, unsigned n);

/* A byte at a time through a buffer: how the key and word blocks are read. */
typedef struct {
    ref_file* r;
    uint32_t pos;           /* file offset of buf[0] */
    unsigned have;
    unsigned at;
    int bad;                /* set on a read past the end, or an error */
    uint8_t buf[256];
} ref_reader;

void ref_reader_seek(ref_reader* rd, ref_file* r, uint32_t off);
unsigned ref_reader_byte(ref_reader* rd);
uint32_t ref_reader_varint(ref_reader* rd);

/* A front-coded entry, as keys and words are stored: the key into key (its
 * length returned), from the one before it, which key still holds. */
int ref_reader_entry(ref_reader* rd, char* key, int prev_len);

/* The block a front-coded section should be scanned from to find q: the
 * last whose first key is below q (0 if none). */
unsigned ref_find_block(const uint8_t* dir, const uint16_t* at, unsigned blocks, const char* q,
                        unsigned qlen);

int ref_keycmp(const char* a, unsigned alen, const char* b, unsigned blen);

/* Is w (len bytes) a stop word, by the list in ref.dat? */
int ref_stop_word(const ref_file* r, const char* w, unsigned len);

#endif
