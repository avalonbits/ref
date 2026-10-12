/*
 * The pages: which they are -- the files given, or a directory's .md files
 * in name order, index.md or README.md first -- and, one at a time, a
 * page's file read whole, split into lines with tabs expanded to stops of
 * four, and parsed into the source's arena.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef AGONDEV
#include <agon/mos.h>
#else
#include <dirent.h>
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

/* A file's lines, \r dropped and tabs expanded. */
static void split_lines(page* p, char* text)
{
    int n = 1;
    int k = 0;
    char* s;

    for (s = text; *s; s++) {
        n += *s == '\n';
    }
    p->lines = md_realloc(NULL, (unsigned)n * (unsigned)sizeof(line));
    s = text;
    while (*s) {
        char* e = strchr(s, '\n');
        int len = e ? (int)(e - s) : (int)strlen(s);
        int tabs = 0;
        int j;

        while (len > 0 && s[len - 1] == '\r') {
            len--;
        }
        for (j = 0; j < len; j++) {
            tabs += s[j] == '\t';
        }
        if (tabs) {
            char* t = source_alloc((unsigned)(len + tabs * 4 + 1));
            int col = 0;

            for (j = 0; j < len; j++) {
                if (s[j] == '\t') {
                    do {
                        t[col++] = ' ';
                    } while (col % 4);
                } else {
                    t[col++] = s[j];
                }
            }
            t[col] = 0;
            p->lines[k].text = t;
            p->lines[k].len = col;
        } else {
            p->lines[k].text = s;
            p->lines[k].len = len;
        }
        p->lines[k].no = k + 1;
        k++;
        if (!e) {
            break;
        }
        s = e + 1;
    }
    p->nlines = k;
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

    return 0;
}

int load_page(int i)
{
    page* p = &pages[i];
    FILE* f = fopen(p->path, "rb");
    long size;
    char* text;

    if (!f) {
        md_error(p->path, 0, "cannot be read");

        return -1;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    text = md_realloc(NULL, (unsigned)size + 1);
    if (size > 0 && fread(text, 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        free(text);
        md_error(p->path, 0, "cannot be read");

        return -1;
    }
    fclose(f);
    text[size] = 0;
    split_lines(p, text);
    parse_page(p);
    /* Every block has its own copy of its text: the file and its lines can
     * go now, before the page is used. */
    free(p->lines);
    p->lines = NULL;
    free(text);

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

static int is_dir(const char* path)
{
    return !ends_md(path);
}

int read_pages(int nargs, char** args)
{
    int i;
    int j;

    pages = arena_alloc(MAX_PAGES * sizeof(page));
    if (nargs == 1 && is_dir(args[0])) {
        if (list_dir(args[0]) != 0) {
            md_error(args[0], 0, "not a directory of .md pages");

            return -1;
        }
        for (i = 1; i < nfound; i++) {
            char* x = found[i];

            for (j = i; j > 0 && before(x, found[j - 1]); j--) {
                found[j] = found[j - 1];
            }
            found[j] = x;
        }
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
