/*
 * md2ref: Markdown pages compiled into a ref document (docs/FORMAT.md).
 *
 *   md2ref [-t <title>] [-w] [-e <file>] <name> [<dir> | <page.md>... | @list]
 *   md2ref [-t <title>] [-w] [-e <file>] -o <file.dat> <dir> | <page.md>... | @list
 *
 * Portable C, like the reader core: it builds for the PC, and for the Agon
 * with agondev, so a program's documentation can be compiled where it is
 * written.
 *
 * One pass, in fixed memory, as aed, acc and zap work: each page is read
 * through a window that holds whole blocks, and each topic is rendered,
 * compressed and written out as soon as its last block is read. What
 * cannot be known yet is settled the way an assembler settles it:
 *
 * - a link to a topic not yet read is a placeholder, patched when the
 *   document is put together (book.c, write.c);
 * - a topic is held, compressed, until its last descendant is read, as
 *   only then is it known whether it is too big to show with them, and so
 *   ends with links to its children (render.c).
 *
 * The text goes to a scratch file beside the document, and the words for
 * the search to another, as the tables that come first in the document
 * are only complete at the end. What memory holds is the outline -- the
 * topics, their titles and anchors, the words and keys -- and one topic.
 *
 *   md2ref.c   the command line, the arenas, and the pass over the pages
 *   input.c    which pages, and a page read a run of whole blocks at a time
 *   parse.c    a run of lines into blocks
 *   inline.c   a block's text: Markdown inlines into ref's markup
 *   book.c     topics, anchors, and links -- placeholders until known
 *   render.c   each topic's markup, tables laid out; held until complete
 *   index.c    the keyword and full-text indexes
 *   lz4w.c     LZ4 compression, a link's placeholder left in the clear
 *   write.c    the scratch files, and the document put together
 */
#ifndef MD2REF_H
#define MD2REF_H

#include <stdint.h>

/* Kept out of its caller, whose frame it would otherwise grow. */
#define NOINLINE __attribute__((noinline))

#define MAX_TOPICS 4000
#define NONE       0xFFFF

/* --- the arenas, and errors ------------------------------------------------- */

/* realloc, or md2ref stops there, saying it is out of memory. */
void* md_realloc(void* p, unsigned n);

/* A 16-bit hash of n bytes of s, seed mixed in, for the tables that find a
 * name or a word again (hash.c): Pearson's, by the table md_pearson, so
 * names that differ only at their end -- one heading's anchor and the next
 * -- land far apart. */
extern const uint8_t md_pearson[256];
unsigned md_hash(unsigned seed, const char* s, unsigned n);

/* n items of size bytes sorted by cmp, those cmp says are the same kept
 * in their order (sort.c): n log n at worst, where qsort can be n squared. */
void md_sort(void* base, unsigned n, unsigned size, int (*cmp)(const void* a, const void* b));

/* The book's arena: the outline, which lasts. */
void* arena_alloc(unsigned n);
char* arena_strndup(const char* s, unsigned n);
char* arena_strdup(const char* s);

/* Where the book's arena is up to, and back to there: everything allocated
 * since freed, for a step's scratch. */
typedef struct {
    unsigned made;          /* chunks made so far */
    unsigned no;            /* the one in use, and how much of it */
    unsigned used;
} arena_mark;
arena_mark arena_here(void);
void arena_back(arena_mark m);

/* Chunks that never move, freed together: an arena of its own, for what
 * lasts as long as something else -- a link's text, as long as the page
 * it waits on is unread. */
typedef struct chunk chunk;
void* chunks_alloc(chunk** chunks, unsigned n);
char* chunks_strndup(chunk** chunks, const char* s, unsigned n);
void chunks_free(chunk** chunks);

/* The source's arena: what parsing makes of the topic being read. */
void* source_alloc(unsigned n);
char* source_strndup(const char* s, unsigned n);

/* An error at a page's line (line 0: the page itself), counted; md2ref
 * stops before writing if there were any. A warning only says so. */
void md_error(const char* file, int line, const char* fmt, ...);
void md_warn(const char* file, int line, const char* fmt, ...);
extern int md_errors;
extern int md_keep_going;   /* -w: an unresolved link is a warning */

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
    block* blocks;          /* while it is read: the topic's so far */
    int nblocks;
    int first_topic;
    int ntopics;
    int done;               /* read: its anchors are known */
    int waiting;            /* the first link waiting for it to be read, or -1 */
    chunk* waiting_text;    /* those links' text, freed once they are settled */
    unsigned patches_from;  /* the first patch made while it was read */
} page;

extern page* pages;
extern int npages;

