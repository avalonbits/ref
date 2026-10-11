/*
 * ref: the Agon documentation, on the Agon.
 *
 *   ref                     the contents
 *   ref <query>             a key lookup, then a search if nothing matches
 *   ref -k <query>          the key lookup only: what ade's F1 runs
 *   ref -s <words>          the full-text search
 *   ref -f <file>           another ref.dat (default /lib/ref/ref.dat)
 *   ref -p ...              print what was found, instead of showing it
 *   ref -T [query]          time a lookup, a search and the largest topic
 *   ref -v                  ref's version, and the agon-docs its ref.dat holds
 *
 * Returns 0 when a topic (or a list to choose one from) was shown, and 100
 * when nothing matched or nothing could be shown -- never 1, 4 or 5, which
 * MOS would report as errors of its own.
 */
#include <stdio.h>
#include <string.h>

#include <agon/mos.h>

#include "ui.h"
#include "version.h"

#ifdef ZMALLOC
#include "zmalloc.h"
#endif

#define NOTHING 100
#define MATCHES 64
#define HITS    100
#define ARGS    200

ref_file book;

static ref_match matches[MATCHES];
static ref_hit hits[HITS];
static char args[ARGS + 1];
static char line[200];
static const uint8_t* titles;

static const char DEFAULT_FILE[] = "/lib/ref/ref.dat";

const char* ui_title_at(uint32_t off)
{
    static char buf[REF_TITLE_MAX + 1];

    if (titles) {
        unsigned n = titles[off];

        memcpy(buf, titles + off + 1, n);
        buf[n] = 0;
    } else if (ref_title(&book, off, buf, sizeof buf) < 0) {
        buf[0] = 0;
    }

    return buf;
}

const char* ui_title(unsigned topic)
{
    ref_topic t;

    ref_get_topic(&book, topic, &t);

    return ui_title_at(t.title);
}

static void out(const char* s)
{
    while (*s) {
        if (*s == '\n') {
            putch('\r');
        }
        putch(*s++);
    }
}

void ui_message(const char* text)
{
    scr_cls();
    scr_begin(scr_rows / 2);
    scr_colour(C_TEXT);
    scr_adds("  ");
    scr_adds(text);
    scr_end();
    scr_begin(scr_rows);
    scr_colour(C_STATUS);
    scr_adds(" Press a key");
    scr_end();
#ifdef REF_SCRIPT
    script_log("message %s", text);
#endif
    in_wait();
}

static const char* const HELP[] = {
    "ref -- the Agon documentation",
    "",
    "Reading a topic",
    "  Up Down PgUp PgDn Home End   scroll (also Space and b)",
    "  Left Right                   scroll code and tables sideways",
    "  Tab  Shift+Tab               choose the next or previous link",
    "  Enter                        follow the link",
    "  Backspace  Alt+Left          back",
    "  Alt+Right                    forward",
    "  n  p                         the next or previous topic",
    "  u                            the section the topic is in",
    "  t                            the contents",
    "  /  F3                        find: type to narrow the list",
    "  Esc  q                       quit",
    "",
    "From the command line",
    "  ref                          the contents",
    "  ref <query>                  look it up: vdu 23 27 3, mos_fopen, cd",
    "  ref -k <query>               look it up, and nothing else",
    "  ref -s <words>               search the text",
    NULL,
};

void ui_help(void)
{
    int r;

    scr_cls();
    for (r = 0; HELP[r] && r < scr_rows; r++) {
        scr_begin(r);
        scr_colour(r == 0 || (HELP[r][0] && HELP[r][0] != ' ') ? C_HEAD : C_TEXT);
        scr_adds(HELP[r]);
        scr_end();
    }
    scr_begin(scr_rows);
    scr_colour(C_STATUS);
    scr_adds(" Press a key");
    scr_end();
#ifdef REF_SCRIPT
    scr_dump("help");
#endif
    in_wait();
}

/* --- running the views ---------------------------------------------------------- */

/* Between the views until the user quits: start at a topic, the contents
 * or find. */
