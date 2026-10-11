/*
 * Markup laid out into screen lines, as build/refbuild/layout.py lays it
 * out: its docstring is the specification, and the host tests compare the
 * two line for line. Each line also records the style and link in force
 * where it starts, so the viewer can draw any line on its own.
 */
#include "refcore.h"

#define SP 0x20

typedef struct {
    ref_line* lines;
    int max;
    int n;
    uint8_t style;
    uint16_t link;
} state;

static state st;

static void emit(const uint8_t* t, unsigned kind, unsigned x, unsigned start, unsigned end,
                 unsigned note, unsigned level)
{
    unsigned k;

    if (st.n < st.max) {
        ref_line* ln = st.lines + st.n;

        ln->kind = (uint8_t)kind;
        ln->x = (uint8_t)(x > 255 ? 255 : x);
        ln->start = (uint16_t)start;
        ln->end = (uint16_t)end;
        ln->note = (uint8_t)note;
        ln->level = (uint8_t)level;
        ln->style = st.style;
        ln->link = st.link;
    }
    st.n++;

    /* The style the next line starts in: this one's, run to its end. */
    for (k = start; k < end; k = REF_SKIP(t, k)) {
        switch (t[k]) {
        case REF_EM_ON:
            st.style |= REF_ST_EM;
            break;
        case REF_EM_OFF:
            st.style &= (uint8_t)~REF_ST_EM;
            break;
        case REF_CODE_ON:
            st.style |= REF_ST_CODE;
            break;
        case REF_CODE_OFF:
            st.style &= (uint8_t)~REF_ST_CODE;
            break;
        case REF_LINK:
            st.link = (uint16_t)(t[k + 1] | t[k + 2] << 8);
            break;
        case REF_LINK_END:
            st.link = REF_NONE;
            break;
        }
    }
}

static void wrap(const uint8_t* t, unsigned s, unsigned e, unsigned indent, unsigned hang,
                 int width, unsigned kind, unsigned note, unsigned level)
{
    unsigned x = indent;
    unsigned j = s;

    for (;;) {
        unsigned start;
        int room;
        int col = 0;
        unsigned brk = 0;
        unsigned nxt = 0;
        int have_brk = 0;

        while (j < e && t[j] == SP) {
            j++;
        }
        start = j;
        room = width - (int)x;
        if (room < 1) {
            room = 1;
        }
        while (j < e) {
            unsigned c = t[j];

            if (c == REF_BREAK) {
                break;
            }
            if (c == SP) {
                brk = j;
                while (j < e && t[j] == SP) {
                    j++;
                }
                nxt = j;
                have_brk = 1;
                col++;
                continue;
            }
            if (c < 0x20) {
                j = REF_SKIP(t, j);
                continue;
            }
            if (col >= room) {
                break;
            }
            col++;
            j++;
        }
        if (j < e && t[j] == REF_BREAK) {
            emit(t, kind, x, start, j, note, level);
            j++;
        } else if (j >= e) {
            emit(t, kind, x, start, j, note, level);
            break;
        } else if (have_brk) {
            emit(t, kind, x, start, brk, note, level);
            j = nxt;
        } else {
            emit(t, kind, x, start, j, note, level);    /* a word too wide: cut */
        }
        x = hang;
    }
}

int ref_layout(const uint8_t* t, unsigned len, int width, ref_line* lines, int max)
{
    unsigned k = 0;

    st.lines = lines;
    st.max = max;
    st.n = 0;
    st.style = 0;
    st.link = REF_NONE;
    while (k + 3 <= len) {
        unsigned flags = t[k];
        unsigned kind = flags & 0x0f;
        unsigned a = t[k + 1];
        unsigned b = t[k + 2];
        unsigned note = (flags & REF_NOTE) ? 1 : 0;
        unsigned body = k + 3;
        unsigned e = body;

        while (e < len && t[e] != REF_END) {
            e = REF_SKIP(t, e);
        }
        if (e > len) {
            break;                          /* a link cut off: malformed */
        }
        /* No reset of the style here: the builder closes every style and
         * link inside the block that opens it. */
        if ((flags & REF_SPACE) && st.n > 0) {
            emit(t, 0, 0, body, body, 0, 0);
        }
        if (kind == REF_PRE) {
            unsigned s = body;
            unsigned j = body;

            while (j < e) {
                if (t[j] == '\n') {
                    emit(t, REF_PRE, a, s, j, note, 0);
                    s = j + 1;
                    j++;
                } else {
                    j = REF_SKIP(t, j);
                }
            }
            if (s < e) {
                emit(t, REF_PRE, a, s, e, note, 0);
            }
        } else if (kind == REF_RULE) {
            emit(t, REF_RULE, a, body, body, note, 0);
        } else if (kind == REF_HEAD) {
            wrap(t, body, e, 0, 0, width, REF_HEAD, note, a);
        } else {
            wrap(t, body, e, a, b, width, kind, note, 0);
        }
        k = e + 1;
    }

    return st.n;
}