/* The pages to compile: the files given, or a directory's .md files in
 * name order (index.md or README.md first). 0, or -1. */
int read_pages(int nargs, char** args);
/* A directory made, if it is not there already: any failure shows when a
 * file in it cannot be written. */
void make_dir(const char* path);

/* Page i, read a run of lines at a time. A run ends where a block must:
 * after a blank line, outside a code fence, before a line at the margin
 * that is not a list item -- so it holds whole blocks, and is parsed as
 * the page whole would be. The window grows for a long block, up to a
 * limit; past it, an error. page_next: 1 with a run, 0 at the end, -1 on
 * an error. A run's lines are good until the next call. */
int page_open(int i);
int page_next(line** lines, int* n);
void page_close(void);

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

/* A page about to be read; then each run of its lines into blocks, added
 * to its blocks. */
void parse_begin(page* p);
void parse_run(page* p, const line* L, int n);

/* What a line is, as the parser sees it: for page_next's runs. */
int line_blank(const line* l);
int line_indent(const line* l);
int line_item(const line* l);
int line_fence(const line* l, char* mark);
int line_closes(const line* l, int len, char mark);

/* A table row's cells (pipes inside code spans are not breaks): how many,
 * up to max, each a pointer and a length, spaces trimmed. */
int split_row(const char* s, int len, const char** cells, int* lens, int max);

/* --- inlines ---------------------------------------------------------------------- */

/* Resolves a link: the topic it leads to, -1 (having said why), or PENDING
 * when the topic is not read yet -- then inline_markup leaves a placeholder,
 * and says where with markup_pending. */
#define PENDING (-2)
typedef int (*link_fn)(const page* p, int no, const char* href, int len);

/* Inline Markdown into markup bytes appended at out (size left in room):
 * how many were written. */
int inline_markup(const page* p, int no, const char* s, uint8_t* out, int room, link_fn links);
/* The plain text of inline Markdown, as a reader sees it, into out. */
void inline_plain(const char* s, char* out, int size);
/* The code spans of inline Markdown, in turn: the next from *at into out,
 * *at moved past it; or 0. */
int inline_code(const char* s, int* at, char* out, int size);

/* --- the book ---------------------------------------------------------------------- */

typedef struct {
    const char* title;      /* plain text */
    const char* heading;    /* the heading's inline Markdown, or NULL for a page's top */
    const char* id;         /* its {#id}, or NULL */
    const char* anchor;     /* its anchor, once its page is read */
    unsigned long total;    /* its and its descendants' markup's length */
    unsigned long text_at;  /* where its text is, in the text section */
    /* 16 bits each, as there are at most MAX_TOPICS and a topic's markup is
     * at most 64 KB: an outline of thousands of topics fits the Agon. */
    int16_t page;
    int16_t made;           /* its number as made: what the words file and the
                               links to children hold, as a topic made later
                               can come before it (book_heading) */
    int16_t level;
    int16_t parent;         /* -1 at the top */
    int16_t first_child;    /* the contents: -1 for none */
    int16_t last_child;
    int16_t next;           /* next sibling, or -1 */
    int16_t nchildren;
    int16_t descendants;
    int16_t span;           /* how many after it are shown with it */
    uint16_t own;           /* its markup's length, without links to its children */
    uint16_t len;           /* its markup's length, as written */
    uint16_t packed;        /* compressed */
} topic;

/* The topics, in blocks of 256 that never move once made: a pointer to one
 * holds, and the table grows without a realloc, which on the Agon holds
 * the old and the new at once -- three times the table, at the size where
 * the heap can least spare it. */
#define TOPIC_BLOCK 256
extern topic* topic_block[(MAX_TOPICS + 1 + TOPIC_BLOCK - 1) / TOPIC_BLOCK];
#define TOPIC(t) (topic_block[(unsigned)(t) >> 8][(uint8_t)(t)])
extern int ntopics;
extern const char* book_title;

/* A page begun; each block that starts a topic -- a heading, not in a note,
 * quote or list -- or, first, the page's top topic: titled by its file name,
 * unless the page opens with its only level-1 heading, which is then its
 * top. Until a second level-1 heading shows it is not the only one, it is
 * taken to be; then the page's top is made, before it, and the topics
 * after it move up one (render.c is told). The new topic's number. */
void book_page(int pi);
int book_starts_topic(const block* b);
int book_top(int pi, const block* first);
int book_heading(int pi, const block* b);
/* A page read: its anchors, as Python-Markdown makes them, from its
 * headings and the {#id}s set in it (book_ids, as its topics are read). */
