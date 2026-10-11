/*
 * ref.dat: opening it, its topics, titles and contents, and a topic's text.
 */
#include "refcore.h"

#include <stdlib.h>
#include <string.h>

#include "refint.h"

static const char MAGIC[8] = { 'A', 'G', 'O', 'N', 'R', 'E', 'F', 0x1a };

uint16_t ref_rd16(const uint8_t* p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

uint32_t ref_rd32(const uint8_t* p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

int ref_read_at(ref_file* r, uint32_t off, void* buf, unsigned n)
{
    if (fseek(r->f, (long)off, SEEK_SET) != 0) {
        return -1;
    }
    if (fread(buf, 1, n, r->f) != n) {
        return -1;
    }

    return 0;
}

/* A section read whole into memory: its bytes, or NULL. */
static uint8_t* load(ref_file* r, uint32_t off, unsigned n)
{
    uint8_t* p = malloc(n ? n : 1);

    if (p && n && ref_read_at(r, off, p, n) != 0) {
        free(p);

        return NULL;
    }

    return p;
}

/* The start of each of a directory's blocks, so a block's first key can be
 * found without walking the directory. */
static uint16_t* index_dir(const uint8_t* dir, unsigned len, unsigned blocks)
{
    uint16_t* at = malloc((blocks ? blocks : 1) * sizeof(uint16_t));
    unsigned k = 0;
    unsigned b;

    if (!at) {
        return NULL;
    }
    for (b = 0; b < blocks; b++) {
        if (k + 5 > len || k + 5 + dir[k + 4] > len) {
            free(at);

            return NULL;
        }
        at[b] = (uint16_t)k;
        k += 5 + dir[k + 4];
    }

    return at;
}

int ref_open(ref_file* r, const char* path)
{
    static uint8_t h[128];
    unsigned bitmap;

    memset(r, 0, sizeof *r);
    r->f = fopen(path, "rb");
    if (!r->f) {
        return REF_ENOFILE;
    }
    if (fread(h, 1, sizeof h, r->f) != sizeof h || memcmp(h, MAGIC, 8) != 0) {
        ref_close(r);

        return REF_ENOTREF;
    }
    if (ref_rd16(h + 8) != 1) {
        ref_close(r);

        return REF_EVERSION;
    }
    memcpy(r->commit, h + 12, 12);
    r->commit[12] = 0;
    memcpy(r->date, h + 24, 10);
    r->date[10] = 0;
    r->ntopics = ref_rd16(h + 36);
    r->nnodes = ref_rd16(h + 38);
    r->nkb = ref_rd16(h + 40);
    r->nwb = ref_rd16(h + 42);
    r->nkeys = ref_rd32(h + 44);
    r->nwords = ref_rd32(h + 48);
    r->o_nodes = ref_rd32(h + 52);
    r->o_topics = ref_rd32(h + 56);
    r->o_titles = ref_rd32(h + 60);
    r->o_text = ref_rd32(h + 64);
    r->o_keys = ref_rd32(h + 68);
    r->o_keydir = ref_rd32(h + 72);
    r->o_words = ref_rd32(h + 76);
    r->o_worddir = ref_rd32(h + 80);
    r->o_norm = ref_rd32(h + 84);
    r->o_dict = ref_rd32(h + 88);
    r->dict_len = ref_rd16(h + 92);
    r->view_size = ref_rd16(h + 94);
    r->max_packed = ref_rd16(h + 96);
    r->size = ref_rd32(h + 100);

    /* The sections are in this order, each ending where the next starts. */
    if (r->o_nodes < 128 || r->o_topics < r->o_nodes || r->o_titles < r->o_topics
        || r->o_text < r->o_titles || r->o_keys < r->o_text || r->o_keydir < r->o_keys
        || r->o_words < r->o_keydir || r->o_worddir < r->o_words || r->o_norm < r->o_worddir
        || r->o_dict < r->o_norm || r->o_dict + r->dict_len != r->size
        || r->o_titles - r->o_topics != 16u * r->ntopics
        || r->o_topics - r->o_nodes != 10u * r->nnodes) {
        ref_close(r);

        return REF_EIO;
    }

    bitmap = (r->ntopics + 7u) / 8u;
    r->topics = load(r, r->o_topics, 16u * r->ntopics);
    r->dict = load(r, r->o_dict, r->dict_len);
    r->keydir = load(r, r->o_keydir, (unsigned)(r->o_words - r->o_keydir));
    r->worddir = load(r, r->o_worddir, (unsigned)(r->o_norm - r->o_worddir));
    r->norm = load(r, r->o_norm, (unsigned)(r->o_dict - r->o_norm));
    r->packed = malloc(r->max_packed ? r->max_packed : 1);
    r->view = malloc(r->view_size ? r->view_size : 1);
    r->scores = malloc(2u * r->ntopics + 1);
    r->bits = malloc(2u * bitmap + 1);
    if (!r->topics || !r->dict || !r->keydir || !r->worddir || !r->norm || !r->packed
        || !r->view || !r->scores || !r->bits) {
        ref_close(r);

        return REF_ENOMEM;
    }
    r->keyblk = index_dir(r->keydir, (unsigned)(r->o_words - r->o_keydir), r->nkb);
    r->wordblk = index_dir(r->worddir, (unsigned)(r->o_norm - r->o_worddir), r->nwb);
    if (!r->keyblk || !r->wordblk) {
        ref_close(r);

        return REF_EIO;
    }

    return REF_OK;
}

void ref_close(ref_file* r)
{
    if (r->f) {
        fclose(r->f);
    }
    free(r->topics);
    free(r->dict);
    free(r->keydir);
    free(r->keyblk);
    free(r->worddir);
    free(r->wordblk);
    free(r->norm);
    free(r->packed);
    free(r->view);
    free(r->scores);
    free(r->bits);
    memset(r, 0, sizeof *r);
}

void ref_get_topic(const ref_file* r, unsigned num, ref_topic* t)
{
    const uint8_t* p = r->topics + 16u * num;

    t->title = ref_rd32(p);
    t->text = ref_rd32(p + 4);
    t->packed = ref_rd16(p + 8);
    t->length = ref_rd16(p + 10);
    t->parent = ref_rd16(p + 12);
    t->span = ref_rd16(p + 14);
}

int ref_get_node(ref_file* r, unsigned num, ref_node* n)
{
    uint8_t b[10];

    if (num >= r->nnodes || ref_read_at(r, r->o_nodes + 10u * num, b, sizeof b) != 0) {
        return -1;
    }
    n->title = ref_rd32(b);
    n->topic = ref_rd16(b + 4);
    n->child = ref_rd16(b + 6);
    n->next = ref_rd16(b + 8);

    return 0;
}

int ref_title(ref_file* r, uint32_t off, char* buf, int size)
{
    uint8_t len;
    int n;

    if (size < 1 || ref_read_at(r, r->o_titles + off, &len, 1) != 0) {
        return -1;
    }
    n = len < size - 1 ? len : size - 1;
    if (n > 0 && fread(buf, 1, (unsigned)n, r->f) != (unsigned)n) {
        return -1;
    }
    buf[n] = 0;

    return n;
}

int ref_topic_title(ref_file* r, unsigned topic, char* buf, int size)
{
    ref_topic t;

    if (topic >= r->ntopics) {
        return -1;
    }
    ref_get_topic(r, topic, &t);

    return ref_title(r, t.title, buf, size);
}

int ref_view(ref_file* r, unsigned num)
{
    ref_topic t;
    unsigned out = 0;
    unsigned k;
    unsigned last;
    int n;

    if (num >= r->ntopics) {
        return -1;
    }
    ref_get_topic(r, num, &t);
    last = num + t.span;
    if (last >= r->ntopics) {
        return -1;
    }
    for (k = num; k <= last; k++) {
        ref_get_topic(r, k, &t);
        if (t.packed > r->max_packed
            || ref_read_at(r, r->o_text + t.text, r->packed, t.packed) != 0) {
            return -1;
        }
        n = ref_lz4(r->packed, t.packed, r->view + out, r->view_size - out, r->dict,
                    r->dict_len);
        if (n < 0 || (unsigned)n != t.length) {
            return -1;
        }
        out += (unsigned)n;
    }

    return (int)out;
}

int ref_lz4(const uint8_t* src, unsigned src_len, uint8_t* dst, unsigned dst_size,
            const uint8_t* dict, unsigned dict_len)
{
    const uint8_t* ip = src;
    const uint8_t* iend = src + src_len;
    uint8_t* op = dst;
    unsigned room = dst_size;

    while (ip < iend) {
        unsigned tok = *ip++;
        unsigned n = tok >> 4;
        unsigned off;
        unsigned back;
        unsigned b;

        if (n == 15) {
            do {
                if (ip >= iend) {
                    return -1;
                }
                b = *ip++;
                n += b;
            } while (b == 255);
        }
        if (n > (unsigned)(iend - ip) || n > room) {
            return -1;
        }
        memcpy(op, ip, n);
        op += n;
        ip += n;
        room -= n;
        if (ip >= iend) {
            break;
        }
        if (iend - ip < 2) {
            return -1;
        }
        off = ip[0] | (unsigned)ip[1] << 8;
        ip += 2;
        n = tok & 15;
        if (n == 15) {
            do {
                if (ip >= iend) {
                    return -1;
                }
                b = *ip++;
                n += b;
            } while (b == 255);
        }
        n += 4;
        if (off == 0 || n > room) {
            return -1;
        }
        room -= n;
        back = (unsigned)(op - dst);
        if (off > back) {
            /* Into the dictionary, which ends where the output starts. */
            unsigned d = off - back;
            unsigned c = d < n ? d : n;

            if (d > dict_len) {
                return -1;
            }
            memcpy(op, dict + dict_len - d, c);
            op += c;
            n -= c;
        }
        if (n) {
            const uint8_t* s = op - off;

            if (off >= n) {
                memcpy(op, s, n);
                op += n;
            } else {
                while (n--) {
                    *op++ = *s++;           /* overlapping: a run */
                }
            }
        }
    }

    return (int)(op - dst);
}
