/*
 * ref: documentation on the Agon. Each document is a file in the format of
 * docs/FORMAT.md, installed as /config/ref/docs/<name>.dat -- or in the
 * current directory, as <name>.dat, which comes first.
 *
 *   ref                     the documents here and installed, to choose one
 *   ref <doc>               a document's contents
 *   ref <doc> <query>       a key lookup, then a search if nothing matches
 *   ref <doc> -k <query>    the key lookup only: what ade's F1 runs
 *   ref <doc> -s <words>    the full-text search
 *   ref -f <file> ...       a document by its path, instead of its name
 *   ref -p ...              print what was found instead of showing it;
 *                           ref -p alone prints the documents
 *   ref <doc> -T [query]    time a lookup, a search and the largest topic
 *   ref -v [doc]            ref's version, and a document's source
 *
 * Returns 0 when a topic (or a list to choose one from) was shown, and 100
 * when nothing matched or nothing could be shown -- never 1, 4 or 5, which
 * MOS would report as errors of its own.
 */
#include <stdio.h>
#include <string.h>

#include <agon/mos.h>

#include "script.h"
#include "ui.h"
#include "version.h"

#ifdef ZMALLOC
#include "zmalloc.h"
#endif

#define NOTHING 100
#define MATCHES 64
#define HITS    100
#define QUERY   200

ref_file book;

/* The command line. */
static struct {
    const char* name;           /* the document, by name */
    const char* path;           /* its file: -f, or from the name */
    int keys_only;              /* -k */
    int search_only;            /* -s */
    int print;                  /* -p */
    int time_it;                /* -T */
    int version;                /* -v */
    char query[QUERY + 1];      /* the rest, joined with spaces */
} opt;

/* What the query found: a lookup's matches, or a search's hits. */
static int kind;                /* REF_LOOK_*; NONE with hits for a search */
static int count;               /* how many are held, at most MATCHES or HITS */
static int total;               /* how many there were */
static ref_match matches[MATCHES];
static ref_hit hits[HITS];

static char line[200];
static const uint8_t* titles;

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

/* 0, or 1 if it printed the usage and ref should stop. Options may come
 * anywhere; the first other word is the document (unless -f named a file),
 * and the rest is the query. */
static int parse(int argc, char** argv)
{
    int i;

    for (i = 1; i < argc; i++) {
        const char* a = argv[i];

        if (strcmp(a, "-k") == 0) {
            opt.keys_only = 1;
        } else if (strcmp(a, "-s") == 0) {
            opt.search_only = 1;
        } else if (strcmp(a, "-p") == 0) {
            opt.print = 1;
        } else if (strcmp(a, "-T") == 0) {
            opt.time_it = 1;
        } else if (strcmp(a, "-v") == 0) {
            opt.version = 1;
        } else if (strcmp(a, "-f") == 0 && i + 1 < argc) {
            opt.path = argv[++i];
        } else if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            ui_print("usage: ref [<doc> [-k | -s] [query]] [-p] [-v] [-f file]\n");

            return 1;
        } else if (!opt.name && !opt.path) {
            opt.name = a;
        } else if (strlen(opt.query) + strlen(a) + 2 < QUERY) {
            if (opt.query[0]) {
                strcat(opt.query, " ");
            }
            strcat(opt.query, a);
        }
    }

    return 0;
}

static int open_book(void)
{
    int e;

#ifdef ZMALLOC
    z_site = "ref_open: tables, buffers";
#endif
    e = ref_open(&book, opt.path);
    if (e == REF_ENOFILE && opt.name) {
        sprintf(line, "ref: no document %.20s; ref lists them\n", opt.name);
        ui_print(line);

        return -1;
    }
    if (e != REF_OK) {
        sprintf(line, "ref: cannot read %s%s\n", opt.path,
                e == REF_ENOFILE ? "" : e == REF_EVERSION ? ": a newer format than this ref"
                : e == REF_ENOMEM ? ": not enough memory" : ": not a ref document");
        ui_print(line);

        return -1;
    }
#ifdef REF_SCRIPT
    {
        static char norm[REF_KEY_MAX + 1];

        ref_normalize(&book, opt.query, norm);
        LOG("args '%s' normalised '%s'", opt.query, norm);
    }
#endif
#ifdef ZMALLOC
    z_site = "the titles";
#endif
    titles = ref_titles(&book);

    return 0;
}

/* Look the query up -- then, unless -k, search for it if no key matches.
 * 0, or -1 if nothing was found (and ref said so). */
