/*
 * Keys: MOS's key events, through agondev's queue, each with the modifiers
 * as they were when it went down -- or, in the scripted build, a script
 * read from /ref.keys on the card, one key a line:
 *
 *   up down left right pgup pgdn home end enter esc tab backtab back del
 *   alt+left alt+right f1 f3 ctrl+up ctrl+down ctrl+left ctrl+right
 *   ctrl+f ctrl+n ctrl+p
 *   c:<x>          the character x          text:<s>   each character of s
 *   dump:<why>     not a key: write the screen to the transcript
 *   vdp:<why>      not a key: write what the VDP shows, read back from it
 *   pixel:<x>,<y>  not a key: the colour of character cell x, y's middle pixel
 *
 * When the script runs out, every key is Esc, so ref always gets out.
 */
#include <string.h>

#include "ui.h"

#ifndef REF_SCRIPT

#include <agon/keyboard.h>

void in_open(void)
{
    kbuf_init(32);
}

void in_close(void)
{
    kbuf_deinit();
}

static struct keyboard_event_t pend;
static int have_pend;

int in_pending(void)
{
    while (!have_pend && kbuf_poll_event(&pend)) {
        if (pend.isdown) {
            have_pend = 1;
        }
    }

    return have_pend;
}

key in_wait(void)
{
    key k;

    while (!in_pending()) {
    }
    have_pend = 0;
    k.ch = pend.ascii;
    k.vk = pend.vkey;
    k.mods = pend.kmod;

    return k;
}

#else

#include <stdio.h>

static FILE* script;
static char line[128];
static const char* text_left;

void in_open(void)
{
    script = fopen("/ref.keys", "r");
}

void in_close(void)
{
    if (script) {
        fclose(script);
    }
}

int in_pending(void)
{
    return 0;                               /* one key at a time: every step drawn */
}

static key make(int ch, int vk, int mods)
{
    key k;

    k.ch = (uint8_t)ch;
    k.vk = (uint8_t)vk;
    k.mods = (uint8_t)mods;

    return k;
}

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
    { "back", 127, VK_BACKSPACE, 0 }, { "del", 0, VK_DELETE, 0 },
    { "alt+left", 0, VK_LEFT, MOD_ALT }, { "alt+right", 0, VK_RIGHT, MOD_ALT },
    { "f1", 0, VK_F1, 0 },            { "f3", 0, VK_F3, 0 },
    { "ctrl+up", 0, VK_UP, MOD_CTRL },  { "ctrl+down", 0, VK_DOWN, MOD_CTRL },
    { "ctrl+left", 0, VK_LEFT, MOD_CTRL }, { "ctrl+right", 0, VK_RIGHT, MOD_CTRL },
    { "ctrl+f", 6, VK_f, MOD_CTRL },  { "ctrl+n", 14, VK_n, MOD_CTRL },
    { "ctrl+p", 16, VK_p, MOD_CTRL },
};

key in_wait(void)
{
    for (;;) {
        size_t n;
        unsigned k;

        if (text_left && *text_left) {
            return make((unsigned char)*text_left++, 0, 0);
        }
        text_left = NULL;
        if (!script || !fgets(line, sizeof line, script)) {
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
            continue;
        }
        if (strncmp(line, "pixel:", 6) == 0) {
            int x = 0;
            int y = 0;

            sscanf(line + 6, "%d,%d", &x, &y);
            scr_vdp_pixel(x, y);
            continue;
        }
        if (strncmp(line, "vdp:", 4) == 0) {
            scr_vdp_dump(line + 4);
            continue;
        }
        if (strncmp(line, "c:", 2) == 0) {
            script_log("key %s", line);

            return make((unsigned char)line[2], 0, 0);
        }
        if (strncmp(line, "text:", 5) == 0) {
            script_log("key %s", line);
            text_left = line + 5;
            continue;
        }
        for (k = 0; k < sizeof NAMES / sizeof NAMES[0]; k++) {
            if (strcmp(line, NAMES[k].name) == 0) {
                script_log("key %s", line);

                return make(NAMES[k].ch, NAMES[k].vk, NAMES[k].mods);
            }
        }
        script_log("unknown key %s", line);
    }
}

#endif

int in_meaning(key k)
{
    int alt = k.mods & MOD_ALT;
    int ctrl = k.mods & MOD_CTRL;

    switch (k.vk) {
    case VK_UP:
    case VK_KP_UP:
        return ctrl ? K_HEAD_PREV : K_UP;
    case VK_DOWN:
    case VK_KP_DOWN:
        return ctrl ? K_HEAD_NEXT : K_DOWN;
    case VK_LEFT:
    case VK_KP_LEFT:
        return ctrl ? K_LINK_PREV : alt ? K_HISTBACK : K_LEFT;
    case VK_RIGHT:
    case VK_KP_RIGHT:
        return ctrl ? K_LINK_NEXT : alt ? K_HISTFWD : K_RIGHT;
    case VK_f:
    case VK_F:
        if (ctrl) {
            return K_FIND_HERE;
        }
        break;
    case VK_n:
    case VK_N:
        if (ctrl) {
            return K_FIND_NEXT;
        }
        break;
    case VK_p:
    case VK_P:
        if (ctrl) {
            return K_FIND_PREV;
        }
        break;
    case VK_PAGEUP:
    case VK_KP_PAGEUP:
        return K_PGUP;
    case VK_PAGEDOWN:
    case VK_KP_PAGEDOWN:
        return K_PGDN;
    case VK_HOME:
    case VK_KP_HOME:
        return K_HOME;
    case VK_END:
    case VK_KP_END:
        return K_END;
    case VK_RETURN:
    case VK_KP_ENTER:
        return K_ENTER;
    case VK_ESCAPE:
        return K_ESC;
    case VK_BACKSPACE:
        return K_BACK;
    case VK_DELETE:
        return K_DELETE;
    case VK_TAB:
        return (k.mods & MOD_SHFT) ? K_BACKTAB : K_TAB;
    case VK_F3:
        return K_FIND;
    case VK_F1:
        return K_HELP;
    }
    /* Keys the VDP gives only as characters. */
    switch (k.ch) {
    case 6:
        return K_FIND_HERE;                 /* CTRL+F, N and P as characters */
    case 14:
        return K_FIND_NEXT;
    case 16:
        return K_FIND_PREV;
    case 13:
        return K_ENTER;
    case 27:
        return K_ESC;
    case 127:
    case 8:
        return K_BACK;
    case 9:
        return K_TAB;
    }
    if (k.ch >= 32 && k.ch < 127) {
        return K_CHAR;
    }

    return K_NONE;
}
