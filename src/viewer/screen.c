/*
 * Drawing: whole rows at a time, each sent to the VDP in one RST 18h call,
 * colours by role mapped to the mode's palette. Nothing is ever written in
 * the last column, so the VDP never wraps or scrolls under us.
 *
 * The scripted build keeps a shadow of every row, and writes it to the
 * transcript on request: what the tests read back.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <agon/mos.h>

#include "ui.h"

int scr_cols;
int scr_rows;
int scr_margin;
int scr_status_cols;
static int phys_cols;

#define ROW_BYTES 600

static char row_buf[ROW_BYTES];
static int row_len;
static int row_x;
static int row_limit;               /* the columns the row may use */
static int row_full;                /* the status row: edge to edge */
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

#ifdef REF_SCRIPT
#define SHADOW_COLS 132
#define SHADOW_ROWS 64
static char shadow[SHADOW_ROWS][SHADOW_COLS];
static uint8_t shadow_lit[SHADOW_ROWS];     /* rows with something in C_SEL */
static int shadow_row;
static FILE* transcript;

void script_log(const char* fmt, ...)
{
    va_list ap;

    /* Appended: under hub, one card runs ref more than once. */
    if (!transcript) {
        transcript = fopen("/ref.out", "a");
    }
    if (!transcript) {
        transcript = fopen("/ref.out", "w");    /* agondev's "a" needs the file */
    }
    if (!transcript) {
        return;
    }
    va_start(ap, fmt);
    vfprintf(transcript, fmt, ap);
    va_end(ap);
    fputc('\n', transcript);
    fflush(transcript);
}

void script_close(void)
{
    if (transcript) {
        fclose(transcript);
        transcript = NULL;
    }
}

void scr_dump(const char* why)
{
    int r;

    script_log("== %s", why);
    for (r = 0; r <= scr_rows && r < SHADOW_ROWS; r++) {
        int w = r == scr_rows ? scr_status_cols : scr_cols;
        int n = w < SHADOW_COLS ? w : SHADOW_COLS;

        while (n > 0 && shadow[r][n - 1] == ' ') {
            n--;
        }
        script_log("%.*s", n, shadow[r]);
    }
    {
        /* Which text rows are lit -- what the cursor is on -- after the dump,
         * so a comparison of the dump with the VDP's screen is not upset. */
        static char lit[SHADOW_ROWS * 3 + 1];
        int len = 0;

        for (r = 0; r < scr_rows && r < SHADOW_ROWS; r++) {
            if (shadow_lit[r]) {
                len += sprintf(lit + len, " %d", r);
            }
        }
        lit[len] = 0;
        script_log("lit %s:%s", why, lit);
    }
}

/* What the VDP has on its screen, read back a character at a time with
 * VDU 23,0,&83 -- every column, the last one too, which ref never writes.
 * Only the full emulator's VDP answers; the CLI emulator's stand-in does not. */
static int last_role;
static void flush(void);

/* The colour of the pixel in the middle of character cell x, y, as the VDP
 * reports it (VDU 23,0,&84, in OS coordinates): what the full emulator's
 * checks read where a character cannot be read back. */
void scr_vdp_pixel(int x, int y)
{
    int ox = (x * 2 + 1) * 1280 / (phys_cols * 2);
    int oy = 1023 - (y * 2 + 1) * 1024 / ((scr_rows + 1) * 2);
    unsigned long c;

    flush();
    mos_clearvdpflags(vdp_pflag_point);
    putch(23);
    putch(0);
    putch(0x84);
    putch(ox & 0xff);
    putch(ox >> 8);
    putch(oy & 0xff);
    putch(oy >> 8);
    mos_waitforvdpflags(vdp_pflag_point);
    c = (unsigned long)getsysvar_scrpixel();
    script_log("pixel %d,%d %06lx", x, y, c & 0xffffffUL);
}

