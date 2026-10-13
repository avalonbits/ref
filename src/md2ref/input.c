/*
 * The pages: which they are -- the files given, or a directory's .md files
 * in name order, index.md or README.md first -- and each read through a
 * window, a run of whole blocks at a time, as lines with tabs expanded to
 * stops of four.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef AGONDEV
#include <agon/mos.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

#include "md2ref.h"

#define MAX_PAGES 64
#define NAME_MAX  256

page* pages;
int npages;

static int lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

static int ends_md(const char* name)
{
    int n = (int)strlen(name);

    return n > 3 && name[n - 3] == '.' && lower(name[n - 2]) == 'm' && lower(name[n - 1]) == 'd';
}

static int same_lower(const char* a, const char* b)
{
    for (; *a && *b; a++, b++) {
        if (lower((unsigned char)*a) != lower((unsigned char)*b)) {
            return 0;
        }
    }

    return *a == *b;
}

static int add_page(const char* path)
{
    const char* slash = strrchr(path, '/');
    page* p;

    if (npages >= MAX_PAGES) {
        md_error(path, 0, "too many pages (at most %d)", MAX_PAGES);

        return -1;
    }
    p = &pages[npages++];
    p->path = arena_strdup(path);
    p->name = slash ? p->path + (slash - path) + 1 : p->path;
    p->waiting = -1;                /* no link waits for it yet */

    return 0;
}

/* --- a page, a run of whole blocks at a time ----------------------------------- */

#define WINDOW     8192             /* to start with; it doubles for a long block */
#define WINDOW_MAX 32768

static FILE* in;
static const char* in_path;
static char* win;                   /* the file, from beg to end */
static unsigned win_size;
static unsigned beg;
static unsigned end;
static unsigned taken;              /* the last run's bytes, gone at the next call */
static int at_eof;
static int next_no;                 /* the number of the line at beg */
static int fresh;                   /* nothing read yet: front matter may come */

static line* run;                   /* the run's lines */
static int run_size;
static char* expanded;              /* lines with tabs, expanded */
static unsigned expanded_size;
static char* look;                  /* a line expanded, to see what it is */
static unsigned look_size;

int page_open(int i)
{
    in_path = pages[i].path;
    in = fopen(in_path, "rb");
    if (!in) {
        md_error(in_path, 0, "cannot be read");

        return -1;
    }
    if (!win) {
        win_size = WINDOW;
        win = md_realloc(NULL, win_size);
    }
    beg = end = taken = 0;
    at_eof = 0;
    next_no = 1;
    fresh = 1;

    return 0;
}

void page_close(void)
{
    if (in) {
        fclose(in);
        in = NULL;
    }
}

void pages_read(void)
{
    free(win);
    free(run);
    free(expanded);
    free(look);
    win = expanded = look = NULL;
    run = NULL;
    win_size = expanded_size = look_size = 0;
    run_size = 0;
}

/* More of the file into the window, what is left moved to its start. 0, or
 * -1 if the window is full and as big as it may be. */
static int fill(void)
{
    unsigned n;

    if (beg > 0) {
        memmove(win, win + beg, end - beg);
        end -= beg;
        beg = 0;
    }
    if (end == win_size) {
        if (win_size >= WINDOW_MAX) {
            return -1;
        }
        win_size *= 2;
        win = md_realloc(win, win_size);
    }
    n = (unsigned)fread(win + end, 1, win_size - end, in);
    end += n;
    if (n == 0) {
        at_eof = 1;
    }

    return 0;
}

/* The line at offset r from beg: where it ends (its '\n', or the end of the
 * file), -1 if there is none, or -2 if more must be read first. */
static int line_at(unsigned r)
{
    const char* nl;

    if (beg + r >= end) {
        return at_eof ? -1 : -2;
    }
    nl = memchr(win + beg + r, '\n', end - beg - r);
    if (nl) {
        return (int)(nl - (win + beg));
    }

    return at_eof ? (int)(end - beg) : -2;
}

static int tabs_in(const char* s, int len)
{
    int n = 0;
    int k;

    if (len <= 0 || !memchr(s, '\t', (size_t)len)) {
        return 0;                   /* most lines: memchr is the eZ80's cpir */
    }
    for (k = 0; k < len; k++) {
        n += s[k] == '\t';
    }

    return n;
}