static void run(int go, int topic, const char* text)
{
    if (go == GO_VIEW && view_open((unsigned)topic) != 0) {
        ui_message("That topic could not be read.");

        return;
    }
    for (;;) {
        int t;

        switch (go) {
        case GO_VIEW:
            go = view_loop();
            break;
        case GO_TOC:
            t = toc_loop(view_current());
            if (t >= 0) {
                go = view_open((unsigned)t) == 0 ? GO_VIEW : GO_TOC;
            } else if (t == -2) {
                go = GO_FIND;
            } else if (t == -3 || !view_has()) {
                return;
            } else {
                go = GO_VIEW;
            }
            break;
        case GO_FIND:
            t = pick_find(text);
            text = NULL;
            if (t >= 0) {
                go = view_open((unsigned)t) == 0 ? GO_VIEW : GO_FIND;
            } else if (view_has()) {
                go = GO_VIEW;
            } else {
                return;
            }
            break;
        default:
            return;
        }
        if (go == GO_QUIT) {
            return;
        }
    }
}

/* A chosen topic from a list, then the views from it. */
static void run_pick(int t)
{
    if (t >= 0) {
        run(GO_VIEW, t, NULL);
    }
}

/* --- printing ------------------------------------------------------------------- */

static ref_line plines[2000];

static void print_topic(unsigned num, int width)
{
    int n = ref_view(&book, num);
    int nl;
    int i;

    if (n < 0) {
        out("ref: that topic could not be read\n");

        return;
    }
    nl = ref_layout(book.view, (unsigned)n, width, plines, 2000);
    for (i = 0; i < nl && i < 2000; i++) {
        const ref_line* ln = &plines[i];
        unsigned k;
        int x;

        for (x = 0; x < ln->x && ln->kind; x++) {
            putch(ln->note && x == ln->x - 2 ? '|' : ' ');
        }
        if (ln->kind == REF_RULE) {
            for (; x < width; x++) {
                putch('-');
            }
        }
        for (k = ln->start; k < ln->end && ln->kind != REF_RULE; k = REF_SKIP(book.view, k)) {
            if (book.view[k] >= 0x20) {
                putch(book.view[k]);
            }
        }
        out("\n");
    }
}

static void print_list(const ref_match* m, int n, int labels)
{
    int i;

    for (i = 0; i < n && i < MATCHES; i++) {
        if (labels) {
            out(m[i].key);
            out("  ");
        }
        out(ui_title(m[i].topic));
        out("\n");
    }
}

static void print_hits(int n)
{
    int i;

    for (i = 0; i < n && i < HITS; i++) {
        out(ui_title(hits[i].topic));
        out("\n");
    }
}

/* --- timing --------------------------------------------------------------------- */

#define RUNS 10

/* Centiseconds a run, to a tenth, from MOS's own clock. */
static void report(const char* what, uint32_t start, const char* detail)
{
    uint32_t cs10 = (getsysvar_time() - start) * 10 / RUNS;

    sprintf(line, "%-8s %lu.%lu cs  (%s, %d runs)\n", what, (unsigned long)(cs10 / 10),
            (unsigned long)(cs10 % 10), detail, RUNS);
    out(line);
}

