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
nav_item* nav;
int nav_count;
int nav_first = -1;
const char* nav_site_name;

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

/* --- the nav: mkdocs.yml ------------------------------------------------------ */

#define NAV_MAX   16384             /* the most of an mkdocs.yml read */
#define NAV_ITEMS 1024
#define NAV_DEPTH 16

/* The pages outside the nav, in a section of their own, last: as the
 * builder names it. */
#define OTHER_PAGES "Pages outside the menu"


static int is_space(int c)
{
    return c == ' ' || c == '\t';
}

/* s, n bytes, its quotes off if it is quoted, as the builder's _unquote. */
static void unquote(const char** s, int* n)
{
    if (*n >= 2 && (**s == '\'' || **s == '"') && (*s)[*n - 1] == **s) {
        (*s)++;
        *n -= 2;
    }
}

static int new_item(const char* title, int tl, int page_no)
{
    nav_item* it;

    if (nav_count == NAV_ITEMS) {
        return -1;
    }
    it = &nav[nav_count];
    it->title = arena_strndup(title, (unsigned)tl);
    it->page = page_no;
    it->child = -1;
    it->next = -1;

    return nav_count++;
}

/* An item of the nav under parent (-1 for the top), after the last there. */
static void link_item(int k, int parent, int* last)
{
    if (last[0] >= 0) {
        nav[last[0]].next = k;
    } else if (parent >= 0) {
        nav[parent].child = k;
    } else {
        nav_first = k;
    }
    last[0] = k;
}

static int add_page(const char* path);

/* mkdocs.yml's site_name and nav, read as the builder's source.py reads
 * them: each "- title: file", "- file" or "- title:" a line, nested by
 * its indent, the pages' paths from dir. 0, -1 on an error, 1 with no
 * mkdocs.yml. */
