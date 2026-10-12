/*
 * The reader core on the PC, for the tests: each mode prints what the core
 * makes of ref.dat, for build/tests/test_core.py to hold against the
 * builder's own answers.
 *
 *   reftool <document> info              counts and sizes
 *   reftool <document> norm              each line of stdin, normalised
 *   reftool <document> lookup            each line of stdin, looked up
 *   reftool <document> from              each line of stdin: the keys it starts
 *   reftool <document> search            each line of stdin, searched
 *   reftool <document> views             every topic's view: length, bytes
 *   reftool <document> layout <width>    every topic's view, laid out
 *   reftool <document> contents          the contents tree, depth first
 *
 * -i <file> and -o <file>, after the mode, read and write files instead of
 * stdin and stdout: the same program, built for the Agon by agondev and by
 * acc, runs there (test/core.sh) and must answer as the PC build does.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "refcore.h"

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

static FILE* in;
static FILE* out_f;

static void fputc_out(int c)
{
    fputc(c, out_f);
}

static int read_line(void)
{
    size_t n;

    if (!fgets(line, sizeof line, in)) {
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
            fprintf(out_f, "bad node %u\n", node);
            exit(1);
        }
        ref_title(&r, n.title, title, sizeof title);
        fprintf(out_f, "%*s%s", depth * 2, "", title);
        if (n.topic != REF_NONE) {
            fprintf(out_f, "  [%u]", n.topic);
        }
        fprintf(out_f, "\n");
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
        fprintf(out_f, "\n");

        return;
    }
    for (x = 0; x < ln->x; x++) {
        fputc_out(ln->note && x == ln->x - 2 ? '|' : ' ');
    }
    if (ln->kind == REF_RULE) {
        for (x = ln->x; x < width; x++) {
            fputc_out('-');
        }
        fprintf(out_f, "\n");

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
        fwrite(buf, 1, (size_t)n, out_f);
    }
    fprintf(out_f, "\n");
}

int main(int argc, char** argv)
{
    int e;
    unsigned k;

    if (argc < 3) {
        fprintf(stderr, "usage: reftool <document> <mode>\n");

        return 2;
    }
    in = stdin;
    out_f = stdout;
    for (k = 3; k + 1 < (unsigned)argc; k++) {
        if (strcmp(argv[k], "-i") == 0) {
            in = fopen(argv[k + 1], "r");
        } else if (strcmp(argv[k], "-o") == 0) {
            out_f = fopen(argv[k + 1], "wb");
        }
    }
    if (!in || !out_f) {
        fprintf(stderr, "reftool: cannot open its files\n");

        return 1;
    }
    e = ref_open(&r, argv[1]);
    if (e != REF_OK) {
        fprintf(stderr, "reftool: cannot open %s: %d\n", argv[1], e);

        return 1;
    }
    if (strcmp(argv[2], "info") == 0) {
        if (ref_book_title(&r, title, sizeof title) >= 0) {
            fprintf(out_f, "title %s\n", title);
        }
        fprintf(out_f, "commit %s\ndate %s\ntopics %u\nnodes %u\nkeys %lu\nwords %lu\nview %u\n",
               r.commit, r.date, r.ntopics, r.nnodes, (unsigned long)r.nkeys,
               (unsigned long)r.nwords, r.view_size);
    } else if (strcmp(argv[2], "norm") == 0) {
        while (read_line()) {
            ref_normalize(&r, line, out);
            fprintf(out_f, "%s\n", out);
        }
    } else if (strcmp(argv[2], "lookup") == 0) {
        while (read_line()) {
            int count;
            int kind = ref_lookup(&r, line, matches, MAX_MATCH, &count);
            int n = count < MAX_MATCH ? count : MAX_MATCH;
            int i;

            fprintf(out_f, "%d %d", kind, count);
            for (i = 0; i < n; i++) {
                fprintf(out_f, " %u", matches[i].topic);
            }
            fprintf(out_f, " |");
            for (i = 0; i < n; i++) {
                fprintf(out_f, " %s;", matches[i].key);
            }
            fprintf(out_f, "\n");
        }
    } else if (strcmp(argv[2], "from") == 0) {
        while (read_line()) {
            int count = ref_keys_from(&r, line, matches, MAX_MATCH);
            int n = count < MAX_MATCH ? count : MAX_MATCH;
            int i;

            fprintf(out_f, "%d", count);
            for (i = 0; i < n; i++) {
                fprintf(out_f, " %s=%u;", matches[i].key, matches[i].topic);
            }
            fprintf(out_f, "\n");
        }
    } else if (strcmp(argv[2], "titles") == 0) {
        /* Every topic's title from the loaded section, as the viewer reads them. */
        const uint8_t* t = ref_titles(&r);
        ref_topic tp;

        for (k = 0; k < r.ntopics; k++) {
            ref_get_topic(&r, k, &tp);
            fwrite(t + tp.title + 1, 1, t[tp.title], out_f);
            fputc_out('\n');
        }
    } else if (strcmp(argv[2], "search") == 0) {
        while (read_line()) {
            int n = ref_search(&r, line, hits, MAX_HITS);
            int i;

            fprintf(out_f, "%d", n);
            for (i = 0; i < n && i < MAX_HITS; i++) {
                fprintf(out_f, " %u:%u:%u", hits[i].topic, hits[i].title, hits[i].words);
            }
            fprintf(out_f, "\n");
        }
    } else if (strcmp(argv[2], "views") == 0) {
        for (k = 0; k < r.ntopics; k++) {
            int n = ref_view(&r, k);

            if (n < 0) {
                fprintf(stderr, "reftool: topic %u will not decompress\n", k);

                return 1;
            }
            fputc_out(n & 0xff);
            fputc_out((n >> 8) & 0xff);
            fwrite(r.view, 1, (size_t)n, out_f);
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
            fprintf(out_f, "@@ %u %d\n", k, nl);
            for (i = 0; i < nl && i < MAX_LINES; i++) {
                show(r.view, &lines[i], width);
            }
            /* Then the style and link each line starts in. */
            for (i = 0; i < nl && i < MAX_LINES; i++) {
                fprintf(out_f, "%u:%u%c", lines[i].style, lines[i].link, i + 1 < nl ? ' ' : '\n');
            }
            if (nl == 0) {
                fprintf(out_f, "\n");
            }
        }
    } else if (strcmp(argv[2], "contents") == 0) {
        contents(0, 0);
    } else {
        fprintf(stderr, "reftool: no mode %s\n", argv[2]);

        return 2;
    }
    ref_close(&r);
    if (out_f != stdout) {
        fclose(out_f);
    }

    return 0;
}
