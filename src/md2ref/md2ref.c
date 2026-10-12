/*
 * md2ref: Markdown pages compiled into a ref document. See md2ref.h.
 *
 *   md2ref [-t <title>] [-w] <name> [<dir> | <page.md>...]
 *   md2ref [-t <title>] [-w] -o <file.dat> <dir> | <page.md>...
 *
 *   <name>  the document: ref-doc/<name>.dat, where a program keeps its
 *           own, ref-doc made if it is not there; the pages, if none are
 *           given, are the current directory's
 *   -o      the document to write instead, anywhere
 *   -t      its title (else the first page's first heading)
 *   -w      a link that leads nowhere is a warning, not an error
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
int md_quiet;

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

void drop_source(void)
{
    int i;

    free_all(&source_chunks);
    for (i = 0; i < npages; i++) {
        free(pages[i].blocks);
        pages[i].lines = NULL;
        pages[i].blocks = NULL;
    }
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

    if (md_quiet) {
        return;
    }
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

    if (md_quiet) {
        return;
    }
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

/* The first pass: each page's topics and anchors. */
static int outline(void)
{
    int i;

    for (i = 0; i < npages; i++) {
        if (load_page(i) == 0) {
            book_page(i);
            pages[i].outlined = pages[i].nblocks;
        }
        drop_source();
    }

    return md_errors ? -1 : 0;
}

/* The second: each page's topics' markup. Its blocks are as the outline
 * found them -- and what was wrong with them was said then. */
static int render(void)
{
    int i;

    for (i = 0; i < npages; i++) {
        int loaded;

        md_quiet = 1;
        loaded = load_page(i) == 0 && pages[i].nblocks == pages[i].outlined;
        md_quiet = 0;
        if (!loaded) {
            md_error(pages[i].path, 0, "changed while md2ref was reading it");
            drop_source();

            return -1;
        }
        render_page(i);
        drop_source();
    }

    return md_errors ? -1 : 0;
}

#define DOC_DIR "ref-doc"

static int usage(void)
{
    printf("usage: md2ref [-t <title>] [-w] [-e <file>] <name> [<dir> | <page.md>... | @list]\n"
           "       md2ref [-t <title>] [-w] [-e <file>] -o <file.dat> <dir> | <page.md>... | @list\n"
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

int main(int argc, char** argv)
{
    static char* inputs[64];
    static char here[] = ".";
    static char named[sizeof DOC_DIR + 16];
    const char* out = NULL;
    const char* title = NULL;
    const char* error_file = NULL;
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

    ok = read_pages(ninputs, inputs) == 0
         && STEP("outline") && outline() == 0 && book_done(title) == 0
         && STEP("render") && render() == 0 && render_done() == 0
         && STEP("index") && make_indexes() == 0
         && STEP("write") && write_book(out) == 0;
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

    return ok ? 0 : FAILED;
}
