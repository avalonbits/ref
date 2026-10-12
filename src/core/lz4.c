/*
 * LZ4 blocks, decoded: a topic's text. On its own so md2ref, which keeps
 * topics compressed while it works, can use it without the rest.
 */
#include "refcore.h"

#include <string.h>

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
