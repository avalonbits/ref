/*
 * The help screen, and a message that waits for a key.
 */
#include "script.h"
#include "ui.h"

static const char* const HELP[] = {
    "ref -- the Agon documentation",
    "",
    "Reading a topic: the cursor",
    "  Arrows                       move it; the screen follows",
    "  PgUp PgDn Home End           by the page, or to either end",
    "  Ctrl+Up  Ctrl+Down           to the previous or next heading",
    "  Ctrl+Left  Ctrl+Right        to the previous or next link (Tab too)",
    "  Enter                        follow the link under it",
    "  Ctrl+F                       find words on the screen and on",
    "  Ctrl+N  Ctrl+P               the next or previous match",
    "  Backspace  Alt+Left          back: to the link you followed, or to",
    "                               the contents, list or find it came from",
    "  Alt+Right                    forward",
    "  n  p                         the next or previous topic",
    "  u                            the section the topic is in",
    "  t                            the contents",
    "  /  F3                        find: type to narrow the list",
    "  Esc  q                       quit",
    "",
    "From the command line",
    "  ref                          the documents on the card",
    "  ref <doc>                    a document's contents",
    "  ref <doc> <query>            look it up: ref mos vdu 23 27 3",
    "  ref <doc> -k <query>         look it up, and nothing else",
    "  ref <doc> -s <words>         search the text",
    NULL,
};

/* "Press a key" on the status line. */
static void press_a_key(void)
{
    scr_begin(scr_rows);
    scr_colour(C_STATUS);
    scr_adds(" Press a key");
    scr_end();
}

void ui_help(void)
{
    int r;

    scr_cls();
    for (r = 0; HELP[r] && r < scr_rows; r++) {
        int title = r == 0 || (HELP[r][0] && HELP[r][0] != ' ');

        scr_begin(r);
        scr_colour(title ? C_HEAD : C_TEXT);
        scr_adds(HELP[r]);
        scr_end();
    }
    press_a_key();
    SCRIPTED(scr_dump("help"));
    in_wait();
}

void ui_message(const char* text)
{
    scr_cls();
    scr_begin(scr_rows / 2);
    scr_colour(C_TEXT);
    scr_adds("  ");
    scr_adds(text);
    scr_end();
    LOG("message %s", text);
    press_a_key();
    in_wait();
}
