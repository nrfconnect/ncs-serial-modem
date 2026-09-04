# Minimal makefile for Sphinx documentation
#
# The documentation is split into docsets, each built with its own
# configuration from _docsets/ over the shared sources in this directory.
# _scripts/build_docsets.py does the work; these targets are thin wrappers.

# You can set these variables from the command line, and also from the
# environment.
SPHINXOPTS    ?=
PYTHON        ?= python3
BUILDDIR      ?= build

BUILDDOCSETS   = $(PYTHON) _scripts/build_docsets.py -b "$(BUILDDIR)" $(SPHINXOPTS) $(O)

.PHONY: help html docsets main nrf91m1 clean

# Put it first so that "make" without argument is like "make help".
help:
	@echo "Run 'doxygen' first, then one of:"
	@echo "  html       Build all docsets into $(BUILDDIR)/html (same as docsets)"
	@echo "  docsets    Build all docsets into $(BUILDDIR)/html"
	@echo "  main       Build only the main docset"
	@echo "  nrf91m1    Build only the nRF91M1 docset"
	@echo "  clean      Remove $(BUILDDIR)"
	@echo ""
	@echo "Pass extra sphinx-build options with SPHINXOPTS, for example:"
	@echo "  make docsets SPHINXOPTS=\"-W --keep-going\""

html: docsets

docsets:
	@$(BUILDDOCSETS)

main:
	@$(BUILDDOCSETS) --docset $@

nrf91m1:
	@$(BUILDDOCSETS) --docset $@ -t nrf91m1

clean:
	rm -rf "$(BUILDDIR)"