static int read_nav(const char* dir)
{
    static char text[NAV_MAX + 1];
    static int stack_indent[NAV_DEPTH + 1];
    static int stack_item[NAV_DEPTH + 1];
    static int stack_last[NAV_DEPTH + 1];
    char* path = arena_alloc((unsigned)strlen(dir) + 12);
    FILE* f;
    unsigned n;
    char* s;
    int in_nav = 0;
    int depth = 0;
    int no = 0;

    sprintf(path, "%s/mkdocs.yml", dir);
    f = fopen(path, "rb");
    if (!f) {
        return 1;
    }
    n = (unsigned)fread(text, 1, NAV_MAX, f);
    fclose(f);
    text[n] = 0;
    nav = arena_alloc(NAV_ITEMS * (unsigned)sizeof(nav_item));
    stack_indent[0] = -1;
    stack_item[0] = -1;
    stack_last[0] = -1;
    for (s = text; *s;) {
        char* e = strchr(s, '\n');
        int len = e ? (int)(e - s) : (int)strlen(s);
        const char* l = s;
        int k = 0;

        s = e ? e + 1 : s + len;
        no++;
        while (len > 0 && (l[len - 1] == '\r' || is_space(l[len - 1]))) {
            len--;
        }
        while (k < len && is_space(l[k])) {
            k++;
        }
        if (!in_nav) {
            if (len >= 9 && memcmp(l, "site_name", 9) == 0) {
                int j = 9;

                while (j < len && is_space(l[j])) {
                    j++;
                }
                if (j < len && l[j] == ':') {
                    const char* v = l + j + 1;
                    int vl = len - j - 1;

                    while (vl > 0 && is_space(*v)) {
                        v++;
                        vl--;
                    }
                    unquote(&v, &vl);
                    nav_site_name = arena_strndup(v, (unsigned)vl);
                }
            } else if (len >= 3 && memcmp(l, "nav", 3) == 0) {
                int j = 3;

                while (j < len && is_space(l[j])) {
                    j++;
                }
                in_nav = j < len && l[j] == ':' && j + 1 == len;
            }
            continue;
        }
        if (k == len || l[k] == '#') {
            continue;                       /* blank, or a comment */
        }
        if (l[0] != ' ' && l[0] != '-') {
            in_nav = 0;                     /* the next top-level key */
            continue;
        }
        if (l[k] != '-' || k + 1 >= len || !is_space(l[k + 1])) {
            md_error(path, no, "nav: cannot read this line");

            return -1;
        }
        {
            int indent = k;
            const char* item = l + k + 1;
            int il = len - k - 1;
            const char* title = "";
            int tl = 0;
            const char* file = NULL;
            int fl = 0;
            int c;
            int it;

            while (il > 0 && is_space(*item)) {
                item++;
                il--;
            }
            /* "title: file", "title:", or "file": a title quoted, or up to
             * its first colon. */
            if (il > 0 && (*item == '\'' || *item == '"')) {
                const char* q = memchr(item + 1, *item, (size_t)(il - 1));

                c = q ? (int)(q - item) + 1 : -1;
                while (c > 0 && c < il && is_space(item[c])) {
                    c++;
                }
                if (c > 0 && c < il && item[c] == ':') {
                    title = item + 1;
                    tl = (int)(q - item) - 1;
                } else {
                    c = -1;
                }
            } else {
                const char* colon = memchr(item, ':', (size_t)il);

                c = colon ? (int)(colon - item) : -1;
                if (c > 0) {
                    title = item;
                    tl = c;
                    while (tl > 0 && is_space(title[tl - 1])) {
                        tl--;
                    }
                } else {
                    c = -1;
                }
            }
            if (c >= 0) {
                file = item + c + 1;
                fl = il - c - 1;
                while (fl > 0 && is_space(*file)) {
                    file++;
                    fl--;
                }
            } else {
                file = item;
                fl = il;
            }
            unquote(&file, &fl);
            while (depth > 0 && stack_indent[depth] >= indent) {
                depth--;
            }
            if (fl > 0) {
                char* p = arena_alloc((unsigned)(strlen(dir) + (unsigned)fl + 2));

                sprintf(p, "%s/%.*s", dir, fl, file);
                if (add_page(p) != 0) {
                    return -1;
                }
                pages[npages - 1].nav_title = tl ? arena_strndup(title, (unsigned)tl) : NULL;
                it = new_item(title, tl, npages - 1);
            } else {
                it = new_item(title, tl, -1);
            }
            if (it < 0 || depth == NAV_DEPTH) {
                md_error(path, no, "nav: too many items, or too deep");

                return -1;
            }
            link_item(it, stack_item[depth], &stack_last[depth]);
            depth++;
            stack_indent[depth] = indent;
            stack_item[depth] = it;
            stack_last[depth] = -1;
        }
    }
    if (nav_first < 0) {
        md_error(path, 0, "has no nav");

        return -1;
    }

    return 0;
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

static int by_name(const void* a, const void* b)
{
    return strcmp(*(char* const*)a, *(char* const*)b);
}

/* After the nav's pages, the directory's others, in a section of their
 * own, last, in name order: as the builder puts them. 0, or -1. */
static int others(const char* dir)
{
    int last = nav_first;
    int section = -1;
    int prev = -1;
    int i;

    if (list_dir(dir) != 0) {
        md_error(dir, 0, "not a directory of .md pages");

        return -1;
    }
    md_sort(found, (unsigned)nfound, (unsigned)sizeof(char*), by_name);
    while (nav[last].next >= 0) {
        last = nav[last].next;
    }
    for (i = 0; i < nfound; i++) {
        int k;
        int it;

        for (k = 0; k < npages && strcmp(pages[k].path, found[i]) != 0; k++) {
        }
        if (k < npages) {
            continue;                       /* in the nav */
        }
        if (section < 0) {
            section = new_item(OTHER_PAGES, (int)strlen(OTHER_PAGES), -1);
            if (section < 0) {
                return -1;
            }
            nav[last].next = section;
        }
        if (add_page(found[i]) != 0 || (it = new_item("", 0, npages - 1)) < 0) {
            return -1;
        }
        if (prev < 0) {
            nav[section].child = it;
        } else {
            nav[prev].next = it;
        }
        prev = it;
    }

    return 0;
}

int read_pages(int nargs, char** args)
{
    int i;

    pages = arena_alloc(MAX_PAGES * sizeof(page));
    if (nargs == 1 && is_dir(args[0])) {
        int had = read_nav(args[0]);

        if (had < 0) {
            return -1;
        }
        if (had == 0) {
            return others(args[0]);
        }
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
