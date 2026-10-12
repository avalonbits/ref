/*
 * md2ref: Markdown pages compiled into a ref document (docs/FORMAT.md).
 *
 *   md2ref [-t <title>] [-w] <name> [<dir> | <page.md>...]
 *   md2ref [-t <title>] [-w] -o <file.dat> <dir> | <page.md>...
 *
 * Portable C, like the reader core: it builds for the PC, and for the Agon
 * with agondev, so a program's documentation can be compiled where it is
 * written. To fit the Agon it goes through the pages twice, a page at a
 * time: first for their topics and anchors, then -- every link's target
 * known -- for their markup. Only one page's text is ever held; the
 * topics, their markup and the indexes are what last.
 *
 *   md2ref.c   the command line, and the steps in order
 *   input.c    reading the pages, and listing a directory
 *   parse.c    a page's lines into blocks
 *   inline.c   a block's text: Markdown inlines into ref's markup
 *   book.c     topics, anchors, links and the contents
 *   render.c   each topic's markup, tables laid out
 *   index.c    the keyword and full-text indexes
 *   lz4w.c     LZ4 compression
 *   write.c    the file
 */
#ifndef MD2REF_H
#define MD2REF_H

#include <stdint.h>

/* Kept out of its caller, whose frame it would otherwise grow. */
#define NOINLINE __attribute__((noinline))

/* --- the arenas, and errors ------------------------------------------------- */

/* realloc, or md2ref stops there, saying it is out of memory. */
void* md_realloc(void* p, unsigned n);

/* The book's arena, which lasts. */
void* arena_alloc(unsigned n);
char* arena_strndup(const char* s, unsigned n);
char* arena_strdup(const char* s);

/* Where the arena is up to, and back to there: everything allocated since
 * freed, for a step's scratch. */
typedef struct {
    unsigned made;          /* chunks made so far */
    unsigned no;            /* the one in use, and how much of it */
    unsigned used;
} arena_mark;
arena_mark arena_here(void);
void arena_back(arena_mark m);

/* The source's arena: a page's text, and what parsing makes of it, dropped
 * when md2ref is done with the page -- its lines and blocks (which are
 * malloc'd, as they grow) are gone then.
 * md2ref goes through the pages twice, a page at a time, so that only one
 * is ever held. */
void* source_alloc(unsigned n);
char* source_strndup(const char* s, unsigned n);
void drop_source(void);

/* An error at a page's line (line 0: the page itself), counted; md2ref
 * stops before writing if there were any. A warning only says so. */
void md_error(const char* file, int line, const char* fmt, ...);
void md_warn(const char* file, int line, const char* fmt, ...);
extern int md_errors;
extern int md_keep_going;   /* -w: an unresolved link is a warning */
extern int md_quiet;        /* say nothing: a second pass over what was said */

/* --- the pages ------------------------------------------------------------------ */

typedef struct {
    const char* text;       /* the line, from where its block's content starts */
    int len;
    int no;                 /* its number in the page, from 1 */
} line;

typedef struct block block;

typedef struct {
    const char* path;       /* as given */
    const char* name;       /* its file name, as links name it */
    line* lines;            /* while it is parsed */
    int nlines;
    block* blocks;          /* the page parsed, while it is loaded */
    int nblocks;
    int outlined;           /* how many blocks the first pass found */
    int first_topic;
    int ntopics;
} page;

extern page* pages;
extern int npages;

/* The pages to compile: the files given, or a directory's .md files in
 * name order (index.md or README.md first). 0, or -1. */
int read_pages(int nargs, char** args);
/* A directory made, if it is not there already: any failure shows when a
 * file in it cannot be written. */
void make_dir(const char* path);
/* Page i read and parsed into the source's arena: its lines and blocks,
 * until the arena is dropped. 0, or -1. */
int load_page(int i);

/* --- blocks ----------------------------------------------------------------------- */

#define B_PARA  1           /* as the markup's kinds (docs/FORMAT.md) */
#define B_HEAD  2
#define B_PRE   3
#define B_RULE  4
#define B_TABLE 5           /* rendered as PRE, laid out */

