# The stand-in for ade's F1, built by agondev against hub's client library
# (HUB=<hub checkout>, ~/code/devel by default).
NAME = hubf1
LDHAS_ARG_PROCESSING = 0
LDHAS_EXIT_HANDLER = 0
include $(shell agondev-config --makefile)
HUB ?= $(HOME)/code/devel
CFLAGS += -I$(HUB)/include
LIBS := $(HUB)/build/lib/agondev/libhub.a
