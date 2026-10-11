# ref: the Agon documentation, on the Agon.
#
#   make test        the builder's tests
#   make report      build the model of agon-docs and write out/report.txt

PYTHON ?= python3

.PHONY: all test report

all: report

test:
	cd build && $(PYTHON) -m unittest discover -s tests -t .

report:
	@mkdir -p out
	cd build && $(PYTHON) -m refbuild --report ../out/report.txt
