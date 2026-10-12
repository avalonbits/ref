/*
 * The documents installed: every <name>.dat in /config/ref/docs, each with
 * its title. Plain `ref` shows them to choose from; `ref -p` prints them.
 */
#include <stdio.h>
#include <string.h>

#include <agon/mos.h>

#include "script.h"
#include "ui.h"

#define SHELF_MAX  32
#define NAME_MAX   8
#define TITLE_SHOW 64

typedef struct {
    char name[NAME_MAX + 1];
    char title[TITLE_SHOW + 1];
} document;

static document docs[SHELF_MAX];
static int ndocs;
static int cursor;

const char* shelf_path(const char* name)
{
    static char path[sizeof SHELF_DIR + NAME_MAX + 8];

    sprintf(path, "%s/%s.dat", SHELF_DIR, name);

    return path;
}

int shelf_valid_name(const char* name)
{
    int n = 0;

    for (; *name; name++, n++) {
        if (!((*name >= 'a' && *name <= 'z') || (*name >= '0' && *name <= '9'))) {
            return 0;
        }
    }

    return n >= 1 && n <= NAME_MAX;
}

/* A file's name as a document's, lower case without .dat, if it is one. */
static int doc_name(const char* fname, char* name)
{
    int n = (int)strlen(fname);
    int k;

    if (n < 5 || n - 4 > NAME_MAX) {
        return 0;
    }
    if (strcmp(fname + n - 4, ".dat") != 0 && strcmp(fname + n - 4, ".DAT") != 0) {
        return 0;
    }
    for (k = 0; k < n - 4; k++) {
        int c = (unsigned char)fname[k];

        name[k] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
    }
    name[n - 4] = 0;

    return shelf_valid_name(name);
}

/* A document's title, from its header: the book's own, or else its name. */
static void read_title(document* d)
{
    static ref_file f;
    static char title[REF_TITLE_MAX + 1];

    strcpy(d->title, d->name);
    if (ref_open(&f, shelf_path(d->name)) != REF_OK) {
        strcpy(d->title, "(cannot be read)");

        return;
    }
    if (ref_book_title(&f, title, sizeof title) >= 0) {
        strncpy(d->title, title, TITLE_SHOW);
        d->title[TITLE_SHOW] = 0;
    }
    ref_close(&f);
}

int shelf_scan(void)
{
    static DIR dir;
    static FILINFO fi;
    int i;
    int j;

    /* Every entry, read in turn: ffs_dfindfirst's pattern matching has
     * been seen to find nothing. */
    ndocs = 0;
    if (ffs_dopen(&dir, SHELF_DIR) != 0) {
        return 0;
    }
    while (ndocs < SHELF_MAX && ffs_dread(&dir, &fi) == 0 && fi.fname[0]) {
        if (!(fi.fattrib & AM_DIR) && doc_name(fi.fname, docs[ndocs].name)) {
            ndocs++;
        }
    }
    ffs_dclose(&dir);

    /* In order of name; few enough that sorting by insertion will do. */
    for (i = 1; i < ndocs; i++) {
        document d = docs[i];

        for (j = i; j > 0 && strcmp(docs[j - 1].name, d.name) > 0; j--) {
            docs[j] = docs[j - 1];
        }
        docs[j] = d;
    }
    for (i = 0; i < ndocs; i++) {
        read_title(&docs[i]);
    }

    return ndocs;
}

void shelf_print(void)
{
    static char line[NAME_MAX + TITLE_SHOW + 8];
    int i;

    for (i = 0; i < ndocs; i++) {
        sprintf(line, "%-8s  %s\n", docs[i].name, docs[i].title);
        ui_print(line);
    }
}

static void draw_row(int r)
{
    scr_begin(r);
    if (r < ndocs) {
        scr_colour(r == cursor ? C_SEL : C_CODE);
        scr_add(' ');
        scr_adds(docs[r].name);
        while (scr_x() < NAME_MAX + 3) {
            scr_add(' ');
        }
        scr_colour(r == cursor ? C_SEL : C_TEXT);
        scr_adds(docs[r].title);
    }
    scr_colour(r == cursor && r < ndocs ? C_SEL : C_TEXT);
    scr_end();
}

static void draw(void)
{
    int r;

    for (r = 0; r < scr_rows; r++) {
        draw_row(r);
    }
    scr_begin(scr_rows);
    scr_colour(C_STATUS);
    scr_adds(" Documents   Enter: open   Esc: quit");
    scr_end();
    LOG("shelf row %d of %d %s", cursor, ndocs, ndocs ? docs[cursor].name : "");
}

const char* shelf_choose(void)
{
    if (cursor >= ndocs) {
        cursor = 0;
    }
    scr_set_header("ref: the documents on this card");
    scr_cls();
    draw();
    for (;;) {
        key k = in_wait();
        int m = in_meaning(k);
        int was = cursor;

        if (m == K_UP && cursor > 0) {
            cursor--;
        } else if (m == K_DOWN && cursor + 1 < ndocs) {
            cursor++;
        } else if (m == K_HOME) {
            cursor = 0;
        } else if (m == K_END && ndocs) {
            cursor = ndocs - 1;
        } else if (m == K_ENTER && ndocs) {
            return docs[cursor].name;
        } else if (m == K_ESC || (m == K_CHAR && k.ch == 'q')) {
            return NULL;
        }
        if (cursor != was) {
            draw_row(was);
            draw_row(cursor);
            LOG("shelf row %d of %d %s", cursor, ndocs, docs[cursor].name);
        }
    }
}
