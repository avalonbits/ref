/*
 * The screen: a header row with the book's title, the text rows inside a
 * margin each side, and a status line -- the header and the status line
 * edge to edge. Rows are drawn whole and sent to the VDP in one RST 18h
 * call each, colours by role mapped to the mode's palette. Nothing is ever
 * printed in the last column, so the VDP never wraps or scrolls under ref;
 * where a bar must reach it, the cell is painted instead.
 */
#include <string.h>

#include <agon/mos.h>

#include "script.h"
#include "ui.h"

int scr_cols;
int scr_rows;
int scr_margin;
int scr_status_cols;
static int phys_cols;
static int phys_rows;
static int row_phys;                /* the screen row being drawn */
static const char* header = "";     /* the book's title, on the top row */

static void draw_header(void);

/* The screen row of a row: the top row is the header, then the text rows,
 * and the status line is the last. */
static int phys(int row)
{
    return row == scr_rows ? phys_rows - 1 : row + 1;
}

#define ROW_BYTES 600

static char row_buf[ROW_BYTES];
static int row_len;
static int row_x;
static int row_limit;               /* the columns the row may use */
static int row_full;                /* the status row: edge to edge */
static int row_logical;             /* the row being drawn, as callers number it */
static int cur_role = -1;
static int colours;

/* Foreground and background per role, for 16 or more colours, for 4, and
 * for 2. */
static const uint8_t PAL16[C_COUNT][2] = {
    { 7, 0 },     /* C_TEXT   white */
    { 15, 0 },    /* C_EM     bright white */
    { 14, 0 },    /* C_CODE   bright cyan */
    { 10, 0 },    /* C_LINK   bright green */
    { 0, 14 },    /* C_SEL    black on bright cyan */
    { 11, 0 },    /* C_HEAD   bright yellow */
    { 13, 0 },    /* C_NOTE   bright magenta */
    { 0, 7 },     /* C_STATUS black on white */
    { 8, 0 },     /* C_DIM    grey */
};
static const uint8_t PAL4[C_COUNT][2] = {
    { 3, 0 }, { 3, 0 }, { 2, 0 }, { 1, 0 }, { 0, 3 }, { 2, 0 }, { 1, 0 }, { 0, 3 }, { 1, 0 },
};
static const uint8_t PAL2[C_COUNT][2] = {
    { 1, 0 }, { 1, 0 }, { 1, 0 }, { 1, 0 }, { 0, 1 }, { 1, 0 }, { 1, 0 }, { 0, 1 }, { 1, 0 },
};

void scr_flush(void)
{
    if (row_len) {
        mos_puts(row_buf, (uint24_t)row_len, 0);
        row_len = 0;
    }
}

static void put(int b)
{
    if (row_len >= ROW_BYTES) {
        scr_flush();
    }
    row_buf[row_len++] = (char)b;
}

void scr_open(void)
{
    int cols = getsysvar_scrCols();
    int rows = getsysvar_scrRows();

    colours = getsysvar_scrColours();
    if (cols < 20) {
        cols = 80;
    }
    if (rows < 5) {
        rows = 24;
    }
    /* A margin each side: two columns at 80, one at 40. The right one takes
     * in the last column, which must never be written anyway. */
    phys_cols = cols;
    scr_margin = cols >= 60 ? 2 : 1;
    scr_cols = cols - 2 * scr_margin;
    scr_status_cols = cols - 1;
    phys_rows = rows;
    scr_rows = rows - 2;                    /* less the header and the status line */
    cur_role = -1;
    put(23);                                /* the cursor off */
    put(1);
    put(0);
    scr_cls();
}

void scr_close(void)
{
    put(20);                                /* default colours */
    put(12);
    put(23);                                /* the cursor on */
    put(1);
    put(1);
    scr_flush();
}

void scr_cls(void)
{
    scr_colour(C_TEXT);
    put(12);
    scr_flush();
    SCRIPTED(shadow_clear());
    draw_header();
}

void scr_set_header(const char* text)
{
    header = text ? text : "";
}

