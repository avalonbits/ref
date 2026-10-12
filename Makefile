# ref: the Agon documentation, on the Agon.
#
#   make dat         fetch agon-docs at the pinned commit, build out/mos.dat,
#                    and write out/report.txt
#   make host        the reader core built for the PC: out/host/reftool
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
AGON_MAKE = PATH=$(AGONDEV)/bin:$$PATH $(MAKE) -f agon.mk

.PHONY: all dat host acc-core test frames agon agon-script agon-heap emu

all: dat

dat:
	@mkdir -p out
	cd build && $(PYTHON) -m refbuild --report ../out/report.txt

# -fsigned-char: char is signed on the eZ80, so the PC build reads bytes the
# way the Agon builds do.
host: out/host/reftool

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

agon:
	$(AGON_MAKE)

agon-script:
	$(AGON_MAKE) OBJDIR=obj/script BINDIR=bin/script EXTRA_CFLAGS=-DREF_SCRIPT

agon-heap:
	$(AGON_MAKE) OBJDIR=obj/heap BINDIR=bin/heap \
		EXTRA_CFLAGS='-DREF_SCRIPT -DZMALLOC -Dmalloc=z_malloc -Dcalloc=z_calloc -Drealloc=z_realloc -Dfree=z_free'

emu:
	test/run.sh
