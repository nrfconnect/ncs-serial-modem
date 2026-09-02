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

DOCSET = "main"

project, root_doc = docsets.ALL_DOCSETS[DOCSET]  # noqa: F405

exclude_patterns = docset_exclude_patterns(DOCSET)  # noqa: F405

html_theme_options["docset"] = DOCSET  # noqa: F405