/* The top row: the book's title, centred, edge to edge in the status
 * line's colours. Not part of the shadow: the transcript names it. */
static void draw_header(void)
{
    int n = (int)strlen(header);
    int x;
    int w = phys_cols - 1;

    if (n > w) {
        n = w;
    }
    scr_colour(C_STATUS);
    put(31);
    put(0);
    put(0);
    for (x = 0; x < (w - n) / 2; x++) {
        put(' ');
    }
    for (; x < (w - n) / 2 + n; x++) {
        put((unsigned char)header[x - (w - n) / 2]);
    }
    for (; x < w; x++) {
        put(' ');
    }
    /* Its last cell painted, as the status line's is. */
    put(28);
    put(phys_cols - 1);
    put(0);
    put(phys_cols - 1);
    put(0);
    put(12);
    put(26);
    scr_colour(C_TEXT);
    scr_flush();
}

void scr_colour(int role)
{
    const uint8_t* c;

    if (role == cur_role) {
        return;
    }
    cur_role = role;
    c = colours >= 16 ? PAL16[role] : colours >= 4 ? PAL4[role] : PAL2[role];
    put(17);
    put(c[0]);
    put(17);
    put(128 + c[1]);
}

void scr_begin(int row)
{
    /* No colour carried from the row before: a row that ended lit would
     * light the next row's padding. */
    if (cur_role == C_SEL || cur_role == C_STATUS) {
        scr_colour(C_TEXT);
    }
    /* The status row runs edge to edge; the text rows sit inside margins. */
    row_full = row == scr_rows;
    row_limit = row_full ? phys_cols - 1 : scr_cols;
    row_phys = phys(row);
    put(31);
    put(row_full ? 0 : scr_margin);
    put(row_phys);
    row_x = 0;
    row_logical = row;
    SCRIPTED(shadow_row(row));
}

void scr_add(int ch)
{
    if (row_x >= row_limit) {
        return;
    }
    put(ch);
    SCRIPTED(shadow_put(row_logical, row_x, ch, cur_role == C_SEL));
    row_x++;
}

void scr_adds(const char* s)
{
    while (*s) {
        scr_add((unsigned char)*s++);
    }
}

void scr_end(void)
{
    if (cur_role != C_STATUS && cur_role != C_SEL) {
        scr_colour(C_TEXT);
    }
    while (row_x < row_limit) {
        scr_add(' ');
    }
    if (row_full) {
        /* The bottom right cell cannot be printed in without the VDP
         * scrolling the screen: clear it instead, as a one-cell viewport,
         * in the row's colours. */
        put(28);
        put(phys_cols - 1);
        put(row_phys);
        put(phys_cols - 1);
        put(row_phys);
        put(12);
        put(26);
    }
    scr_flush();
}

void scr_scroll(int first, int n)
{
    int k;

    /* The text rows from first: a viewport above the status line, scrolled a
     * character row at a time (VDU 23,7: up when n > 0, so the text moves
     * as the reader goes down), then the viewport reset. The rows scrolled
     * in are left for the caller to draw. */
    if (n == 0) {
        return;
    }
    scr_colour(C_TEXT);
    put(28);
    put(0);
    put(phys(scr_rows - 1));
    put(phys_cols - 1);
    put(phys(first));
    for (k = 0; k < (n > 0 ? n : -n); k++) {
        put(23);
        put(7);
        put(0);
        put(n > 0 ? 3 : 2);
        put(0);
    }
    put(26);
    scr_flush();
    SCRIPTED(shadow_scroll(first, n));
}

int scr_x(void)
{
    return row_x;
}

void scr_cursor(int x, int y, int show)
{
    if (show) {
        put(31);
        put(y == scr_rows ? x : x + scr_margin);
        put(phys(y));
    }
    put(23);
    put(1);
    put(show ? 1 : 0);
    scr_flush();
}

void scr_forget_colour(void)
{
    cur_role = -1;
}

int scr_screen_cols(void)
{
    return phys_cols;
}

int scr_screen_rows(void)
{
    return phys_rows;
}

const char* scr_header(void)
{
    return header;
}
