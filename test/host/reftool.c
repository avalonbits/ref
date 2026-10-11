/*
 * The reader core on the PC, for the tests: each mode prints what the core
 * makes of ref.dat, for build/tests/test_core.py to hold against the
 * builder's own answers.
 *
 *   reftool <ref.dat> info              counts and sizes
 *   reftool <ref.dat> norm              each line of stdin, normalised
 *   reftool <ref.dat> lookup            each line of stdin, looked up
 *   reftool <ref.dat> search            each line of stdin, searched
 *   reftool <ref.dat> views             every topic's view: length, bytes
 *   reftool <ref.dat> layout <width>    every topic's view, laid out
 *   reftool <ref.dat> contents          the contents tree, depth first
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/core/refcore.h"

#define MAX_MATCH 64
#define MAX_HITS  100
#define MAX_LINES 4000

static ref_file r;
static ref_match matches[MAX_MATCH];
static ref_hit hits[MAX_HITS];
static ref_line lines[MAX_LINES];
static char line[1024];
static char out[REF_KEY_MAX + 1];
static char title[REF_TITLE_MAX + 1];

static int read_line(void)
{
    size_t n;

    if (!fgets(line, sizeof line, stdin)) {
        return 0;
    }
    n = strlen(line);
    while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) {
        line[--n] = 0;
    }

    return 1;
}

static void contents(unsigned node, int depth)
{
    ref_node n;

    while (node != REF_NONE) {
        if (ref_get_node(&r, node, &n) != 0) {
            printf("bad node %u\n", node);
            exit(1);
        }
        ref_title(&r, n.title, title, sizeof title);
        printf("%*s%s", depth * 2, "", title);
        if (n.topic != REF_NONE) {
            printf("  [%u]", n.topic);
        }
        printf("\n");
        if (n.child != REF_NONE) {
            contents(n.child, depth + 1);
        }
        node = n.next;
    }
}

/* A line as a screen shows it: layout.py's text(). */
static void show(const uint8_t* t, const ref_line* ln, int width)
{
    int x;
    unsigned k;

    if (ln->kind == 0) {
        printf("\n");

        return;
    }
    for (x = 0; x < ln->x; x++) {
        putchar(ln->note && x == ln->x - 2 ? '|' : ' ');
    }
    if (ln->kind == REF_RULE) {
        for (x = ln->x; x < width; x++) {
            putchar('-');
        }
        printf("\n");

        return;
    }
    {
        /* Visible characters, then trailing spaces trimmed. */
        static char buf[4096];
        int n = 0;

        for (k = ln->start; k < ln->end; k = REF_SKIP(t, k)) {
            if (t[k] >= 0x20 && n < (int)sizeof buf) {
                buf[n++] = (char)t[k];
            }
        }
        while (n && buf[n - 1] == ' ') {
            n--;
        }
        fwrite(buf, 1, (size_t)n, stdout);
    }
    printf("\n");
}

int main(int argc, char** argv)
{
    int e;
    unsigned k;

    if (argc < 3) {
        fprintf(stderr, "usage: reftool <ref.dat> <mode>\n");

        return 2;
    }
    e = ref_open(&r, argv[1]);
    if (e != REF_OK) {
        fprintf(stderr, "reftool: cannot open %s: %d\n", argv[1], e);

        return 1;
    }
    if (strcmp(argv[2], "info") == 0) {
        printf("commit %s\ndate %s\ntopics %u\nnodes %u\nkeys %lu\nwords %lu\nview %u\n",
               r.commit, r.date, r.ntopics, r.nnodes, (unsigned long)r.nkeys,
               (unsigned long)r.nwords, r.view_size);
    } else if (strcmp(argv[2], "norm") == 0) {
        while (read_line()) {
            ref_normalize(&r, line, out);
            printf("%s\n", out);
        }
    } else if (strcmp(argv[2], "lookup") == 0) {
        while (read_line()) {
            int count;
            int kind = ref_lookup(&r, line, matches, MAX_MATCH, &count);
            int n = count < MAX_MATCH ? count : MAX_MATCH;
            int i;

            printf("%d %d", kind, count);
            for (i = 0; i < n; i++) {
                printf(" %u", matches[i].topic);
            }
            printf(" |");
            for (i = 0; i < n; i++) {
                printf(" %s;", matches[i].key);
            }
            printf("\n");
        }
    } else if (strcmp(argv[2], "search") == 0) {
        while (read_line()) {
            int n = ref_search(&r, line, hits, MAX_HITS);
            int i;

            printf("%d", n);
            for (i = 0; i < n && i < MAX_HITS; i++) {
                printf(" %u:%u:%u", hits[i].topic, hits[i].title, hits[i].words);
            }
            printf("\n");
        }
    } else if (strcmp(argv[2], "views") == 0) {
        for (k = 0; k < r.ntopics; k++) {
            int n = ref_view(&r, k);

            if (n < 0) {
                fprintf(stderr, "reftool: topic %u will not decompress\n", k);

                return 1;
            }
            putchar(n & 0xff);
            putchar((n >> 8) & 0xff);
            fwrite(r.view, 1, (size_t)n, stdout);
        }
    } else if (strcmp(argv[2], "layout") == 0 && argc > 3) {
        int width = atoi(argv[3]);

        for (k = 0; k < r.ntopics; k++) {
            int n = ref_view(&r, k);
            int nl;
            int i;

            if (n < 0) {
                return 1;
            }
            nl = ref_layout(r.view, (unsigned)n, width, lines, MAX_LINES);
            printf("@@ %u %d\n", k, nl);
            for (i = 0; i < nl && i < MAX_LINES; i++) {
                show(r.view, &lines[i], width);
            }
            /* Then the style and link each line starts in. */
            for (i = 0; i < nl && i < MAX_LINES; i++) {
                printf("%u:%u%c", lines[i].style, lines[i].link, i + 1 < nl ? ' ' : '\n');
            }
            if (nl == 0) {
                printf("\n");
            }
        }
    } else if (strcmp(argv[2], "contents") == 0) {
        contents(0, 0);
    } else {
        fprintf(stderr, "reftool: no mode %s\n", argv[2]);

        return 2;
    }
    ref_close(&r);

    return 0;
}