void scr_vdp_dump(const char* why)
{
    static char row[SHADOW_COLS + 1];
    int r;
    int x;

    script_log("== vdp %s", why);
    for (r = 0; r <= scr_rows && r < SHADOW_ROWS; r++) {
        int n = 0;

        for (x = 0; x < phys_cols && x < SHADOW_COLS; x++) {
            int c = 0;
            int tries;

            /* The VDP matches a cell against the font in the current text
             * colours: try each role's, the last that worked first. Only a
             * printable answer counts -- a blank cell read in colours
             * whose foreground is its background matches a solid block. */
            for (tries = 0; tries < C_COUNT && !(c >= 0x20 && c < 0x7f); tries++) {
                if (tries) {
                    last_role = (last_role + 1) % C_COUNT;
                }
                cur_role = -1;
                scr_colour(last_role);
                flush();
                mos_clearvdpflags(vdp_pflag_scrchar);
                putch(23);
                putch(0);
                putch(0x83);
                putch(x & 0xff);
                putch(x >> 8);
                putch(r & 0xff);
                putch(r >> 8);
                mos_waitforvdpflags(vdp_pflag_scrchar);
                c = getsysvar_scrchar();
            }
            row[n++] = (char)(c >= 0x20 && c < 0x7f ? c : c ? '?' : '.');
        }
        while (n > 0 && row[n - 1] == ' ') {
            n--;
        }
        row[n] = 0;
        script_log("%s", row);
    }
    cur_role = -1;
}
#endif

static void flush(void)
{
    if (row_len) {
        mos_puts(row_buf, (uint24_t)row_len, 0);
        row_len = 0;
    }
}

static void put(int b)
{
    if (row_len >= ROW_BYTES) {
        flush();
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
    scr_rows = rows - 1;
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
    flush();
}

void scr_cls(void)
{
    scr_colour(C_TEXT);
    put(12);
    flush();
#ifdef REF_SCRIPT
    memset(shadow, ' ', sizeof shadow);
    memset(shadow_lit, 0, sizeof shadow_lit);
#endif
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
    put(31);
    put(row_full ? 0 : scr_margin);
    put(row);
    row_x = 0;
#ifdef REF_SCRIPT
    shadow_row = row;
    if (row < SHADOW_ROWS) {
        shadow_lit[row] = 0;
    }
#endif
}

void scr_add(int ch)
{
    if (row_x >= row_limit) {
        return;
    }
    put(ch);
#ifdef REF_SCRIPT
    if (shadow_row < SHADOW_ROWS && row_x < SHADOW_COLS) {
        shadow[shadow_row][row_x] = (char)ch;
        if (cur_role == C_SEL) {
            shadow_lit[shadow_row] = 1;
        }
    }
#endif
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
        put(scr_rows);
        put(phys_cols - 1);
        put(scr_rows);
        put(12);
        put(26);
    }
    flush();
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
    put(scr_rows - 1);
    put(phys_cols - 1);
    put(first);
    for (k = 0; k < (n > 0 ? n : -n); k++) {
        put(23);
        put(7);
        put(0);
        put(n > 0 ? 3 : 2);
        put(0);
    }
    put(26);
    flush();
#ifdef REF_SCRIPT
    {
        int h = scr_rows - first;
        int m = n > 0 ? n : -n;
        int r;

        if (m > h) {
            m = h;
        }
        if (n > 0) {
            memmove(shadow_lit + first, shadow_lit + first + m, (size_t)(h - m));
            memset(shadow_lit + scr_rows - m, 0, (size_t)m);
            memmove(shadow[first], shadow[first + m], (size_t)(h - m) * SHADOW_COLS);
            for (r = scr_rows - m; r < scr_rows; r++) {
                memset(shadow[r], ' ', SHADOW_COLS);
            }
        } else {
            memmove(shadow_lit + first + m, shadow_lit + first, (size_t)(h - m));
            memset(shadow_lit + first, 0, (size_t)m);
            memmove(shadow[first + m], shadow[first], (size_t)(h - m) * SHADOW_COLS);
            for (r = first; r < first + m; r++) {
                memset(shadow[r], ' ', SHADOW_COLS);
            }
        }
    }
#endif
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
        put(y);
    }
    put(23);
    put(1);
    put(show ? 1 : 0);
    flush();
}
