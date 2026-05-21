#
# Copyright (c) 2026, Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

"""Sphinx configuration for the nRF91M1 Serial Modem docset.

This docset is a subset of the main docset: it reads the same sources from
:file:`doc/` and starts from :file:`doc/index_nrf91m1.rst`. The shared settings
live in :file:`doc/_docsets/conf_common.py`.

Add the pages that do not apply to the nRF91M1 to ``exclude_patterns`` below.
Excluded pages are never read, so an ``:ref:`` or ``:doc:`` link from an
included page to an excluded one becomes a warning. Turn such links into
intersphinx references to the main docset instead, for example
``:external+main:doc:`app/at_carrier```.
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).absolute().parents[1]))

from conf_common import *  # noqa: F401,F403
from conf_common import docsets, docset_exclude_patterns, html_theme_options, \
    docset_latex_documents, docset_html_context

DOCSET = "nrf91m1"

project, root_doc = docsets.ALL_DOCSETS[DOCSET]

exclude_patterns = docset_exclude_patterns(DOCSET) + [
    # Pages that do not apply to the nRF91M1 go here.
    "app/at_carrier.rst",
    "app/at_commands.rst",
    "app/README.rst",
    "app/sm_*.rst",
    "gsg_guide.rst",
    "lib/*.rst",
    "releases/*.rst",
    "samples/*.rst",
    "uart_configuration.rst",
]

latex_documents = docset_latex_documents(DOCSET)

html_theme_options["docset"] = DOCSET
html_context = docset_html_context(DOCSET)

# Lets this docset link into the main docset with :external+main:. The mapping
# is only available once the main docset has been built, which is why
# _scripts/build_docsets.py builds the docsets in order.
intersphinx_mapping = {}

_main_mapping = docsets.get_intersphinx_mapping("main")
if _main_mapping:
    intersphinx_mapping["main"] = _main_mapping
