/*
 * The scripted build: see script.h. The checks in test/run.sh drive ref
 * with a script of keys on the card and read back what it wrote:
 *
 *   view topic 502 top 0 lines 22 cur 4:20      the topic view, after a key
 *   == <why> ... lit <why>: 0 1 ... header ...  a screen dump (dump:<why>)
 *   == vdp <why> ...                            the VDP's own screen (vdp:)
 *   pixel x,y rrggbb                            a cell's colour (pixel:)
 *   exit 0                                      ref's result
 */
#ifdef REF_SCRIPT

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <agon/mos.h>

#include "script.h"

/* --- the transcript ------------------------------------------------------------- */

static FILE* transcript;

void script_log(const char* fmt, ...)
{
    va_list ap;

    /* Appended: under hub, one card runs ref more than once. agondev's "a"
     * needs the file to be there already. */
    if (!transcript) {
        transcript = fopen("/ref.out", "a");
    }
    if (!transcript) {
        transcript = fopen("/ref.out", "w");
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

/* --- the shadow ------------------------------------------------------------------ */

#define SHADOW_COLS 132
#define SHADOW_ROWS 64

static char shadow[SHADOW_ROWS][SHADOW_COLS];
static uint8_t shadow_lit[SHADOW_ROWS];

void shadow_clear(void)
{
    memset(shadow, ' ', sizeof shadow);
    memset(shadow_lit, 0, sizeof shadow_lit);
}

void shadow_row(int row)
{
    if (row < SHADOW_ROWS) {
        shadow_lit[row] = 0;
    }
}

void shadow_put(int row, int x, int ch, int lit)
{
    if (row < SHADOW_ROWS && x < SHADOW_COLS) {
        shadow[row][x] = (char)ch;
        if (lit) {
            shadow_lit[row] = 1;
        }
    }
}

void shadow_scroll(int first, int n)
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

/* --- dumps ------------------------------------------------------------------------ */

/* The text rows and the status line as drawn; then, after them, which text
 * rows are lit, and the header -- so a dump compares with the VDP's screen
 * row for row. */
void scr_dump(const char* why)
{
    static char lit[SHADOW_ROWS * 3 + 1];
    int len = 0;
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
    for (r = 0; r < scr_rows && r < SHADOW_ROWS; r++) {
        if (shadow_lit[r]) {
            len += sprintf(lit + len, " %d", r);
        }
    }
    lit[len] = 0;
    script_log("lit %s:%s", why, lit);
    script_log("header %s", scr_header());
}

/* What the VDP has on its screen, read back a cell at a time with
 * VDU 23,0,&83 -- every column, the last too. Only the full emulator's VDP
 * answers; the CLI emulator's stand-in does not. The VDP matches a cell
 * against the font in the current text colours, so each role's colours are
 * tried, the last that worked first. Only a printable answer counts: a
 * blank cell read in colours whose foreground is its background matches a
 * solid block. */
static void vdp_dump(const char* why)
{
    static char row[SHADOW_COLS + 1];
    static int role;
    int r;
    int x;

    script_log("== vdp %s", why);
    for (r = 0; r < scr_screen_rows() && r < SHADOW_ROWS; r++) {
        int n = 0;

        for (x = 0; x < scr_screen_cols() && x < SHADOW_COLS; x++) {
            int c = 0;
            int tries;

            for (tries = 0; tries < C_COUNT && !(c >= 0x20 && c < 0x7f); tries++) {
                if (tries) {
                    role = (role + 1) % C_COUNT;
                }
                scr_forget_colour();
                scr_colour(role);
                scr_flush();
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
    scr_forget_colour();
}

/* The colour of the pixel in the middle of screen cell x, y, as the VDP
 * reports it (VDU 23,0,&84, in OS coordinates): read where a character
 * cannot be, such as the bottom right cell. */
static void vdp_pixel(int x, int y)
{
    int ox = (x * 2 + 1) * 1280 / (scr_screen_cols() * 2);
    int oy = 1023 - (y * 2 + 1) * 1024 / (scr_screen_rows() * 2);
    unsigned long c;

    scr_flush();
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

/* --- keys ------------------------------------------------------------------------- */
/*
 * /ref.keys, one a line:
 *
 *   up down left right pgup pgdn home end enter esc tab backtab back
 *   alt+left alt+right f1 f3 ctrl+up ctrl+down ctrl+left ctrl+right
 *   ctrl+f ctrl+n ctrl+p
 *   c:<x>          the character x          text:<s>   each character of s
 *   dump:<why>     not a key: the screen, as drawn, to the transcript
 *   vdp:<why>      not a key: the screen, as the VDP has it
 *   pixel:<x>,<y>  not a key: the colour of screen cell x, y
 *
 * When the script runs out, every key is Esc, so ref always gets out.
 */

static FILE* keys;
static char line[128];
static const char* text_left;

static const struct {
    const char* name;
    uint8_t ch, vk, mods;
} NAMES[] = {
    { "up", 11, VK_UP, 0 },           { "down", 10, VK_DOWN, 0 },
    { "left", 8, VK_LEFT, 0 },        { "right", 21, VK_RIGHT, 0 },
    { "pgup", 0, VK_PAGEUP, 0 },      { "pgdn", 0, VK_PAGEDOWN, 0 },
    { "home", 0, VK_HOME, 0 },        { "end", 0, VK_END, 0 },
    { "enter", 13, VK_RETURN, 0 },    { "esc", 27, VK_ESCAPE, 0 },
    { "tab", 9, VK_TAB, 0 },          { "backtab", 9, VK_TAB, MOD_SHFT },
    { "back", 127, VK_BACKSPACE, 0 },
    { "alt+left", 0, VK_LEFT, MOD_ALT }, { "alt+right", 0, VK_RIGHT, MOD_ALT },
    { "f1", 0, VK_F1, 0 },            { "f3", 0, VK_F3, 0 },
    { "ctrl+up", 0, VK_UP, MOD_CTRL },  { "ctrl+down", 0, VK_DOWN, MOD_CTRL },
    { "ctrl+left", 0, VK_LEFT, MOD_CTRL }, { "ctrl+right", 0, VK_RIGHT, MOD_CTRL },
    { "ctrl+f", 6, VK_f, MOD_CTRL },  { "ctrl+n", 14, VK_n, MOD_CTRL },
    { "ctrl+p", 16, VK_p, MOD_CTRL },
};

static key make(int ch, int vk, int mods)
{
    key k;

    k.ch = (uint8_t)ch;
    k.vk = (uint8_t)vk;
    k.mods = (uint8_t)mods;

    return k;
}

void script_keys_open(void)
{
    keys = fopen("/ref.keys", "r");
}

void script_keys_close(void)
{
    if (keys) {
        fclose(keys);
    }
}

key script_keys_wait(void)
{
    for (;;) {
        size_t n;
        unsigned k;

        if (text_left && *text_left) {
            return make((unsigned char)*text_left++, 0, 0);
        }
        text_left = NULL;
        if (!keys || !fgets(line, sizeof line, keys)) {
            return make(27, VK_ESCAPE, 0);
        }
        n = strlen(line);
        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) {
            line[--n] = 0;
        }
        if (n == 0 || line[0] == '#') {
            continue;
        }
        if (strncmp(line, "dump:", 5) == 0) {
            scr_dump(line + 5);
        } else if (strncmp(line, "vdp:", 4) == 0) {
            vdp_dump(line + 4);
        } else if (strncmp(line, "pixel:", 6) == 0) {
            int x = 0;
            int y = 0;

            sscanf(line + 6, "%d,%d", &x, &y);
            vdp_pixel(x, y);
        } else if (strncmp(line, "c:", 2) == 0) {
            script_log("key %s", line);

            return make((unsigned char)line[2], 0, 0);
        } else if (strncmp(line, "text:", 5) == 0) {
            script_log("key %s", line);
            text_left = line + 5;
        } else {
            for (k = 0; k < sizeof NAMES / sizeof NAMES[0]; k++) {
                if (strcmp(line, NAMES[k].name) == 0) {
                    script_log("key %s", line);

                    return make(NAMES[k].ch, NAMES[k].vk, NAMES[k].mods);
                }
            }
            script_log("unknown key %s", line);
        }
    }
}

#endif
