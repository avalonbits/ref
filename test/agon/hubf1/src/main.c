/*
 * What ade's F1 does, and nothing else: run `ref -k <word>` through hub as a
 * user program, with this program as the frame's continuation, which prints
 * the result hub hands back -- 0 when ref showed a topic, 100 when nothing
 * matched.
 *
 *   hubf1 <word...>      queue ref, then return
 *   hubf1 -c <cmd...>    queue that command instead, as a control
 *   hubf1 -back          the continuation
 */
#include <stdio.h>
#include <string.h>

#include <hub/hub.h>

static char cmd[HUB_CMD_MAX + 1];

int main(int argc, char** argv)
{
    int i;

    /* hub_present first: every other call needs it to have found hub. */
    if (!hub_present()) {
        printf("f1: no hub\r\n");

        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "-back") == 0) {
        printf("f1 back: result %d, failed job %d\r\n", hub_last_result(), hub_failed_job());

        return 0;
    }
    i = 1;
    if (argc > 2 && strcmp(argv[1], "-c") == 0) {
        strcpy(cmd, argv[2]);
        i = 3;
    } else {
        strcpy(cmd, "ref -k");
    }
    for (; i < argc; i++) {
        if (strlen(cmd) + strlen(argv[i]) + 2 > HUB_CMD_MAX) {
            break;
        }
        strcat(cmd, " ");
        strcat(cmd, argv[i]);
    }
    if (hub_enter("F1T ") != HUB_OK || hub_push(cmd, HUB_USER_PROGRAM) != HUB_OK
        || hub_return_to("hubf1 -back") != HUB_OK) {
        printf("f1: hub refused\r\n");

        return 0;
    }

    return 0;
}
