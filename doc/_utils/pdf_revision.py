#
# Copyright (c) 2026, Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

"""PDF document revision per docset (filename suffix and title-page band)."""

import os
from pathlib import Path

DOC_BASE = Path(__file__).resolve().parents[1]

NRF91M1_VERSION_ENV = "NRF91M1_DOC_VERSION"
NRF91M1_VERSION_FILE = DOC_BASE / "nrf91m1" / "DOC_VERSION"


def sm_release_label() -> str:
    """Serial Modem release for Sphinx ``version`` / ``release`` (HTML sidebar)."""

    return os.environ.get("VERSION", "latest").strip() or "latest"


def _read_file(path: Path) -> str:
    if not path.is_file():
        return ""
    return path.read_text(encoding="utf-8").strip()


def _read_env_or_file(env_var: str, path: Path) -> str:
    env = os.environ.get(env_var, "").strip()
    if env:
        return env
    return _read_file(path)


def revision_for(docset: str) -> str:
    """Return the PDF revision string for a docset, without a leading ``v``."""

    if docset == "main":
        return sm_release_label()
    if docset == "nrf91m1":
        return _read_env_or_file(NRF91M1_VERSION_ENV, NRF91M1_VERSION_FILE)
    return ""


def prepare_build_environment() -> None:
    """Set ``NRF91M1_DOC_VERSION`` from the version file when the env is unset."""

    if os.environ.get(NRF91M1_VERSION_ENV, "").strip():
        return
    file_version = _read_file(NRF91M1_VERSION_FILE)
    if file_version:
        os.environ[NRF91M1_VERSION_ENV] = file_version


def band_label(revision: str) -> str | None:
    """Third PDF band line (``v0.1``, ``latest``, or ``None`` when unset)."""

    if not revision:
        return None
    if revision[0].isdigit():
        return f"v{revision}"
    return revision


def filename_suffix(revision: str) -> str:
    """Suffix for :func:`docsets.pdf_stem` (``_v2.0.0``, ``_latest``, or ``""``)."""

    if not revision:
        return ""
    if revision[0].isdigit():
        return f"_v{revision}"
    return f"_{revision}"
