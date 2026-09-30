#
# Copyright (c) 2026, Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

"""Sphinx configuration for the main Serial Modem docset.

This docset covers all nRF91-based devices. The shared settings live in
:file:`doc/_docsets/conf_common.py`.
"""

import sys
from pathlib import Path

# Sphinx puts only this docset directory on sys.path; conf_common lives in the parent.
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from conf_common import *  # noqa: F401,F403
from conf_common import docsets, docset_exclude_patterns, html_theme_options, \
    docset_latex_documents, docset_html_context, docset_latex_elements

DOCSET = Path(__file__).resolve().parent.name

project, root_doc = docsets.ALL_DOCSETS[DOCSET]

exclude_patterns = docset_exclude_patterns(DOCSET) + [
    # Pages that do not apply to the nRF91M1 go here.
    "index_nrf91m1.rst",
    "nrf91m1/*.rst",
]

latex_documents = docset_latex_documents(DOCSET)
latex_elements = docset_latex_elements(
    DOCSET, author_name=author, release_label=release
)

html_theme_options["docset"] = DOCSET
html_context = docset_html_context(DOCSET)
