#
#  Smalltalk-2026: A Manual.
#
#  pdflatex three times, because the table of contents and the internal
#  references each need a pass to settle.  No latexmk dependency: the point
#  of this document is a system that builds with what you already have.
#
TEX      := pdflatex
TEXFLAGS := -interaction=nonstopmode -halt-on-error -file-line-error
DOC      := smalltalk-2026-manual
SOURCES  := $(DOC).tex preamble.tex $(wildcard chapters/*.tex)

#  The examples file is the book's own test suite: every answer printed in
#  the text is a doctest here, checked against a real image.
#
#  The dialect comes from PROFILE: #dialect governs the packages a profile
#  loads and the doit each doctest is compiled into alike, so the book is
#  checked in the language it documents.  Point PROFILE at bluebook and the
#  1983 answers are what the run demands.
#
#  Run from the repository root, not from here: a profile's package paths are
#  relative to the profile file, but the 1983 MANIFEST's entries are relative
#  to the tree root, so a bootstrap started anywhere else cannot find them.
ROOT     := ..
ST80     ?= ./st80
PROFILE  ?= profiles/st2026.profile
EXAMPLES := examples/ManualExamples.class.st

.PHONY: all verify clean distclean help

all: $(DOC).pdf

$(DOC).pdf: $(SOURCES)
	$(TEX) $(TEXFLAGS) $(DOC).tex
	$(TEX) $(TEXFLAGS) $(DOC).tex
	$(TEX) $(TEXFLAGS) $(DOC).tex
	@echo "built $@ -- $$($(TEX) --version >/dev/null 2>&1 && echo ok)"

#  Check every example in the book against a live image.  Fails loudly.
verify: $(EXAMPLES)
	@test -x $(ROOT)/$(ST80) || { echo "verify: $(ST80) is not built -- run make in $(ROOT)" >&2; exit 1; }
	cd $(ROOT) && $(ST80) -bootstrap -profile $(PROFILE) \
		-doctests manual/$(EXAMPLES)

clean:
	rm -f $(DOC).aux $(DOC).log $(DOC).out $(DOC).toc $(DOC).lof \
	      $(DOC).lot $(DOC).idx $(DOC).ilg $(DOC).ind $(DOC).fls \
	      $(DOC).fdb_latexmk chapters/*.aux

distclean: clean
	rm -f $(DOC).pdf

help:
	@echo "Targets:"
	@echo "  all       (default) build $(DOC).pdf"
	@echo "  verify    run every example in the book against a live image"
	@echo "  clean     remove LaTeX intermediates"
	@echo "  distclean also remove the PDF"
