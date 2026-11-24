#
# Copyright (c) 2026 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

from sphinx.application import Sphinx


def add_kapa_search(app: Sphinx) -> None:
    """Add Kapa.ai search widget to the documentation build.

    The widget is only added to the nRF91M1 docset, and only when the ``kapa``
    tag is set, that is, when sphinx-build is invoked with ``-t kapa``. CI sets
    the tag on the branch the nRF91M1 publication is published from as
    ``latest``, see ``PUBLICATIONS`` in :file:`doc/_utils/docsets.py`.

    Args:
        app: Sphinx instance.
    """

    if not (app.tags.has("kapa") and app.tags.has("nrf91m1")):
        return

    app.add_css_file("css/kapa-widget.css")
    app.add_js_file("js/kapa-widget.js")
