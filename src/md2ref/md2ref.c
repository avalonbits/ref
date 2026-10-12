/*
 * md2ref: Markdown pages compiled into a ref document. See md2ref.h.
 *
 *   md2ref [-t <title>] [-w] [-z] <name> [<dir> | <page.md>...]
 *   md2ref [-t <title>] [-w] [-z] -o <file.dat> <dir> | <page.md>...
 *
 *   <name>  the document: ref-doc/<name>.dat, where a program keeps its
 *           own, ref-doc made if it is not there; the pages, if none are
 *           given, are the current directory's
 *   -o      the document to write instead, anywhere
 *   -t      its title (else the first page's first heading)
 *   -w      a link that leads nowhere is a warning, not an error
 *   -z      compress each topic's text: a smaller document, but compressing
 *           takes as long as all the rest on the Agon; else each topic is
 *           stored as it is, an LZ4 block of literals any reader reads
 *   -e      errors and warnings into this file too, which is made even
 *           when there are none: ade reads it after a build
 *   @list   the pages named in this file, one a line: a build.obey line
 *           is too short for many
 *
 * Errors are reported as file:line: error: ..., and md2ref then writes
 * nothing and returns 100 -- as acc and zap do with -e, so an editor can
 * go to the line.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "md2ref.h"
#include "timing.h"

#if defined(AGONDEV) && defined(MD2REF_CYCLES)
#include <ez80f92.h>
/* The emulator's cycle count, for make agon-md2ref-cycles: from
 * fab-agon-emulator 2037657 on, a write to IO port 0x40 starts it and one
 * to 0x41 prints it on the host, exactly, as acc's bench does. The seconds
 * MOS reports come from a clock another thread of the emulator keeps, and
 * mean nothing when it runs flat out; the count is of the instructions the
 * eZ80 runs. Only in this build: on a real Agon the ports are not the
 * emulator's. */
#define cycles_start()  (IO(0x40) = 0)
#define cycles_report() (IO(0x41) = 0)
#else
#define cycles_start()
#define cycles_report()
#endif

#ifdef ZMALLOC
#include "zmalloc.h"
/* Each step's allocations under its name, in the shim's report. */
#define STEP(name) (z_site = (name), 1)
#else
#define STEP(name) 1
#endif

#define FAILED 100
#define CHUNK  4096

int md_errors;
int md_keep_going;
int md_compress;

static FILE* errors;        /* -e */

/* --- the arenas: chunks, each used from its start ------------------------------- */

/* Two: the book's, which lasts, and the source's -- the pages' text and
 * what parsing makes of it -- dropped once the book is rendered and keyed,
 * so the Agon need not hold the source and all that is made from it at
 * once.
 *
 * In each, the newest chunk is the one used. A large allocation gets a
 * chunk of its own, kept behind it, so the room left in the one in use is
 * not lost. The chunks are numbered as made, which is how going back to a
 * mark knows which came after it. */
typedef struct chunk chunk;
struct chunk {
    chunk* next;
    unsigned no;
    unsigned used;
    unsigned size;
};

static chunk* book_chunks;
static chunk* source_chunks;
static unsigned made;

void* md_realloc(void* p, unsigned n)
{
    p = realloc(p, n);
    if (!p) {
        printf("md2ref: error: out of memory\n");
        if (errors) {
            fprintf(errors, "md2ref: error: out of memory\n");
            fclose(errors);
        }
#ifdef ZMALLOC
        z_report();
#endif
        exit(FAILED);
    }

    return p;
}

static chunk* new_chunk(unsigned size)
{
    chunk* c = md_realloc(NULL, (unsigned)(sizeof(chunk) + size + sizeof(void*)));

    c->no = ++made;
    c->used = 0;
    c->size = size;

    return c;
}

static void* alloc_in(chunk** chunks, unsigned n)
{
    chunk* c = *chunks;
    void* p;

    n = (n + sizeof(void*) - 1) & ~(unsigned)(sizeof(void*) - 1);
    if (c && n > CHUNK / 4 && c->used + n > c->size) {
        c = new_chunk(n);
        c->next = (*chunks)->next;
        (*chunks)->next = c;
    } else if (!c || c->used + n > c->size) {
        c = new_chunk(n > CHUNK ? n : CHUNK);
        c->next = *chunks;
        *chunks = c;
    }
    p = (char*)(c + 1) + c->used;
    c->used += n;
    memset(p, 0, n);

    return p;
}

static void free_all(chunk** chunks)
{
    while (*chunks) {
        chunk* c = *chunks;

        *chunks = c->next;
        free(c);
    }
}

void* arena_alloc(unsigned n)
{
    return alloc_in(&book_chunks, n);
}

char* arena_strndup(const char* s, unsigned n)
{
    char* p = arena_alloc(n + 1);

    memcpy(p, s, n);
    p[n] = 0;

    return p;
}

char* arena_strdup(const char* s)
{
    return arena_strndup(s, (unsigned)strlen(s));
}

