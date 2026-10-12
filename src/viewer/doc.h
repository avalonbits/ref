/*
 * The topic being read, laid out into lines, and questions about it: where
 * a link is, how far a heading goes, where the next link or heading is,
 * where some text is. Nothing here knows about the screen or the cursor;
 * view.c asks, and draws.
 */
#ifndef DOC_H
#define DOC_H

#include <stdint.h>

#include "refcore.h"

#define DOC_MAX_LINES 2000

extern ref_line doc_lines[DOC_MAX_LINES];
extern int doc_nlines;

/* Topic num, with what is shown with it, laid out to width. 0, or -1. */
int doc_load(unsigned num, int width);

/* The column of line i's first character, and of its last. */
int doc_first_col(int i);
int doc_last_col(int i);

/* col, kept to where line i has text. */
int doc_clamp(int i, int col);

/* A link: where it starts and the line it ends on. A link that wraps is on
 * several lines; the lines after its first are lit from their start. */
typedef struct {
    uint16_t target;        /* the topic it leads to */
    int first;              /* the line it starts on */
    int first_col;          /* and the column */
    int last;               /* the line it ends on */
} doc_link;

/* The link at column col of line i, if there is one. */
int doc_link_at(int i, int col, doc_link* link);

/* The next link (dir 1) or the one before (-1) after the position, its
 * start into *line and *col; 0 if there is none that way. */
int doc_step_link(int dir, int* line, int* col);

/* The first and last lines of the heading line i is part of (a heading
 * that wraps is several lines), or 0 if it is not a heading. */
int doc_heading(int i, int* first, int* last);

/* The first line of the next heading (dir 1) or the one before from line
 * i, or -1. */
int doc_step_heading(int i, int dir);

/* text, in any case, forward (dir 1) or back from line, wrapping round
 * the topic; never across lines. Columns here count the line's characters
 * from 0, its indent left out: the first tried on line is from. 1, and the
 * match's line and column, if found. */
int doc_find(const char* text, int dir, int line, int from, int* at_line, int* at);

#endif
