/*
 * A stable sort, n log n at worst: merging runs of 1, 2, 4... into a
 * scratch copy and back. md2ref sorts with it where qsort's quicksort, or
 * an insertion, could take n squared.
 */
#include <stdlib.h>
#include <string.h>

#include "md2ref.h"

/* md_sort for 16-bit items, which md2ref sorts most: moved as they are,
 * not a memcpy call each, and indexed with no multiply. */
static void sort16(uint16_t* base, unsigned n, int (*cmp)(const void* a, const void* b))
{
    uint16_t* from = base;
    uint16_t* to = md_realloc(NULL, n * (unsigned)sizeof(uint16_t));
    unsigned width;

    for (width = 1; width < n; width *= 2) {
        unsigned lo;

        for (lo = 0; lo < n; lo += 2 * width) {
            unsigned mid = lo + width < n ? lo + width : n;
            unsigned hi = mid + width < n ? mid + width : n;
            uint16_t* a = from + lo;
            uint16_t* a_end = from + mid;
            uint16_t* b = a_end;
            uint16_t* b_end = from + hi;
            uint16_t* o = to + lo;

            while (a < a_end && b < b_end) {
                *o++ = cmp(b, a) < 0 ? *b++ : *a++;
            }
            while (a < a_end) {
                *o++ = *a++;
            }
            while (b < b_end) {
                *o++ = *b++;
            }
        }
        {
            uint16_t* t = from;

            from = to;
            to = t;
        }
    }
    if (from != base) {
        memcpy(base, from, n * sizeof(uint16_t));
        free(from);
    } else {
        free(to);
    }
}

void md_sort(void* base, unsigned n, unsigned size, int (*cmp)(const void* a, const void* b))
{
    if (size == sizeof(uint16_t) && n >= 2) {
        sort16(base, n, cmp);

        return;
    }
    uint8_t* from = base;
    uint8_t* to;
    unsigned width;

    if (n < 2) {
        return;
    }
    to = md_realloc(NULL, n * size);
    for (width = 1; width < n; width *= 2) {
        unsigned lo;

        for (lo = 0; lo < n; lo += 2 * width) {
            unsigned mid = lo + width < n ? lo + width : n;
            unsigned hi = mid + width < n ? mid + width : n;
            uint8_t* a = from + lo * size;
            uint8_t* a_end = from + mid * size;
            uint8_t* b = a_end;
            uint8_t* b_end = from + hi * size;
            uint8_t* o = to + lo * size;

            while (a < a_end && b < b_end) {
                if (cmp(b, a) < 0) {            /* only a later one that is less goes first */
                    memcpy(o, b, size);
                    b += size;
                } else {
                    memcpy(o, a, size);
                    a += size;
                }
                o += size;
            }
            memcpy(o, a, (size_t)(a_end - a));
            o += a_end - a;
            memcpy(o, b, (size_t)(b_end - b));
        }
        {
            uint8_t* t = from;

            from = to;
            to = t;
        }
    }
    if (from != base) {
        memcpy(base, from, n * size);
        free(from);
    } else {
        free(to);
    }
}