arena_mark arena_here(void)
{
    arena_mark m;

    m.made = made;
    m.no = book_chunks ? book_chunks->no : 0;
    m.used = book_chunks ? book_chunks->used : 0;

    return m;
}

void arena_back(arena_mark m)
{
    chunk** at = &book_chunks;

    while (*at) {
        chunk* c = *at;

        if (c->no > m.made) {
            *at = c->next;
            free(c);
        } else {
            if (c->no == m.no) {
                c->used = m.used;
            }
            at = &c->next;
        }
    }
}

static char* copy_in(chunk** chunks, const char* s)
{
    unsigned n = (unsigned)strlen(s);
    char* p = alloc_in(chunks, n + 1);

    memcpy(p, s, n + 1);

    return p;
}

void* source_alloc(unsigned n)
{
    return alloc_in(&source_chunks, n);
}

char* source_strndup(const char* s, unsigned n)
{
    char* p = source_alloc(n + 1);

    memcpy(p, s, n);
    p[n] = 0;

    return p;
}

/* Blocks still wanted, their strings copied to a fresh source arena and
 * the old one freed: the topics before them are done with. */
static void keep_blocks(block* b, int n)
{
    chunk* fresh = NULL;
    int k;
    int j;

    for (k = 0; k < n; k++) {
        const char** ids = NULL;

        if (b[k].nids) {
            ids = alloc_in(&fresh, (unsigned)b[k].nids * (unsigned)sizeof(char*));
            for (j = 0; j < b[k].nids; j++) {
                ids[j] = copy_in(&fresh, b[k].ids[j]);
            }
        }
        b[k].text = b[k].text ? copy_in(&fresh, b[k].text) : NULL;
        b[k].marker = b[k].marker ? copy_in(&fresh, b[k].marker) : NULL;
        b[k].id = b[k].id ? copy_in(&fresh, b[k].id) : NULL;
        b[k].ids = ids;
    }
    free_all(&source_chunks);
    source_chunks = fresh;
}

/* --- errors ------------------------------------------------------------------------ */

/* file:line: what: message, on the console and, with -e, in the file. */
static void report(const char* what, const char* file, int no, const char* message)
{
    static char where[300];

    if (no > 0) {
        snprintf(where, sizeof where, "%s:%d: %s: ", file, no, what);
    } else {
        snprintf(where, sizeof where, "%s: %s: ", file, what);
    }
    printf("%s%s\n", where, message);
    if (errors) {
        fprintf(errors, "%s%s\n", where, message);
    }
}

void md_error(const char* file, int no, const char* fmt, ...)
{
    static char message[300];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(message, sizeof message, fmt, ap);
    va_end(ap);
    report("error", file, no, message);
    md_errors++;
}

void md_warn(const char* file, int no, const char* fmt, ...)
{
    static char message[300];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(message, sizeof message, fmt, ap);
    va_end(ap);
    report("warning", file, no, message);
}

/* @list: each line a page, into inputs (up to max). 0, or -1. */
static int read_list(const char* path, char** inputs, int* n, int max)
{
    static char line[256];
    FILE* f = fopen(path, "r");
    int no = 0;

    if (!f) {
        md_error(path, 0, "cannot be read");

        return -1;
    }
    while (fgets(line, sizeof line, f)) {
        int len = (int)strlen(line);

        no++;
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'
                           || line[len - 1] == ' ' || line[len - 1] == '\t')) {
            line[--len] = 0;
        }
        if (len == 0) {
            continue;
        }
        if (*n >= max) {
            md_error(path, no, "too many pages (at most %d)", max);
            fclose(f);

            return -1;
        }
        inputs[(*n)++] = arena_strdup(line);
    }
    fclose(f);

    return 0;
}

/* --- the steps ------------------------------------------------------------------- */

#define DOC_DIR "ref-doc"

static int usage(void)
{
    printf("usage: md2ref [-t <title>] [-w] [-z] [-e <file>] <name> [<dir> | <page.md>... | @list]\n"
           "       md2ref [-t <title>] [-w] [-z] [-e <file>] -o <file.dat> <dir> | <page.md>... | @list\n"
           "  <name> writes " DOC_DIR "/<name>.dat, from the current directory's pages\n"
           "  if none are given; @list names the pages in a file, one a line\n");

    return FAILED;
}

/* A document's name, as ref takes it: one to eight lower-case letters and
 * digits. */
static int valid_name(const char* s)
{
    int n = 0;

    for (; *s; s++, n++) {
        if (!((*s >= 'a' && *s <= 'z') || (*s >= '0' && *s <= '9'))) {
            return 0;
        }
    }

    return n >= 1 && n <= 8;
}

/* A page, read once: each run of its lines parsed, and each topic, as the
 * next begins, rendered and written -- or held, until its descendants are
 * read. */
