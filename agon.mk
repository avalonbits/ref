# ref for the Agon, built by agondev: bin/ref.bin from src/core and
# src/viewer. The top-level Makefile runs this (make agon).
#
#   make -f agon.mk                                   bin/ref.bin
#   make -f agon.mk OBJDIR=obj/script BINDIR=bin/script EXTRA_CFLAGS=-DREF_SCRIPT
#                                                     the scripted build

NAME = ref
LDHAS_ARG_PROCESSING = 0
LDHAS_EXIT_HANDLER = 0

include $(shell agondev-config --makefile)

# After the include: makefile.inc sets CFLAGS with a plain `=`.
#
# -twoaddr-reschedule=false works round a miscompile in agondev's clang
# 15.0.7: with it on, `n = *p++` (and the like) can load from the pointer
# after its increment. test/core.sh catches it: the reader core built here
# answers differently from the PC build. See BRIEFING.md.
AGONDEV_FIX = -mllvm -twoaddr-reschedule=false
CFLAGS += -Isrc/core -Wall $(AGONDEV_FIX) $(EXTRA_CFLAGS) -MMD -MP
-include $(shell find $(OBJDIR) -name '*.d' 2>/dev/null)
