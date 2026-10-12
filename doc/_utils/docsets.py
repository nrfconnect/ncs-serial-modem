#
# Copyright (c) 2026, Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

"""Docset metadata shared by all Serial Modem documentation builds.

A docset is a convention of the ``sphinx_ncs_theme``, not a Sphinx feature. The
theme renders the docset switcher from the ``docsets`` theme option and links to
``../<docset>/<home>.html``, so every docset must be built into its own
directory next to the other docsets, and every docset must declare the same
``docsets`` dictionary. Keeping :data:`ALL_DOCSETS` here is what guarantees the
latter.
"""

import sys
from pathlib import Path

import pdf_revision  # noqa: E402 — same directory as this module
from sphinx.cmd.build import get_parser

DOC_BASE = Path(__file__).resolve().parents[1]

# Docset directory name mapped to its title in the docset switcher and the
# docname of its home page. The first entry is the default docset that the
# published root page redirects to.
ALL_DOCSETS = {
    "main": ("Serial Modem", "index"),
    "nrf91m1": ("nRF91M1 AT Commands", "index_nrf91m1"),
}

# PDF base filename produced for each docset (without extension).
# Used as the LaTeX output filename and as the copy target in the HTML tree.
# When a doc revision is set (see :func:`doc_revision`), :func:`pdf_stem` appends
# :func:`pdf_revision.filename_suffix`.
PDF_FILENAMES = {
    "main": "serial_modem_documentation",
    "nrf91m1": "nrf91m1_at_commands",
}

# Primary title on the PDF front page (top of the blue band).
# Root of the published documentation, holding the docsets of the development
# version. A docset that is built on its own takes the inventory of the docsets
# it references from here, since they are not built next to it.
PUBLISHED_URL = "https://nrfconnectdocs.nordicsemi.com/addons/addon-serial_modem/latest"

# PDF filename produced for each docset (without extension).
# Used as the LaTeX output filename and as the copy target in the HTML tree.
PDF_TITLES = {
    "main": "Serial Modem Documentation",
    "nrf91m1": "nRF91M1 AT Commands",
}

# Secondary line on the PDF front page (bottom-right of the blue band, where the
# Serial Modem layout prints the author name). When empty, ``@author`` is used.
PDF_GUIDE_TITLES = {
    "main": "",
    "nrf91m1": "Command Reference Guide",
}

def apply_build_environment() -> None:
    """Prepare the environment before :program:`sphinx-build` runs.

    Called from :file:`_scripts/build_docsets.py` so the build driver and
    Sphinx subprocess agree on env vars such as ``NRF91M1_DOC_VERSION``.
    """

    pdf_revision.prepare_build_environment()


def pdf_document_title(docset: str) -> str:
    """Return the LaTeX document title (PDF metadata and ``@title``).

    Args:
        docset: Docset name.

    Returns:
        Full document title string.
    """

    title = PDF_TITLES[docset]
    guide = PDF_GUIDE_TITLES.get(docset, "")
    revision = doc_revision(docset)
    rev_label = pdf_revision.band_label(revision)
    if rev_label:
        if guide:
            return f"{title} {guide} {rev_label}"
        return f"{title} {rev_label}"
    if guide:
        return f"{title} {guide}"
    return title


def doc_revision(docset: str) -> str:
    """Return the PDF document revision for a docset (no leading ``v``)."""

    return pdf_revision.revision_for(docset)


def pdf_band_release(docset: str) -> str | None:
    """Return the optional third PDF band line for the guide-style title page.

    Args:
        docset: Docset name.

    Returns:
        Release text for the title page, without LaTeX escaping, or ``None``.
    """

    return pdf_revision.band_label(doc_revision(docset))


def pdf_stem(docset: str) -> str:
    """Return the PDF basename for a docset.

    Args:
        docset: Docset name.

    Returns:
        Filename stem used for LaTeX output and the HTML download link.
    """

    return PDF_FILENAMES[docset] + pdf_revision.filename_suffix(doc_revision(docset))


def get_builddir() -> Path:
    """Return the documentation build directory.

    Docsets are built into ``<build>/<format>/<docset>``, so the build
    directory is two levels above the Sphinx output directory.
    """

    argv = sys.argv[1:]

    # The PDF build runs sphinx-build in make mode, whose command line the
    # regular parser rejects. Make mode takes its arguments in a fixed order,
    # "-M <builder> <sourcedir> <outputdir>", before any option.
    if argv[:1] == ["-M"]:
        outputdir = Path(argv[3])
    else:
        outputdir = Path(get_parser().parse_args().outputdir)

    return (outputdir / ".." / "..").resolve()


def get_intersphinx_mapping(docset: str) -> tuple[str, str]:
    """Return the intersphinx mapping for a docset.

    The target stays relative, because the docsets are siblings both in the
    build directory and in the published documentation. Only the inventory
    differs: the one of the local build when that docset was built, and the
    published one otherwise, so that building a single docset still resolves
    the references into the docsets it was built without.

    Args:
        docset: Docset name.

    Returns:
        Intersphinx mapping of the docset.
    """

    target = str(Path("..") / docset)
    inventory = get_builddir() / "html" / docset / "objects.inv"

    if inventory.exists():
        return (target, str(inventory))

    return (target, f"{PUBLISHED_URL}/{docset}/objects.inv")
