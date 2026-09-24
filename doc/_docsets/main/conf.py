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

sys.path.insert(0, str(Path(__file__).absolute().parents[1]))

from conf_common import *  # noqa: F401,F403
from conf_common import docsets, docset_exclude_patterns, html_theme_options

DOCSET = "main"

project, root_doc = docsets.ALL_DOCSETS[DOCSET]

exclude_patterns = docset_exclude_patterns(DOCSET) + [
    # Pages that do not apply to the nRF91M1 go here.
    "index_nrf91m1.rst",
    "app/nrf91m1_intro.rst",
]

html_theme_options["docset"] = DOCSET