void book_ids(int pi, int t, const block* b);
void book_page_done(int pi);
/* The contents done, the title chosen. 0, or -1 if there is nothing. */
int book_done(const char* title);
/* A link's target topic, -1 (with an error or a warning), or PENDING. */
int resolve_link(const page* p, int no, const char* href, int len);
/* The link resolve_link last left PENDING; and that link's placeholder
 * written to the text file, as patch k. When the page it leads to is read
 * (book_page_done), its topic goes into the patch -- NONE for one that
 * leads nowhere, an error or with -w a warning -- and the link is freed.
 * resolve_pending: 0, or -1 if there were errors. */
int last_pending(void);
void link_placed(int link, unsigned k);
int resolve_pending(void);

/* --- markup ----------------------------------------------------------------------- */

/* A topic read: blocks first..n-1 (from its heading, when it has one)
 * rendered, its words taken, compressed, and held; then, as each is
 * complete -- every descendant read -- links to its children added if it
 * is too big to show with them, and written. render_complete(parent) as
 * the next topic begins; render_complete(-1) at a page's end. */
void render_topic(int t, const block* b, int n);
void render_complete(int parent);
/* A page's top topic, made late at first: the topics from it on move up
 * one, and it is held beneath those held, as their ancestor. */
void render_top(int t);
/* At the end: each topic's number from its number as made. */
extern int* final_of;
void number_topics(void);
/* Every topic written: the most any shows, with those it spans. 0, or -1. */
int render_done(void);
/* Where in the markup being rendered a link's placeholder is. */
void markup_pending(int at);
/* Text into the Agon's character set, from UTF-8: into out. */
int agon_char(const char** s);

extern unsigned view_size;
extern unsigned max_packed;

/* --- indexes --------------------------------------------------------------------- */

/* A topic's words, from its markup's plain text (and in_title, its title's):
 * between words_begin and words_end, for each topic, or more of one. */
void words_begin(int t);
/* The words of a topic's markup, n bytes of whole blocks. */
void words_markup(const uint8_t* m, unsigned n);
void words_text(const char* plain, int in_title);
void words_end(void);
/* The keys, from every topic's heading and title. */
void make_keys(void);

typedef struct {
    uint8_t* data;          /* front-coded blocks (the keys'; the words' are
                               written as they are made) */
    unsigned long len;
    uint8_t* dir;           /* the directory */
    unsigned dir_len;
    unsigned blocks;
    unsigned count;
} section;

extern section keys_section;
extern section words_section;
extern const uint8_t* norm_settings;
extern unsigned norm_len;

/* The words section, from the words file, into the document with emit;
 * words_section filled in. 0, or -1. */
int write_words(void (*emit)(const void* p, unsigned n));

/* --- writing ------------------------------------------------------------------------ */

/* LZ4 block of n bytes at src into dst: its length. The two bytes at each
 * of fixed[0..nfixed-1] (ascending) are left as literals, neither matched
 * nor matched from, and where each lands in dst goes in fixed_at. */
unsigned lz4_compress(const uint8_t* src, unsigned n, uint8_t* dst, unsigned room,
                      const unsigned* fixed, int nfixed, unsigned* fixed_at);

/* The scratch files, beside the document: <out>~t for the text, <out>~w for
 * the words. 0, or -1. */
int scratch_open(const char* out);
void scratch_close(void);
/* A topic's compressed text, at the end of the text: where it went. */
unsigned long write_text(const uint8_t* p, unsigned n);
/* A placeholder's link, in a topic's marks: a pending link's number, or
 * LINK_TO(made) for a topic's number as made. Its place in the text file,
 * once written, is a patch: its index, and the number to put there or
 * what it waits for, settled with set_patch. */
#define LINK_TO(made) (-2 - (made))
#define PATCH_WAITING 0x10000UL      /* a link's last placeholder, its page not read yet */
#define PATCH_MADE    0x20000UL      /* + a topic's number as made, settled as its page is */
#define PATCH_CHAIN   0x40000UL      /* + the patch of the same link's placeholder before */
unsigned add_patch(unsigned long at, unsigned value);
void set_patch(unsigned k, unsigned value);
unsigned patch_count(void);
unsigned patch_value(unsigned k);
/* The words file: records appended; then read back from the start. */
void words_put(const uint8_t* p, unsigned n);
int words_rewind(void);
unsigned words_get(uint8_t* p, unsigned n);
/* <out>~r, the words' sorted runs: written; then read from anywhere. */
int runs_open(void);
void runs_put(const uint8_t* p, unsigned n);
unsigned long runs_size(void);
int runs_reread(void);
unsigned runs_get(unsigned long at, uint8_t* p, unsigned n);
/* The document, put together. 0, or -1. */
int write_book(const char* path);

#endif
