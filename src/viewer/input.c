/*
 * Keys: MOS's key events, through agondev's queue, each with the modifiers
 * as they were when it went down -- or, in the scripted build, a script on
 * the card (script.c). And what each key means, however it was spelled.
 */
#include <string.h>

#include "script.h"
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

/* The scripted build: keys from /ref.keys (script.c). */

void in_open(void)
{
    script_keys_open();
}

void in_close(void)
{
    script_keys_close();
}

int in_pending(void)
{
    return 0;                               /* one key at a time: every step drawn */
}

key in_wait(void)
{
    return script_keys_wait();
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
