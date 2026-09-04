#
# Copyright (c) 2026, Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

"""Sphinx configuration shared by all Serial Modem docsets.

Every docset has its own configuration directory next to this file. Those
configuration files import everything from this module and then set the few
values that differ between docsets. The reStructuredText sources are shared and
stay in :file:`doc/`.

Relative paths in a Sphinx configuration file resolve against the configuration
directory, which is no longer the source directory, so all paths here are
anchored to :file:`doc/` explicitly.
"""

from pathlib import Path
import os
import sys

DOC_BASE = Path(__file__).absolute().parents[1]

sys.path.insert(0, str(DOC_BASE / "_extensions"))
sys.path.insert(0, str(DOC_BASE / "_utils"))

import docsets  # noqa: E402

# -- Project information -----------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#project-information

copyright = "2026, Nordic Semiconductor"
author = "Nordic Semiconductor"
version = release = os.environ.get("VERSION", "latest")

# -- General configuration ---------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#general-configuration

extensions = [
    "breathe",
    "sphinx_tabs.tabs",
    "sphinx_togglebutton",
    "sphinxcontrib.jquery",
    "sphinx_copybutton",
    "sphinx.ext.intersphinx",
]

templates_path = [str(DOC_BASE / "_templates")]

# Directories that hold build output, tooling, or content copied verbatim, none
# of which Sphinx must read as source.
COMMON_EXCLUDE_PATTERNS = [
    "build",
    "_build_doxygen",
    "_build_sphinx",
    "_docsets",
    "_scripts",
    "_static",
    "_utils",
    "Thumbs.db",
    ".DS_Store",
]

# -- Options for HTML output -------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#options-for-html-output

html_theme = "sphinx_ncs_theme"
html_show_sphinx = False

html_theme_options = {
    "docsets": docsets.ALL_DOCSETS,
    "addons_url": "https://nrfconnect.github.io/ncs-app-index/",
    "bare_metal_url": "",
    "ncs_url": "https://nrfconnectdocs.nordicsemi.com/ncs/latest/nrf/index.html",
    "ncs_label": "nRF Connect SDK Docs",
    "logo_url": "https://docs.nordicsemi.com/",
}

# The theme fetches versions.json from the version root, one level above the
# docset directories, so html_extra_path cannot deliver it. It is copied there
# by _scripts/build_docsets.py instead.

## -- Options for Breathe ----------------------------------------------------
# https://breathe.readthedocs.io/en/latest/index.html
#
# WARNING: please, check breathe maintainership status before using this
# extension in production!

breathe_projects = {"ncs-serial-modem": str(DOC_BASE / "_build_doxygen" / "xml")}
breathe_default_project = "ncs-serial-modem"
breathe_default_members = ("members",)

# Include following files at the end of each .rst file.
rst_epilog = """
.. include:: /links.txt
.. include:: /shortcuts.txt
"""


def docset_exclude_patterns(docset: str) -> list[str]:
    """Return the exclude patterns for a docset.

    The sources are shared, so each docset must exclude the home pages that
    belong to the other docsets. Otherwise they end up outside of any toctree.

    Args:
        docset: Docset name.

    Returns:
        Value for the ``exclude_patterns`` configuration option.
    """

    return COMMON_EXCLUDE_PATTERNS + [
        f"{home}.rst"
        for name, (_, home) in docsets.ALL_DOCSETS.items()
        if name != docset
    ]
