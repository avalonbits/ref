/*
 * What ref does without the screen: -p prints what was found, and -T times
 * the core in the machine, from MOS's own clock.
 */
#include <stdio.h>

#include <agon/mos.h>

#include "doc.h"
#include "ui.h"

void ui_print(const char* s)
{
    while (*s) {
        if (*s == '\n') {
            putch('\r');
        }
        putch(*s++);
    }
}

/* --- -p ------------------------------------------------------------------------- */

int batch_print_topic(unsigned num, int width)
{
    int i;

    if (doc_load(num, width) != 0) {
        ui_print("ref: that topic could not be read\n");

        return -1;
    }
    for (i = 0; i < doc_nlines; i++) {
        const ref_line* ln = &doc_lines[i];
        unsigned k;
        int x;

        for (x = 0; x < ln->x && ln->kind; x++) {
            putch(ln->note && x == ln->x - 2 ? '|' : ' ');
        }
        if (ln->kind == REF_RULE) {
            for (; x < width; x++) {
                putch('-');
            }
        } else {
            for (k = ln->start; k < ln->end; k = REF_SKIP(book.view, k)) {
                if (book.view[k] >= 0x20) {
                    putch(book.view[k]);
                }
            }
        }
        ui_print("\n");
    }

    return 0;
}

void batch_print_matches(const ref_match* m, int n)
{
    int i;

    for (i = 0; i < n; i++) {
        ui_print(m[i].key);
        ui_print("  ");
        ui_print(ui_title(m[i].topic));
        ui_print("\n");
    }
}

void batch_print_hits(const ref_hit* h, int n)
{
    int i;

    for (i = 0; i < n; i++) {
        ui_print(ui_title(h[i].topic));
        ui_print("\n");
    }
}

/* --- -T --------------------------------------------------------------------------- */

#define RUNS    10
#define MATCHES 64
#define HITS    100

static ref_match matches[MATCHES];
static ref_hit hits[HITS];

/* Centiseconds a run, to a tenth. */
static void report(const char* what, uint32_t start, const char* detail)
{
    static char line[120];
    uint32_t cs10 = (getsysvar_time() - start) * 10 / RUNS;

    sprintf(line, "%-8s %lu.%lu cs  (%s, %d runs)\n", what, (unsigned long)(cs10 / 10),
            (unsigned long)(cs10 % 10), detail, RUNS);
    ui_print(line);
}

static unsigned largest_topic(void)
{
    unsigned big = 0;
    unsigned big_len = 0;
    unsigned k;
    ref_topic tp;

    for (k = 0; k < book.ntopics; k++) {
        ref_get_topic(&book, k, &tp);
        if (tp.length > big_len) {
            big = k;
            big_len = tp.length;
        }
    }

    return big;
}

int batch_time(const char* path, const char* query)
{
    static char detail[80];
    uint32_t t;
    int i;
    int count;
    unsigned big;

    if (!query[0]) {
        query = "vdu 23 27 3";
    }
    t = getsysvar_time();
    for (i = 0; i < RUNS; i++) {
        ref_close(&book);
        if (ref_open(&book, path) != REF_OK) {
            return -1;
        }
    }
    report("open", t, "header, topics and directories");

    t = getsysvar_time();
    for (i = 0; i < RUNS; i++) {
        ref_lookup(&book, query, matches, MATCHES, &count);
    }
    report("lookup", t, query);

    t = getsysvar_time();
    for (i = 0; i < RUNS; i++) {
        ref_search(&book, "sprite", hits, HITS);
    }
    report("search", t, "sprite");

    t = getsysvar_time();
    for (i = 0; i < RUNS; i++) {
        ref_keys_from(&book, "vdu 23", matches, MATCHES);
    }
    report("keys", t, "keys from vdu 23");

    big = largest_topic();
    t = getsysvar_time();
    for (i = 0; i < RUNS; i++) {
        ref_view(&book, big);
    }
    sprintf(detail, "topic %u, the largest, read and decompressed", big);
    report("read", t, detail);

    t = getsysvar_time();
    for (i = 0; i < RUNS; i++) {
        doc_load(big, 79);
    }
    sprintf(detail, "topic %u, the largest, read and laid out", big);
    report("view", t, detail);

    return 0;
}
