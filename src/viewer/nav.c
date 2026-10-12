/*
 * Moving between the views -- a topic, the contents, a list, find --
 * until the user quits. Each view returns where to go next (GO_*); a
 * topic's Backspace returns the view it was opened from, which is shown
 * again as it was left.
 */
#include "ui.h"

int nav_run(int go, int topic, const char* text, int from)
{
    int resume = 0;                 /* find, as it was left */

    if (go == GO_VIEW && view_open_from((unsigned)topic, from) != 0) {
        ui_message("That topic could not be read.");

        return GO_QUIT;
    }
    while (go != GO_QUIT) {
        int t;

        switch (go) {
        case GO_VIEW:
            go = view_loop();
            resume = go == GO_FIND && view_has();
            break;

        case GO_TOC:
            t = toc_loop(view_current());
            if (t >= 0) {
                go = view_open_from((unsigned)t, GO_TOC) == 0 ? GO_VIEW : GO_TOC;
            } else if (t == TOC_FIND) {
                go = GO_FIND;
                resume = 0;
            } else if (t == TOC_QUIT) {
                return GO_QUIT;
            } else if (!view_has()) {
                return GO_BACK;             /* Esc, with no topic to go back to */
            } else {
                go = GO_VIEW;               /* TOC_BACK, to the topic */
            }
            break;

        case GO_FIND:
            /* Fresh from / or F3, with text from the command line or none;
             * as it was left when Backspace comes back to it. */
            t = pick_find(resume ? NULL : text ? text : "");
            text = NULL;
            resume = 0;
            if (t >= 0) {
                go = view_open_from((unsigned)t, GO_FIND) == 0 ? GO_VIEW : GO_FIND;
            } else if (view_has()) {
                go = GO_VIEW;
            } else {
                return GO_QUIT;
            }
            break;

        case GO_LIST:
            t = pick_again();
            if (t >= 0) {
                go = view_open_from((unsigned)t, GO_LIST) == 0 ? GO_VIEW : GO_LIST;
            } else if (view_has()) {
                go = GO_VIEW;
            } else {
                return GO_QUIT;
            }
            break;

        default:
            return GO_QUIT;
        }
    }

    return GO_QUIT;
}
