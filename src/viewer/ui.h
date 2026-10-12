/*
 * The viewer: what its parts share.
 *
 *   main.c     the command line: what it asks for, and what ref returns
 *   nav.c      moving between the views until the user quits
 *   view.c     a topic: the cursor, drawing, keys and history
 *   doc.c      the topic laid out into lines, and questions about it
 *   toc.c      the contents tree
 *   pick.c     lists to choose from: matches, results, search as you type
 *   help.c     the help screen, and messages
 *   batch.c    without the screen: -p prints, -T times
 *   screen.c   drawing, through VDU sequences
 *   input.c    keys, and what each one means
 *   script.c   the scripted build, for the emulator checks (script.h)
 */
#ifndef UI_H
#define UI_H

#include <stdint.h>

#include "refcore.h"

/* --- the book ---------------------------------------------------------------------- */

extern ref_file book;

/* A topic's title, or the title at an offset in the titles section. */
const char* ui_title(unsigned topic);
const char* ui_title_at(uint32_t off);

/* --- the screen ---------------------------------------------------------------------- */
/*
 * The top row is the header, then scr_rows rows of text, numbered from 0,
 * then the status line, row scr_rows. The text rows are inside a margin
 * each side; the header and the status line are edge to edge.
 */

extern int scr_rows;        /* text rows */
extern int scr_cols;        /* the text's width: the screen less its margins */
extern int scr_margin;      /* blank columns each side of the text */
extern int scr_status_cols; /* the status line's width: the screen less its last column,
                               which is painted rather than printed in */

/* Colours by role, mapped to the mode's palette. */
enum {
    C_TEXT, C_EM, C_CODE, C_LINK, C_SEL, C_HEAD, C_NOTE, C_STATUS, C_DIM, C_COUNT
};

void scr_open(void);
void scr_close(void);
void scr_cls(void);                     /* everything blank, and the header drawn */
void scr_set_header(const char* text);  /* the header's title, from the next scr_cls */

/* A row is drawn whole: begin, characters each in its colour, end (which
 * pads it to the width). */
void scr_begin(int row);
void scr_colour(int role);
void scr_add(int ch);
void scr_adds(const char* s);
int scr_x(void);                        /* the column the row has reached */
void scr_end(void);

/* The text rows from row first, scrolled n rows up (n > 0) or down; the
 * rows that come into view are blank, for the caller to draw. A move of
 * more than SCROLL_MAX rows is cheaper redrawn. */
#define SCROLL_MAX 3
void scr_scroll(int first, int n);

/* The VDP's text cursor at x, y (a text row, or the status line), or hidden. */
void scr_cursor(int x, int y, int show);

/* For script.c's read-backs. */
void scr_flush(void);                   /* send what is waiting */
void scr_forget_colour(void);           /* send the next colour, even if the same */
int scr_screen_cols(void);              /* the whole screen's size */
int scr_screen_rows(void);
const char* scr_header(void);

/* --- keys ------------------------------------------------------------------------------ */

typedef struct {
    uint8_t ch;             /* the ASCII it gives, or 0 */
    uint8_t vk;             /* the VDP's virtual key */
    uint8_t mods;           /* MOD_*, as they were when the key went down */
} key;

#define MOD_CTRL 0x01
#define MOD_SHFT 0x02
#define MOD_ALT  0x04

/* The VDP's virtual keys ref uses (fab-gl's VirtualKey). */
#define VK_f           27
#define VK_n           35
#define VK_p           37
#define VK_F           53   /* shifted: MOS reports the shifted letter */
#define VK_N           61
#define VK_P           63
#define VK_ESCAPE      125
#define VK_BACKSPACE   132
#define VK_HOME        133
#define VK_KP_HOME     134
#define VK_END         135
#define VK_KP_END      136
#define VK_TAB         142
#define VK_RETURN      143
#define VK_KP_ENTER    144
#define VK_PAGEUP      146
#define VK_KP_PAGEUP   147
#define VK_PAGEDOWN    148
#define VK_KP_PAGEDOWN 149
#define VK_UP          150
#define VK_KP_UP       151
#define VK_DOWN        152
#define VK_KP_DOWN     153
#define VK_LEFT        154
#define VK_KP_LEFT     155
#define VK_RIGHT       156
#define VK_KP_RIGHT    157
#define VK_F1          159
#define VK_F3          161

/* What a key means, however it was spelled. */
enum {
    K_NONE, K_CHAR,
    K_UP, K_DOWN, K_LEFT, K_RIGHT, K_PGUP, K_PGDN, K_HOME, K_END,
    K_HEAD_PREV, K_HEAD_NEXT,       /* CTRL+Up, CTRL+Down */
    K_LINK_PREV, K_LINK_NEXT,       /* CTRL+Left, CTRL+Right */
    K_TAB, K_BACKTAB,
    K_ENTER, K_ESC, K_BACK,
    K_HISTBACK, K_HISTFWD,          /* ALT+Left, ALT+Right */
    K_FIND,                         /* F3: search as you type */
    K_FIND_HERE, K_FIND_NEXT, K_FIND_PREV, /* CTRL+F, CTRL+N, CTRL+P */
    K_HELP
};

void in_open(void);
void in_close(void);
key in_wait(void);
int in_pending(void);       /* is a key waiting already? */
int in_meaning(key k);      /* K_* */

/* --- the views ---------------------------------------------------------------------- */

/* Where to go next, as the views return it. */
enum { GO_QUIT, GO_VIEW, GO_TOC, GO_FIND, GO_LIST };

/* Moving between the views, starting at one -- GO_VIEW with a topic
 * (opened from from, if a list), GO_TOC, GO_FIND with text -- until the
 * user quits. */
void nav_run(int go, int topic, const char* text, int from);

/* The topic view. Opening a topic keeps the one shown in the history;
 * opened from another view (GO_TOC, GO_LIST, GO_FIND), the history keeps
 * that too, so Backspace goes back there. */
int view_open(unsigned num);
int view_open_from(unsigned num, int from);
int view_loop(void);        /* until the user leaves it: GO_* */
int view_current(void);     /* the topic shown, or -1 */
int view_has(void);

/* The contents, at the node for topic (or the top): a topic chosen, or
 * where to go instead. */
#define TOC_BACK -1         /* Esc: back to the topic */
#define TOC_FIND -2         /* / or F3 */
#define TOC_QUIT -3         /* q */
int toc_loop(int topic);

/* Lists to choose a topic from, n held of total found: a topic, or -1. */
int pick_topics(const char* heading, const ref_match* m, int n, int total, int keys);
int pick_hits(const char* heading, const ref_hit* h, int n, int total);
/* Search as you type, starting from text -- or, with NULL, resumed as it
 * was left. */
int pick_find(const char* text);
/* The last list chosen from, again, its cursor where it was. */
int pick_again(void);

void ui_help(void);
void ui_message(const char* text);      /* until a key */

/* --- without the screen ---------------------------------------------------------- */

/* Text to the console, a newline as CR LF. */
void ui_print(const char* s);

/* -p: a topic laid out to width, or what was found, one a line. */
int batch_print_topic(unsigned num, int width);
void batch_print_matches(const ref_match* m, int n);
void batch_print_hits(const ref_hit* h, int n);

/* -T: the core timed in the machine. */
int batch_time(const char* path, const char* query);

#endif
