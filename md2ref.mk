# md2ref for the Agon, built by agondev. The top-level Makefile runs this
# (make agon-md2ref) with OBJDIR, BINDIR and OBJS given on the command line:
# makefile.inc expands its rules as it is read, so setting them here, after
# it, would be too late.

NAME = md2ref
LDHAS_ARG_PROCESSING = 0
LDHAS_EXIT_HANDLER = 0

include $(shell agondev-config --makefile)

# After the include: makefile.inc sets CFLAGS with a plain `=`. The same
# compiler workaround as agon.mk's.
CFLAGS += -Isrc/core $(if $(ZMALLOC),-Isrc/viewer) -Wall -mllvm -twoaddr-reschedule=false -MMD -MP \
	$(EXTRA_CFLAGS)

# md2ref, and the reader core's normalising, which keys must agree with;
# with ZMALLOC=1, zap's zmalloc shim too (make agon-md2ref-heap). OBJS
# comes from the top-level Makefile's command line, which is what keeps
# makefile.inc from compiling ref's sources as well.
-include $(shell find $(OBJDIR) -name '*.d' 2>/dev/null)