/* s, tabs expanded to stops of four, into out. */
static int expand(const char* s, int len, char* out)
{
    int col = 0;
    int k;

    for (k = 0; k < len; k++) {
        if (s[k] == '\t') {
            do {
                out[col++] = ' ';
            } while (col % 4);
        } else {
            out[col++] = s[k];
        }
    }

    return col;
}

static int spaces(const char* s, int len)
{
    int k = 0;

    while (k < len && s[k] == ' ') {
        k++;
    }

    return k;
}

/* The line from r to e as the parser sees it, \r dropped, tabs expanded
 * (into out, room for them, when it has any: tabs says, as the caller
 * has counted them), its indent counted. */
static line seen_into(unsigned r, int e, char* out, int tabs)
{
    line l;
    const char* s = win + beg + r;
    int len = e - (int)r;

    while (len > 0 && s[len - 1] == '\r') {
        len--;
    }
    l.text = s;
    l.len = len;
    l.no = 0;
    if (tabs) {
        l.len = expand(s, len, out);
        l.text = out;
    }
    l.indent = spaces(l.text, l.len);

    return l;
}

/* As seen_into, into look: for a line only looked at. */
static line seen(unsigned r, int e)
{
    const char* s = win + beg + r;
    int len = e - (int)r;
    int tabs = tabs_in(s, len);
    unsigned need = (unsigned)(len + tabs * 3 + 1);

    if (need > look_size) {
        look_size = need;
        look = md_realloc(look, look_size);
    }

    return seen_into(r, e, look, tabs);
}

/* A run may end before l: it starts a block, and no block before it can
 * take it in -- it is at the margin, after a blank line, and not a list
 * item (which a list before it would take). */
static int starts_run(const line* l)
{
    return !line_blank(l) && line_indent(l) == 0 && !line_item(l);
}

/* Front matter, --- lines at the very top: skipped. 0, or -1. */
static int skip_front(void)
{
    unsigned r = 0;
    int lines = 0;
    int e;

    for (;;) {
        line l;

        while ((e = line_at(r)) == -2) {
            if (fill() != 0) {
                return -1;
            }
        }
        if (e < 0) {
            return 0;                       /* never closed: no front matter */
        }
        l = seen(r, e);
        lines++;
        if (lines == 1 ? !(l.len >= 3 && memcmp(l.text, "---", 3) == 0)
                       : l.len >= 3 && memcmp(l.text, "---", 3) == 0) {
            if (lines > 1) {
                beg += (unsigned)e + (beg + (unsigned)e < end);
                next_no += lines;
            }

            return 0;
        }
        r = (unsigned)e + 1;
    }
}

/* Room in expanded for a line of len bytes with tabs in it, from x. */
static void expanded_room(unsigned x, int len, int tabs)
{
    unsigned need = x + (unsigned)(len + tabs * 3 + 1);

    if (need > expanded_size) {
        expanded_size = need + 256;
        expanded = md_realloc(expanded, expanded_size);
    }
}

/* Each of a run's lines is read once: where it ends, its tabs expanded,
 * its indent counted, and kept, so the parser asks none of it again. Its
 * text is kept as an offset -- from beg into the window, or into
 * expanded, marked by a negative length until the run is done -- as more
 * read into the window, or a longer line expanded, moves both. */
