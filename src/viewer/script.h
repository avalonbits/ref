/*
 * The scripted build (REF_SCRIPT), for the emulator checks in test/run.sh:
 * keys come from /ref.keys on the card instead of the keyboard, and ref
 * writes what it does and draws to /ref.out. Nothing of it is in the
 * release build: SCRIPTED(...) and LOG(...) compile to nothing there.
 */
#ifndef SCRIPT_H
#define SCRIPT_H

#ifdef REF_SCRIPT

#include "ui.h"

/* The transcript, /ref.out. */
void script_log(const char* fmt, ...);
void script_close(void);

/* Keys from /ref.keys: input.c's, in this build. */
void script_keys_open(void);
void script_keys_close(void);
key script_keys_wait(void);

/* A copy of what is drawn on the text rows and the status line -- each
 * character, and which rows have something lit -- kept as screen.c draws,
 * for the transcript's screen dumps. */
void shadow_clear(void);
void shadow_row(int row);
void shadow_put(int row, int x, int ch, int lit);
void shadow_scroll(int first, int n);

/* The screen, into the transcript: as drawn, and as the VDP has it. */
void scr_dump(const char* why);

#define SCRIPTED(call) call
#define LOG(...) script_log(__VA_ARGS__)

#else

#define SCRIPTED(call)
#define LOG(...)

#endif

#endif