static int read_page(int pi)
{
    page* p = &pages[pi];
    line* L;
    int n;
    int got;
    int cur = -1;               /* the topic being read */
    int first = 0;              /* its first block */
    int seen = 0;               /* the blocks looked at */
    int any = 0;                /* a block yet on this page */

    if (page_open(pi) != 0) {
        return -1;
    }
    book_page(pi);
    parse_begin(p);
    while ((got = page_next(&L, &n)) == 1) {
        int done_some = 0;

        parse_run(p, L, n);
        for (; seen < p->nblocks; seen++) {
            const block* b = &p->blocks[seen];

            if (!any) {
                any = 1;
                cur = book_top(pi, b);
            }
            if (book_starts_topic(b)) {
                int next;

                if (cur >= 0) {
                    render_topic(cur, p->blocks + first, seen - first);
                }
                first = seen;           /* moved down once a run, not at each topic */
                done_some = 1;
                next = book_heading(pi, &p->blocks[seen]);
                if (next < 0) {
                    page_close();

                    return -1;
                }
                render_complete(TOPIC(next).parent);
                cur = next;
            }
            if (cur >= 0) {
                book_ids(pi, cur, &p->blocks[seen]);
            }
        }
        if (done_some) {
            memmove(p->blocks, p->blocks + first, (unsigned)(p->nblocks - first) * sizeof(block));
            p->nblocks -= first;
            seen -= first;
            first = 0;
            keep_blocks(p->blocks, p->nblocks);
        }
    }
    page_close();
    if (got < 0) {
        return -1;
    }
    if (!any) {
        cur = book_top(pi, NULL);
    }
    if (cur >= 0) {
        render_topic(cur, p->blocks, p->nblocks);
    }
    p->nblocks = 0;
    free_all(&source_chunks);
    render_complete(-1);
    book_page_done(pi);

    return 0;
}

int main(int argc, char** argv)
{
    static char* inputs[64];
    static char here[] = ".";
    static char named[sizeof DOC_DIR + 16];
    const char* out = NULL;
    const char* title = NULL;
    const char* error_file = NULL;
    clock_t begin;
    unsigned cs;
    int ninputs = 0;
    int i;
    int ok;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            out = argv[++i];
        } else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            title = argv[++i];
        } else if (strcmp(argv[i], "-w") == 0) {
            md_keep_going = 1;
        } else if (strcmp(argv[i], "-z") == 0) {
            md_compress = 1;
        } else if (strcmp(argv[i], "-e") == 0 && i + 1 < argc) {
            error_file = argv[++i];
        } else if (argv[i][0] == '-') {
            return usage();
        } else if (ninputs < 64) {
            inputs[ninputs++] = argv[i];
        }
    }
    if (error_file) {
        errors = fopen(error_file, "w");
        if (!errors) {
            printf("%s: error: cannot be written\n", error_file);

            return FAILED;
        }
    }
    if (!out) {
        /* md2ref <name> [pages]: the first is the name. */
        if (ninputs == 0) {
            return usage();
        }
        if (!valid_name(inputs[0])) {
            md_error("md2ref", 0, "'%.40s' is not a document's name: one to eight lower-case"
                     " letters and digits (or give -o <file.dat>)", inputs[0]);
            if (errors) {
                fclose(errors);
            }

            return FAILED;
        }
        sprintf(named, "%s/%s.dat", DOC_DIR, inputs[0]);
        out = named;
        ninputs--;
        memmove(inputs, inputs + 1, (size_t)ninputs * sizeof inputs[0]);
        if (ninputs == 0) {
            inputs[ninputs++] = here;
        }
        make_dir(DOC_DIR);
    } else if (ninputs == 0) {
        return usage();
    }
    /* @list: the pages it names, in its place. */
    if (ninputs == 1 && inputs[0][0] == '@') {
        const char* list = inputs[0] + 1;

        ninputs = 0;
        if (read_list(list, inputs, &ninputs, 64) != 0 || ninputs == 0) {
            if (ninputs == 0 && md_errors == 0) {
                md_error(list, 0, "names no pages");
            }
            if (errors) {
                fclose(errors);
            }
            free_all(&book_chunks);

            return FAILED;
        }
    }

    /* Timed as zap and acc time it, down to the wording: from after the
     * arguments are checked to after the document is written. */
    begin = clock();
    cycles_start();
    ok = read_pages(ninputs, inputs) == 0 && scratch_open(out) == 0;
    for (i = 0; ok && i < npages; i++) {
        STEP("pages");
        ok = read_page(i) == 0;
    }
    ok = ok && md_errors == 0 && book_done(title) == 0 && render_done() == 0
         && (number_topics(), 1)
         && resolve_pending() == 0 && STEP("index") && (make_keys(), 1)
         && STEP("write") && write_book(out) == 0;
    scratch_close();
    cycles_report();
    cs = elapsed_cs(begin, clock());
    if (ok) {
        printf("md2ref: %s: %d pages, %d topics, %u keys, %u words\n", out, npages, ntopics,
               keys_section.count, words_section.count);
    }
    if (errors) {
        fclose(errors);
    }
    free_all(&book_chunks);
    free_all(&source_chunks);
#ifdef ZMALLOC
    z_report();             /* the heap's peak: make agon-md2ref-heap */
#endif
    if (ok) {
        printf("Done in %u.%02u seconds\n", cs / 100, cs % 100);
    }

    return ok ? 0 : FAILED;
}