static int look_up(void)
{
    kind = REF_LOOK_NONE;
    count = 0;
    if (!opt.search_only) {
        kind = ref_lookup(&book, opt.query, matches, MATCHES, &count);
        total = count;
        if (count > MATCHES) {
            count = MATCHES;
        }
    }
    if (kind == REF_LOOK_NONE && !opt.keys_only) {
        count = ref_search(&book, opt.query, hits, HITS);
        total = count;
        if (count > HITS) {
            count = HITS;
        }
    }
    if (count == 0) {
        sprintf(line, "ref: nothing for %.60s\n", opt.query);
        ui_print(line);

        return -1;
    }

    return 0;
}

/* One topic found, rather than several to choose from. */
static int one_topic(void)
{
    return kind != REF_LOOK_NONE && kind != REF_LOOK_LIST && count == 1;
}

static void print_found(void)
{
    int width = getsysvar_scrCols() - 1;

    if (width < 19) {
        width = 79;
    }
    if (one_topic()) {
        batch_print_topic(matches[0].topic, width);
    } else if (kind == REF_LOOK_NONE) {
        batch_print_hits(hits, count);
    } else {
        batch_print_matches(matches, count);
    }
}

/* The book open, shown from what the query found -- its contents, if there
 * was no query. GO_QUIT, or GO_BACK to go back to the documents. */
static int show_found(void)
{
    static char book_title[REF_TITLE_MAX + 1];
    int t;

    if (ref_book_title(&book, book_title, sizeof book_title) < 0) {
        strcpy(book_title, opt.name ? opt.name : opt.path);
    }
    scr_set_header(book_title);
    if (!opt.query[0]) {
        return nav_run(GO_TOC, -1, NULL, 0);
    }
    if (one_topic()) {
        return nav_run(GO_VIEW, matches[0].topic, NULL, 0);
    }
    if (kind == REF_LOOK_NONE) {
        sprintf(line, "Search: %.50s", opt.query);
        t = pick_hits(line, hits, count, total);
    } else {
        sprintf(line, "%d topics for %.50s", total, opt.query);
        t = pick_topics(line, matches, count, total, 1);
    }

    return t >= 0 ? nav_run(GO_VIEW, t, NULL, GO_LIST) : GO_QUIT;
}

/* Plain ref: the documents, to choose from, until the user quits; each
 * opens at its contents, and Esc there comes back here. */
static int browse(void)
{
    const char* name;

    if (shelf_scan() == 0) {
        sprintf(line, "ref: no documents here or in %s\n", SHELF_DIR);
        ui_print(line);

        return NOTHING;
    }
    scr_open();
    in_open();
    while ((name = shelf_choose()) != NULL) {
        int go;

        opt.name = name;
        opt.path = shelf_path(name);
        if (open_book() != 0) {
            break;
        }
        go = show_found();
        ref_close(&book);
        view_reset();
        toc_reset();
        pick_reset();
        if (go == GO_QUIT) {
            break;
        }
    }
    in_close();
    scr_close();

    return 0;
}

static int run(int argc, char** argv)
{
    int result = 0;

    if (parse(argc, argv)) {
        return 0;
    }
    if (!opt.name && !opt.path) {
        if (opt.version) {
            ui_print("ref " REF_VERSION "\n");

            return 0;
        }
        if (opt.print) {
            if (shelf_scan() == 0) {
                return NOTHING;
            }
            shelf_print();

            return 0;
        }

        return browse();
    }
    if (opt.name && !shelf_valid_name(opt.name)) {
        sprintf(line, "ref: %.20s is not a document's name; ref lists them\n", opt.name);
        ui_print(line);

        return NOTHING;
    }
    if (!opt.path) {
        opt.path = shelf_path(opt.name);
    }
    if (open_book() != 0) {
        return NOTHING;
    }
    if (opt.version) {
        sprintf(line, "ref %s, %s: %s (%s)\n", REF_VERSION, opt.name ? opt.name : opt.path,
                book.commit, book.date);
        ui_print(line);
    } else if (opt.time_it) {
        result = batch_time(opt.path, opt.query) == 0 ? 0 : NOTHING;
    } else if (opt.print && !opt.query[0]) {
        ui_print("ref: -p needs a query\n");
        result = NOTHING;
    } else if (opt.query[0] && look_up() != 0) {
        result = NOTHING;
    } else if (opt.print) {
        print_found();
    } else {
        scr_open();
        in_open();
        show_found();
        in_close();
        scr_close();
    }
    ref_close(&book);

    return result;
}

int main(int argc, char** argv)
{
    int e = run(argc, argv);

#ifdef ZMALLOC
    z_report();             /* the heap's peak: make agon-heap */
#endif
    LOG("exit %d", e);
    SCRIPTED(script_close());

    return e;
}
