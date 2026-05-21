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

from sphinx.cmd.build import get_parser

# Docset directory name mapped to its title in the docset switcher and the
# docname of its home page. The first entry is the default docset that the
# published root page redirects to.
ALL_DOCSETS = {
    "main": ("Serial Modem", "index"),
    "nrf91m1": ("Serial Modem for nRF91M1", "index_nrf91m1"),
}


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


def get_intersphinx_mapping(docset: str) -> tuple[str, str] | None:
    """Return the intersphinx mapping for a docset.

    Args:
        docset: Docset name.

    Returns:
        Intersphinx mapping, or ``None`` if the docset has not been built yet.
    """

    inventory = get_builddir() / "html" / docset / "objects.inv"
    if not inventory.exists():
        return None

    return (str(Path("..") / docset), str(inventory))
