/*
 * Markup laid out into screen lines, as build/refbuild/layout.py lays it
 * out: its docstring is the specification, and the host tests compare the
 * two line for line. Each line also records the style and link in force
 * where it starts, so the viewer can draw any line on its own.
 *
 * One pass over the text: the style is followed as the scan goes, and noted
 * at each space, the place a line may end, so the next line starts with
 * what was in force there. Runs of plain text are crossed by two small leaf
 * functions, which the compiler gives a register; on the eZ80 that, and the
 * single pass, are what make the largest topic quick to show.
 */
#include "refcore.h"

#define SP 0x20

static ref_line* out;
static int out_max;
static int n;
static const uint8_t* base;

static void emit(unsigned kind, unsigned x, const uint8_t* start, const uint8_t* end,
                 unsigned note, unsigned level, unsigned style, unsigned link)
{
    if (n < out_max) {
        ref_line* ln = out + n;

        ln->kind = (uint8_t)kind;
        ln->x = (uint8_t)(x > 255 ? 255 : x);
        ln->start = (uint16_t)(start - base);
        ln->end = (uint16_t)(end - base);
        ln->note = (uint8_t)note;
        ln->level = (uint8_t)level;
        ln->style = (uint8_t)style;
        ln->link = (uint16_t)link;
    }
    n++;
}

/* Follow control byte *p: style and link updated in place. */
#define FOLLOW(p, style, link)                                   \
    switch (*(p)) {                                              \
    case REF_EM_ON:                                              \
        (style) |= REF_ST_EM;                                    \
        break;                                                   \
    case REF_EM_OFF:                                             \
        (style) &= ~(unsigned)REF_ST_EM;                         \
        break;                                                   \
    case REF_CODE_ON:                                            \
        (style) |= REF_ST_CODE;                                  \
        break;                                                   \
    case REF_CODE_OFF:                                           \
        (style) &= ~(unsigned)REF_ST_CODE;                       \
        break;                                                   \
    case REF_LINK:                                               \
        (link) = (unsigned)((p)[1] | (p)[2] << 8);               \
        break;                                                   \
    case REF_LINK_END:                                           \
        (link) = REF_NONE;                                       \
        break;                                                   \
    }

/* The first byte from p below a space: a control, a newline or END. A
 * leaf on its own, so the compiler keeps p in a register. */
static const uint8_t* text_end(const uint8_t* p)
{
    while (*p >= SP) {
        p++;
    }

    return p;
}

/* The first byte from p that is a space or below: where a word ends. */
static const uint8_t* word_end(const uint8_t* p)
{
    while (*p > SP) {
        p++;
    }

    return p;
}

static void wrap(const uint8_t* s, const uint8_t* e, unsigned indent, unsigned hang, int width,
                 unsigned kind, unsigned note, unsigned level)
{
    unsigned x = indent;
    const uint8_t* j = s;
    unsigned style = 0;
    unsigned link = REF_NONE;

    for (;;) {
        const uint8_t* start;
        const uint8_t* brk = 0;
        const uint8_t* nxt = 0;
        unsigned brk_style = 0;
        unsigned brk_link = REF_NONE;
        unsigned start_style;
        unsigned start_link;
        int room;
        int col = 0;

        while (j < e && *j == SP) {
            j++;
        }
        start = j;
        start_style = style;
        start_link = link;
        room = width - (int)x;
        if (room < 1) {
            room = 1;
        }
        while (j < e) {
            unsigned c = *j;

            if (c > SP) {
                /* A run of visible characters, as far as it fits. */
                const uint8_t* q = word_end(j);
                int run = (int)(q - j);

                if (col + run > room) {
                    j += room > col ? room - col : 0;
                    break;
                }
                col += run;
                j = q;
                continue;
            }
            if (c == SP) {
                brk = j;
                brk_style = style;
                brk_link = link;
                while (j < e && *j == SP) {
                    j++;
                }
                nxt = j;
                col++;
                continue;
            }
            if (c == REF_BREAK) {
                break;
            }
            FOLLOW(j, style, link);
            j += c == REF_LINK ? 3 : 1;
        }
        if (j < e && *j == REF_BREAK) {
            emit(kind, x, start, j, note, level, start_style, start_link);
            j++;
        } else if (j >= e) {
            emit(kind, x, start, j, note, level, start_style, start_link);
            break;
        } else if (brk) {
            emit(kind, x, start, brk, note, level, start_style, start_link);
            j = nxt;
            style = brk_style;              /* spaces change nothing */
            link = brk_link;
        } else {
            emit(kind, x, start, j, note, level, start_style, start_link);  /* cut */
        }
        x = hang;
    }
}

int ref_layout(const uint8_t* t, unsigned len, int width, ref_line* lines, int max)
{
    const uint8_t* p = t;
    const uint8_t* end = t + len;

    out = lines;
    out_max = max;
    n = 0;
    base = t;
    /* Every scan stops at END at the latest: the text must end with one. */
    if (len == 0 || t[len - 1] != REF_END) {
        return 0;
    }
    while (p + 3 <= end) {
        unsigned flags = p[0];
        unsigned kind = flags & 0x0f;
        unsigned a = p[1];
        unsigned b = p[2];
        unsigned note = (flags & REF_NOTE) ? 1 : 0;
        const uint8_t* body = p + 3;
        const uint8_t* e = body;

        for (;;) {
            e = text_end(e);
            if (*e == REF_END) {
                break;
            }
            e += *e == REF_LINK ? 3 : 1;
            if (e >= end) {
                return n;                   /* a link cut off: malformed */
            }
        }
        /* Every block starts with no style: the builder closes each style
         * inside the block that opens it. */
        if ((flags & REF_SPACE) && n > 0) {
            emit(0, 0, body, body, 0, 0, 0, REF_NONE);
        }
        if (kind == REF_PRE) {
            const uint8_t* s = body;
            const uint8_t* j = body;
            unsigned style = 0;
            unsigned link = REF_NONE;
            unsigned s_style = 0;
            unsigned s_link = REF_NONE;

            while (j < e) {
                unsigned c = *j;

                if (c >= SP) {
                    j = text_end(j);
                } else if (c == '\n') {
                    emit(REF_PRE, a, s, j, note, 0, s_style, s_link);
                    s = ++j;
                    s_style = style;
                    s_link = link;
                } else {
                    FOLLOW(j, style, link);
                    j += c == REF_LINK ? 3 : 1;
                }
            }
            if (s < e) {
                emit(REF_PRE, a, s, e, note, 0, s_style, s_link);
            }
        } else if (kind == REF_RULE) {
            emit(REF_RULE, a, body, body, note, 0, 0, REF_NONE);
        } else if (kind == REF_HEAD) {
            wrap(body, e, 0, 0, width, REF_HEAD, note, a);
        } else {
            wrap(body, e, a, b, width, kind, note, 0);
        }
        p = e + 1;
    }

    return n;
}