struct block {
    uint8_t kind;
    uint8_t level;          /* a heading's */
    uint8_t space;          /* a blank line before */
    uint8_t note;           /* inside a note or a quote */
    uint8_t indent;
    uint8_t hang;
    uint8_t em;             /* all of it emphasised: a note's title */
    uint8_t nids;
    const char* marker;     /* a list item's, before its text, or NULL */
    const char* text;       /* inline Markdown; code as it is; a table's rows, '\n' apart */
    int no;                 /* its first line */
    const char* id;         /* a heading's {#id}, or NULL */
    const char** ids;       /* the {#id}s set on a paragraph or a table's cells */
};

/* A page's lines into blocks. */
void parse_page(page* p);

/* A table row's cells (pipes inside code spans are not breaks): how many,
 * up to max, each a pointer and a length, spaces trimmed. */
int split_row(const char* s, int len, const char** cells, int* lens, int max);

/* --- inlines ---------------------------------------------------------------------- */

/* Resolves a link: the topic it leads to, or -1 (and says why). */
typedef int (*link_fn)(const page* p, int no, const char* href, int len);

/* Inline Markdown into markup bytes appended at out (size left in room):
 * how many were written. */
int inline_markup(const page* p, int no, const char* s, uint8_t* out, int room, link_fn links);
/* The plain text of inline Markdown, as a reader sees it, into out. */
void inline_plain(const char* s, char* out, int size);
/* The code spans of inline Markdown, in turn: the i-th into out, or 0. */
int inline_code(const char* s, int i, char* out, int size);

/* --- the book ---------------------------------------------------------------------- */

typedef struct {
    const char* title;      /* plain text */
    const char* heading;    /* the heading's inline Markdown, or NULL for a page's top */
    int page;
    int level;
    int parent;             /* -1 at a page's top */
    int first_block;        /* its text: blocks first_block .. end_block - 1 */
    int end_block;
    int span;
    int nchildren;
    int* children;
    const char* anchor;
} topic;

extern topic* topics;
extern int ntopics;
extern const char* book_title;

/* The loaded page's topics, from its headings, and their anchors; then,
 * every page done, the contents. 0, or -1. */
void book_page(int pi);
int book_done(const char* title);
/* A link's target topic, or -1 (with an error or a warning). */
int resolve_link(const page* p, int no, const char* href, int len);

/* --- markup ----------------------------------------------------------------------- */

/* A topic's markup, kept compressed as the file holds it. */
typedef struct {
    uint8_t* packed;        /* an LZ4 block */
    unsigned packed_len;
    unsigned len;           /* the markup's own length */
} text;

extern text* markup;        /* each topic's, its menu included */
extern unsigned view_size;

/* The loaded page's topics' markup; then, every page done, the spans, and
 * the menus of topics that show their children as links. 0, or -1. */
void render_page(int pi);
int render_done(void);
/* Topic t's markup into out, which has room for markup[t].len. */
void unpack(int t, uint8_t* out);
/* Markup's plain text, controls dropped, lines ended by '\n'. */
unsigned markup_plain(const uint8_t* m, unsigned len, char* out, unsigned size);
/* Text into the Agon's character set, from UTF-8: into out. */
int agon_char(const char** s);

/* --- indexes --------------------------------------------------------------------- */

/* Keys from every topic's heading and title, and words from its markup.
 * 0, or -1. */
int make_indexes(void);

typedef struct {
    uint8_t* data;          /* front-coded blocks */
    unsigned len;
    uint8_t* dir;           /* the directory */
    unsigned dir_len;
    unsigned blocks;
    unsigned count;
} section;

extern section keys_section;
extern section words_section;
extern const uint8_t* norm_settings;
extern unsigned norm_len;

/* --- writing ------------------------------------------------------------------------ */

/* LZ4 block of n bytes at src into dst: its length. */
unsigned lz4_compress(const uint8_t* src, unsigned n, uint8_t* dst, unsigned room);

int write_book(const char* path);

#endif
