/*
 * The viewer: what its parts share.
 *
 *   screen.c   drawing, through VDU sequences; a shadow of the screen in
 *              the scripted build, for the tests
 *   input.c    keys: MOS's key events, or a script on the card
 *   view.c     a topic: its lines, links, scrolling and history
 *   toc.c      the contents tree
 *   pick.c     a list to choose from: matches, results, search as you type
 *   main.c     the command line, and what runs between the three views
 */
#ifndef UI_H
#define UI_H

#include <stdint.h>

#include "refcore.h"

/* --- the file ---------------------------------------------------------------- */

extern ref_file book;

/* A topic's title: from the titles section, loaded once. */
const char* ui_title(unsigned topic);
const char* ui_title_at(uint32_t off);

/* --- the screen -------------------------------------------------------------- */

extern int scr_cols;        /* the width text is laid out to: the screen less its margins */
extern int scr_margin;      /* blank columns each side */
extern int scr_rows;        /* rows for text: the screen less the status line */

/* Colours by role, mapped to the mode's palette. */
enum {
    C_TEXT, C_EM, C_CODE, C_LINK, C_SEL, C_HEAD, C_NOTE, C_STATUS, C_DIM, C_COUNT
};

void scr_open(void);
void scr_close(void);
void scr_cls(void);
/* A row of text: from x, styled by colour role per character. Rows are
 * drawn whole: begin, add, end (which pads to the width). */
void scr_begin(int row);
void scr_colour(int role);
void scr_add(int ch);
void scr_adds(const char* s);
void scr_end(void);
int scr_x(void);
/* Scroll the text rows from row first down to the status line, n rows up
 * (n > 0) or down; the rows that come into view are blank, for the caller
 * to draw. */
void scr_scroll(int first, int n);
/* Rows moved further than this are redrawn rather than scrolled. */
#define SCROLL_MAX 3             /* the column the row has reached */
/* The cursor, shown at x, y, or hidden. */
void scr_cursor(int x, int y, int show);

#ifdef REF_SCRIPT
/* Write what the screen shows now to the transcript, under a heading. */
void scr_dump(const char* why);
void scr_vdp_dump(const char* why);
void script_log(const char* fmt, ...);
void script_close(void);
#endif

/* --- keys -------------------------------------------------------------------- */

typedef struct {
    uint8_t ch;             /* the ASCII it gives, or 0 */
    uint8_t vk;             /* the VDP's virtual key */
    uint8_t mods;           /* MOD_* */
} key;

#define MOD_CTRL 0x01
#define MOD_SHFT 0x02
#define MOD_ALT  0x04

/* The VDP's virtual keys ref uses (fab-gl's VirtualKey). */
#define VK_ESCAPE    125
#define VK_DELETE    130
#define VK_BACKSPACE 132
#define VK_HOME      133
#define VK_KP_HOME   134
#define VK_END       135
#define VK_KP_END    136
#define VK_TAB       142
#define VK_RETURN    143
#define VK_KP_ENTER  144
#define VK_PAGEUP    146
#define VK_KP_PAGEUP 147
#define VK_PAGEDOWN  148
#define VK_KP_PAGEDOWN 149
#define VK_UP        150
#define VK_KP_UP     151
#define VK_DOWN      152
#define VK_KP_DOWN   153
#define VK_LEFT      154
#define VK_KP_LEFT   155
#define VK_RIGHT     156
#define VK_KP_RIGHT  157
#define VK_F1        159
#define VK_F3        161
#define VK_f         27
#define VK_n         35
#define VK_p         37
#define VK_F         53         /* shifted: MOS reports the shifted letter */
#define VK_N         61
#define VK_P         63

/* What a key means, whichever of its spellings was pressed. */
enum {
    K_NONE, K_CHAR, K_UP, K_DOWN, K_LEFT, K_RIGHT, K_PGUP, K_PGDN, K_HOME, K_END,
    K_ENTER, K_ESC, K_BACK, K_TAB, K_BACKTAB, K_HISTBACK, K_HISTFWD, K_FIND, K_HELP,
    K_DELETE, K_HEAD_NEXT, K_HEAD_PREV, K_LINK_NEXT, K_LINK_PREV, K_FIND_HERE,
    K_FIND_NEXT, K_FIND_PREV
};

void in_open(void);
void in_close(void);
key in_wait(void);
int in_pending(void);
int in_meaning(key k);

/* --- the views ---------------------------------------------------------------- */

/* What a view returns to main's loop. */
enum { GO_QUIT, GO_VIEW, GO_TOC, GO_FIND };

/* Open topic num in the topic view, keeping where we were in its history. */
int view_open(unsigned num);
int view_loop(void);
int view_current(void);     /* the topic shown, or -1 */
int view_has(void);

/* The contents, at the node for topic (or the top): a topic chosen, or -1. */
int toc_loop(int topic);

/* A list of topics to choose from, under a heading: a topic, or -1. */
int pick_topics(const char* heading, const ref_match* m, int n, int keys);
int pick_hits(const char* heading, const ref_hit* h, int n);
/* Search as you type, starting from text: a topic, or -1. */
int pick_find(const char* text);

void ui_message(const char* text);
void ui_help(void);

#endif