int page_next(line** lines, int* n)
{
    unsigned r;
    unsigned x = 0;             /* expanded's bytes so far */
    int fenced;
    char mark = 0;
    int open_len = 0;
    int was_blank;
    int count;
    int e;
    int k;

    beg += taken;
    taken = 0;
    if (fresh) {
        fresh = 0;
        if (skip_front() != 0) {
            md_error(in_path, next_no, "a block of more than %u bytes", (unsigned)WINDOW_MAX);

            return -1;
        }
    }

    /* Where the run ends, read into the window as far as it needs. */
    r = 0;
    fenced = 0;
    was_blank = 0;
    count = 0;
    for (;;) {
        line l;
        const char* s;
        int tabs;
        int len;

        e = line_at(r);
        if (e == -2) {
            if (fill() != 0) {
                md_error(in_path, next_no, "a block of more than %u bytes: md2ref reads a"
                         " page %u bytes at a time", (unsigned)WINDOW_MAX, (unsigned)WINDOW_MAX);

                return -1;
            }
            continue;               /* the same line again: r is from beg, which fill keeps */
        }
        if (e == -1) {
            break;
        }
        s = win + beg + r;
        len = e - (int)r;
        tabs = tabs_in(s, len);
        if (tabs) {
            expanded_room(x, len, tabs);
        }
        l = seen_into(r, e, tabs ? expanded + x : NULL, tabs);
        if (count > 0 && !fenced && was_blank && starts_run(&l)) {
            break;
        }
        if (fenced) {
            fenced = !line_closes(&l, open_len, mark);
        } else if ((open_len = line_fence(&l, &mark)) != 0) {
            fenced = 1;
        }
        was_blank = line_blank(&l);
        if (count == run_size) {
            run_size = count + 64;
            run = md_realloc(run, (unsigned)run_size * (unsigned)sizeof(line));
        }
        run[count] = l;
        run[count].no = next_no + count;
        if (tabs) {
            run[count].text = (const char*)(uintptr_t)x;
            run[count].tabbed = 1;
            x += (unsigned)l.len;
        } else {
            run[count].text = (const char*)(uintptr_t)r;
            run[count].tabbed = 0;
        }
        count++;
        r = (unsigned)e + (beg + (unsigned)e < end);
    }
    if (count == 0) {
        return 0;
    }
    taken = r;
    next_no += count;
    for (k = 0; k < count; k++) {
        uintptr_t at = (uintptr_t)run[k].text;

        run[k].text = run[k].tabbed ? expanded + at : win + beg + at;
    }
    *lines = run;
    *n = count;

    return 1;
}

/* --- directories -------------------------------------------------------------- */

static char* found[MAX_PAGES];
static int nfound;

static void found_name(const char* dir, const char* name)
{
    if (nfound < MAX_PAGES && ends_md(name)) {
        char* path = arena_alloc((unsigned)(strlen(dir) + strlen(name) + 2));

        sprintf(path, "%s/%s", dir, name);
        found[nfound++] = path;
    }
}

#ifdef AGONDEV
static int list_dir(const char* dir)
{
    static DIR d;
    static FILINFO fi;

    if (ffs_dopen(&d, dir) != 0) {
        return -1;
    }
    while (ffs_dread(&d, &fi) == 0 && fi.fname[0]) {
        if (!(fi.fattrib & AM_DIR)) {
            found_name(dir, fi.fname);
        }
    }
    ffs_dclose(&d);

    return 0;
}
#else
static int list_dir(const char* dir)
{
    DIR* d = opendir(dir);
    struct dirent* e;

    if (!d) {
        return -1;
    }
    while ((e = readdir(d)) != NULL) {
        found_name(dir, e->d_name);
    }
    closedir(d);

    return 0;
}
#endif

void make_dir(const char* path)
{
#ifdef AGONDEV
    ffs_mkdir(path);
#else
    mkdir(path, 0777);
#endif
}

static const char* base_name(const char* path)
{
    const char* s = strrchr(path, '/');

    return s ? s + 1 : path;
}

/* index.md and README.md first, then the rest by name. */
static int before(const char* a, const char* b)
{
    int fa = same_lower(base_name(a), "index.md") || same_lower(base_name(a), "readme.md");
    int fb = same_lower(base_name(b), "index.md") || same_lower(base_name(b), "readme.md");

    if (fa != fb) {
        return fa;
    }

    return strcmp(a, b) < 0;
}

static int by_page(const void* a, const void* b)
{
    const char* x = *(char* const*)a;
    const char* y = *(char* const*)b;

    return before(x, y) ? -1 : before(y, x) ? 1 : 0;
}

static int is_dir(const char* path)
{
    return !ends_md(path);
}

int read_pages(int nargs, char** args)
{
    int i;

    pages = arena_alloc(MAX_PAGES * sizeof(page));
    if (nargs == 1 && is_dir(args[0])) {
        if (list_dir(args[0]) != 0) {
            md_error(args[0], 0, "not a directory of .md pages");

            return -1;
        }
        md_sort(found, (unsigned)nfound, (unsigned)sizeof(char*), by_page);
        if (nfound == 0) {
            md_error(args[0], 0, "has no .md pages");

            return -1;
        }
        for (i = 0; i < nfound; i++) {
            if (add_page(found[i]) != 0) {
                return -1;
            }
        }

        return 0;
    }
    for (i = 0; i < nargs; i++) {
        if (add_page(args[i]) != 0) {
            return -1;
        }
    }

    return 0;
}