static int timing(const char* path, const char* query)
{
    uint32_t t;
    int i;
    int count;
    unsigned big = 0;
    unsigned k;
    ref_topic tp;
    static char detail[80];

    if (!query[0]) {
        query = "vdu 23 27 3";
    }
    t = getsysvar_time();
    for (i = 0; i < RUNS; i++) {
        ref_close(&book);
        if (ref_open(&book, path) != REF_OK) {
            return NOTHING;
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
    for (k = 0; k < book.ntopics; k++) {
        unsigned bigsize;

        ref_get_topic(&book, big, &tp);
        bigsize = tp.length;
        ref_get_topic(&book, k, &tp);
        if (tp.length > bigsize) {
            big = k;
        }
    }
    t = getsysvar_time();
    for (i = 0; i < RUNS; i++) {
        ref_view(&book, big);
    }
    sprintf(detail, "topic %u, the largest, read and decompressed", big);
    report("read", t, detail);
    t = getsysvar_time();
    for (i = 0; i < RUNS; i++) {
        int n = ref_view(&book, big);

        ref_layout(book.view, (unsigned)n, 79, plines, 2000);
    }
    sprintf(detail, "topic %u, the largest, read and laid out", big);
    report("view", t, detail);

    return 0;
}

/* --- the command line ------------------------------------------------------------ */

static int ref_main(int argc, char** argv);

int main(int argc, char** argv)
{
    int e = ref_main(argc, argv);

#ifdef ZMALLOC
    z_report();             /* the heap's peak: make heap */
#endif

#ifdef REF_SCRIPT
    script_log("exit %d", e);
    script_close();
#endif

    return e;
}

static int ref_main(int argc, char** argv)
{
    const char* path = DEFAULT_FILE;
    int keys_only = 0;
    int search_only = 0;
    int print = 0;
    int time_it = 0;
    int version = 0;
    int i;
    int e;
    int count;
    int kind;
    int width;

    args[0] = 0;
    for (i = 1; i < argc; i++) {
        const char* a = argv[i];

        if (strcmp(a, "-k") == 0) {
            keys_only = 1;
        } else if (strcmp(a, "-s") == 0) {
            search_only = 1;
        } else if (strcmp(a, "-p") == 0) {
            print = 1;
        } else if (strcmp(a, "-T") == 0) {
            time_it = 1;
        } else if (strcmp(a, "-v") == 0) {
            version = 1;
        } else if (strcmp(a, "-f") == 0 && i + 1 < argc) {
            path = argv[++i];
        } else if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            out("usage: ref [-k | -s] [-p] [-v] [-f file] [query]\n");

            return 0;
        } else if (strlen(args) + strlen(a) + 2 < ARGS) {
            if (args[0]) {
                strcat(args, " ");
            }
            strcat(args, a);
        }
    }

#ifdef ZMALLOC
    z_site = "ref_open: tables, buffers";
#endif
    e = ref_open(&book, path);
#ifdef REF_SCRIPT
    if (e == REF_OK) {
        static char norm[REF_KEY_MAX + 1];

        ref_normalize(&book, args, norm);
        script_log("args '%s' normalised '%s'", args, norm);
    }
#endif
    if (e != REF_OK) {
        sprintf(line, "ref: cannot read %s%s\n", path,
                e == REF_ENOFILE ? "" : e == REF_EVERSION ? ": a newer format than this ref"
                : e == REF_ENOMEM ? ": not enough memory" : ": not a ref.dat");
        out(line);

        return NOTHING;
    }
    if (version) {
        sprintf(line, "ref %s, agon-docs %s (%s)\n", REF_VERSION, book.commit, book.date);
        out(line);
        ref_close(&book);

        return 0;
    }
#ifdef ZMALLOC
    z_site = "the titles";
#endif
    titles = ref_titles(&book);
    if (time_it) {
        e = timing(path, args);
        ref_close(&book);

        return e;
    }
    width = getsysvar_scrCols() - 1;
    if (width < 19) {
        width = 79;
    }

    /* What the command line asks for, before anything is drawn. */
    kind = REF_LOOK_NONE;
    count = 0;
    if (args[0] && !search_only) {
        kind = ref_lookup(&book, args, matches, MATCHES, &count);
    }
    if (args[0] && kind == REF_LOOK_NONE && keys_only) {
        sprintf(line, "ref: nothing for %.60s\n", args);
        out(line);
        ref_close(&book);

        return NOTHING;
    }
    if (args[0] && kind == REF_LOOK_NONE) {
        count = ref_search(&book, args, hits, HITS);
        if (count == 0) {
            sprintf(line, "ref: nothing for %.60s\n", args);
            out(line);
            ref_close(&book);

            return NOTHING;
        }
    }

    if (print) {
        if (!args[0]) {
            out("ref: -p needs a query\n");
        } else if (kind == REF_LOOK_NONE) {
            print_hits(count);
        } else if (count == 1 && kind != REF_LOOK_LIST) {
            print_topic(matches[0].topic, width);
        } else {
            print_list(matches, count, 1);
        }
        ref_close(&book);

        return args[0] ? 0 : NOTHING;
    }

    scr_open();
    in_open();
    if (!args[0]) {
        run(GO_TOC, -1, NULL);
    } else if (kind == REF_LOOK_NONE) {
        sprintf(line, "Search: %.50s", args);
        run_pick(pick_hits(line, hits, count));
    } else if (count == 1 && kind != REF_LOOK_LIST) {
        run(GO_VIEW, matches[0].topic, NULL);
    } else {
        sprintf(line, "%d topics for %.50s", count, args);
        run_pick(pick_topics(line, matches, count < MATCHES ? count : MATCHES, 1));
    }
    in_close();
    scr_close();
    ref_close(&book);

    return 0;
}
