# ref: the Agon documentation, on the Agon.
#
#   make dat         fetch agon-docs at the pinned commit, build out/mos.dat,
#                    and write out/report.txt
#   make host        the reader core built for the PC: out/host/reftool, and
#                    md2ref: out/host/md2ref
#   make agon-md2ref bin/md2ref/md2ref.bin, md2ref for the Agon (md2ref.mk)
#   make agon-md2ref-heap
#                    bin/md2ref-heap/md2ref.bin, with the zmalloc shim
#   make agon-md2ref-cycles
#                    bin/md2ref-cycles/md2ref.bin, which has the emulator
#                    count its cycles (IO ports 0x40 and 0x41): for timing
#   make acc-core    the reader core compiled by acc, which must keep working
#   make agon        bin/ref.bin, by agondev (agon.mk)
#   make agon-script bin/script/ref.bin: keys from /ref.keys, a transcript
#                    in /ref.out, for the emulator checks
#   make test        the builder's tests, the core against the builder, the
#                    stack frames, and the core under acc
#   make emu         ref in the emulator: test/run.sh
#   make agon-heap   bin/heap/ref.bin: the scripted build with zap's zmalloc
#                    shim, which reports the heap's peak as ref exits

PYTHON ?= python3
CC     ?= cc
ACC    ?= $(HOME)/code/acc/bin/acc

CORE_SRC := $(wildcard src/core/*.c)
CORE_HDR := $(wildcard src/core/*.h)
SAN      := -fsanitize=address,undefined -fno-sanitize-recover=all

AGONDEV ?= $(HOME)/agondev
# Each program's objects, given on the command line: makefile.inc takes
# every .c under src as the link's prerequisites when it is read, so
# setting OBJS in agon.mk or md2ref.mk, after it, would still compile the
# other program's sources. Expanded by the sub-make, with its OBJDIR.
REF_OBJS = OBJS='$$(patsubst src/%.c,$$(OBJDIR)/%.o,$$(wildcard src/core/*.c src/viewer/*.c))'
MD2REF_OBJS = OBJS='$$(patsubst src/%.c,$$(OBJDIR)/%.o,$$(wildcard src/md2ref/*.c) \
	src/core/normalize.c src/core/lz4.c $$(if $$(ZMALLOC),src/viewer/zmalloc.c)) \
	$$(patsubst src/%.s,$$(OBJDIR)/%.o,$$(wildcard src/md2ref/*.s))'

AGON_MAKE = PATH=$(AGONDEV)/bin:$$PATH $(MAKE) -f agon.mk $(REF_OBJS)
MD2REF_MAKE = PATH=$(AGONDEV)/bin:$$PATH $(MAKE) -f md2ref.mk $(MD2REF_OBJS)

.PHONY: all dat host acc-core test frames agon agon-script agon-heap agon-md2ref \
	agon-md2ref-heap agon-md2ref-cycles emu

ZMALLOC_FLAGS = -DZMALLOC -Dmalloc=z_malloc -Dcalloc=z_calloc -Drealloc=z_realloc -Dfree=z_free

all: dat

dat:
	@mkdir -p out
	cd build && $(PYTHON) -m refbuild --report ../out/report.txt

# -fsigned-char: char is signed on the eZ80, so the PC build reads bytes the
# way the Agon builds do.
host: out/host/reftool out/host/md2ref

MD2REF_SRC := $(wildcard src/md2ref/*.c) src/core/normalize.c src/core/lz4.c
MD2REF_HDR := $(wildcard src/md2ref/*.h) $(CORE_HDR)

out/host/md2ref: $(MD2REF_SRC) $(MD2REF_HDR)
	@mkdir -p out/host
	$(CC) -std=c99 -Wall -Wextra -Wno-unused-parameter -Werror -fsigned-char -O1 -g $(SAN) \
		-Isrc/core $(MD2REF_SRC) -o $@

# Without the sanitizers, for valgrind: test/growth.sh counts the
# instructions it runs.
out/host/md2ref-plain: $(MD2REF_SRC) $(MD2REF_HDR)
	@mkdir -p out/host
	$(CC) -std=c99 -Wall -Wextra -Wno-unused-parameter -Werror -fsigned-char -O1 -g \
		-Isrc/core $(MD2REF_SRC) -o $@

out/host/reftool: $(CORE_SRC) $(CORE_HDR) test/host/reftool.c
	@mkdir -p out/host
	$(CC) -std=c99 -Wall -Wextra -Wno-unused-parameter -Werror -fsigned-char -O1 -g $(SAN) \
		-Isrc/core $(CORE_SRC) test/host/reftool.c -o $@

acc-core:
	@mkdir -p out/acc
	@if [ -x "$(ACC)" ]; then \
		for f in $(CORE_SRC); do \
			$(ACC) -c $$f -o out/acc/$$(basename $$f .c).o >/dev/null || exit 1; \
		done; echo "PASS  the reader core compiles with acc"; \
	else echo "SKIP  acc-core: no acc at $(ACC)"; fi

frames:
	test/frames.sh

test: host acc-core frames
	cd build && $(PYTHON) -m unittest discover -s tests -t .

# agondev's makefile makes $(BINDIR) with a plain mkdir, which fails for
# bin/script in a fresh checkout with no bin: so each is made here first.
agon:
	@mkdir -p bin obj
	$(AGON_MAKE)

agon-script:
	@mkdir -p bin/script obj/script
	$(AGON_MAKE) OBJDIR=obj/script BINDIR=bin/script EXTRA_CFLAGS=-DREF_SCRIPT

agon-md2ref:
	@mkdir -p bin/md2ref obj/md2ref
	$(MD2REF_MAKE) OBJDIR=obj/md2ref BINDIR=bin/md2ref

agon-md2ref-heap:
	@mkdir -p bin/md2ref-heap obj/md2ref-heap
	$(MD2REF_MAKE) OBJDIR=obj/md2ref-heap BINDIR=bin/md2ref-heap ZMALLOC=1 \
		EXTRA_CFLAGS='$(ZMALLOC_FLAGS)'

agon-md2ref-cycles:
	@mkdir -p bin/md2ref-cycles obj/md2ref-cycles
	$(MD2REF_MAKE) OBJDIR=obj/md2ref-cycles BINDIR=bin/md2ref-cycles \
		EXTRA_CFLAGS=-DMD2REF_CYCLES

agon-heap:
	@mkdir -p bin/heap obj/heap
	$(AGON_MAKE) OBJDIR=obj/heap BINDIR=bin/heap \
		EXTRA_CFLAGS='-DREF_SCRIPT $(ZMALLOC_FLAGS)'

emu:
	test/run.sh
