"""
SM Project Info
================

Sphinx extension that:

1. Provides helper functions used by ``conf.py`` to compute version strings
   and read them from the west manifest (``get_manifest_revision``,
   ``strip_v``).

2. Pre-expands ``|name|`` substitution markers in RST source *before*
   docutils parses it (including inside ``code-block`` /
   ``literalinclude`` bodies where docutils' own substitution machinery
   does not reach).  The substitution table is supplied by ``conf.py``
   via the ``sm_substitutions`` Sphinx config value.

3. Exposes ``build_rst_epilog`` so that ``conf.py`` can pre-expand the
   same markers inside ``links.txt`` / ``shortcuts.txt`` URI targets
   before handing the combined text to Sphinx as ``rst_epilog``.

Usage in conf.py
~~~~~~~~~~~~~~~~

After adding the extension directory to ``sys.path``::

    from sm_project_info import (
        get_manifest_revision, strip_v, build_rst_epilog
    )

    ...

    SUBSTITUTIONS = [("ncs_version", NCS_VERSION), ...]

    sm_substitutions = SUBSTITUTIONS   # read by this extension's setup()
    rst_epilog = build_rst_epilog(sm_BASE / "doc", SUBSTITUTIONS)

    extensions = [..., "sm_project_info"]

Why pre-expansion is necessary for links / shortcuts
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Docutils does NOT expand RST substitution references (``|name|``) inside
the URI part of a hyperlink target, so a URL containing
``|ncs_version_number|`` would render with the literal placeholder.  We
therefore resolve those markers ourselves before handing the text to
Sphinx via ``rst_epilog``.  The source-read handler covers the remaining
case: markers inside ``code-block`` / ``literalinclude`` bodies.

Why list order matters for SUBSTITUTIONS
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
``_expand_placeholders`` walks the list top-to-bottom.  If a name is ever
a *substring* of another name the longer one should be listed first so it
is replaced before the shorter one could consume part of it.

Copyright (c) 2026 Nordic Semiconductor ASA
SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
"""

import re
from pathlib import Path


# ---------------------------------------------------------------------------
# Version file parsing (mirrors zephyr/cmake/modules/version.cmake)
# ---------------------------------------------------------------------------

# Field patterns that mirror the REGEX MATCH calls in
# zephyr/cmake/modules/version.cmake (the canonical Zephyr parser).
# Character classes are kept identical to the CMake originals:
#   numeric fields  -> [0-9]*   (zero-or-more, matches empty → "")
#   EXTRAVERSION    -> [a-z0-9\.\-]*
_VERSION_FIELDS = {
    "VERSION_MAJOR": r"VERSION_MAJOR = ([0-9]*)",
    "VERSION_MINOR": r"VERSION_MINOR = ([0-9]*)",
    "PATCHLEVEL":    r"PATCHLEVEL = ([0-9]*)",
    "VERSION_TWEAK": r"VERSION_TWEAK = ([0-9]*)",
    "EXTRAVERSION":  r"EXTRAVERSION = ([a-z0-9.\-]*)",
}


def parse_version_file(version_file) -> dict:
    """Parse a Zephyr-format VERSION file and return its fields as a dict.

    The regex patterns are a direct translation of the ones used by
    ``zephyr/cmake/modules/version.cmake`` (the canonical parser), ensuring
    both tools produce identical results from the same file.

    :param version_file: Path-like object pointing to a VERSION file.
    :returns: Dict with keys ``VERSION_MAJOR``, ``VERSION_MINOR``,
        ``PATCHLEVEL``, ``VERSION_TWEAK``, ``EXTRAVERSION``.
    """
    text = Path(version_file).read_text(encoding="utf-8")
    result = {}
    for key, pattern in _VERSION_FIELDS.items():
        m = re.search(pattern, text)
        result[key] = m.group(1) if m else ""
    return result


def get_version_string(version_file) -> str:
    """Return the dot-separated version string for the given VERSION file.

    :param version_file: Path-like object pointing to a VERSION file.
    :returns: Version string such as ``"2.1.0"`` or ``"2.1.0-rc1"``.
    """
    v = parse_version_file(version_file)
    base = "{}.{}.{}".format(v["VERSION_MAJOR"], v["VERSION_MINOR"], v["PATCHLEVEL"])
    extra = v.get("EXTRAVERSION", "")
    return "{}-{}".format(base, extra) if extra else base

from sphinx.application import Sphinx

__version__ = "0.1.0"


# ---------------------------------------------------------------------------
# Version helpers (called from conf.py at load time)
# ---------------------------------------------------------------------------

def get_manifest_revision(manifest, name: str) -> str:
    """Return the revision string of *name* as written in ``west.yml``."""
    try:
        projects = manifest.get_projects([name])
    except ValueError as e:
        raise RuntimeError(
            f"west.yml: project '{name}' not found ({e})"
        ) from e
    if not projects or not projects[0].revision:
        raise RuntimeError(
            f"west.yml: project '{name}' missing revision"
        )
    return projects[0].revision


def strip_v(rev: str) -> str:
    """Strip a leading ``v`` from *rev* (e.g. ``"v3.3.0"`` → ``"3.3.0"``)."""
    return rev[1:] if rev.startswith("v") else rev


# ---------------------------------------------------------------------------
# RST substitution machinery (private to this module)
# ---------------------------------------------------------------------------

# Regex matching any RST substitution-reference-like token (``|name|``).
# Used by the leftover-placeholder check to flag misspelled / missing keys
# after expansion.  Must agree with the syntax produced by ``_sub_marker``.
_SUB_MARKER_RE = re.compile(r"\|[A-Za-z_][A-Za-z0-9_]*\|")


def _sub_marker(name: str) -> str:
    """Return the RST substitution marker for *name* (e.g. ``"|ncs_version|"``)."""
    return f"|{name}|"


def _expand_placeholders(text: str, subs: list) -> str:
    """Replace every ``|name|`` occurrence in *text* with its mapped value.

    Iterates over *subs* in the order entries are listed so the caller
    controls replacement order.
    """
    for name, value in subs:
        text = text.replace(_sub_marker(name), value)
    return text


def _assert_no_unresolved_markers(text: str, source: str) -> None:
    """Raise if *text* still contains any ``|name|``-shaped tokens.

    Catches typos and missing entries in SUBSTITUTIONS early — a
    silently-unresolved placeholder inside a URL would otherwise ship as
    a broken link in the rendered HTML.
    """
    leftovers = sorted(set(_SUB_MARKER_RE.findall(text)))
    if leftovers:
        raise RuntimeError(
            f"{source}: unresolved substitution(s) {leftovers}. "
            f"Add the missing entry to SUBSTITUTIONS in conf.py."
        )


# ---------------------------------------------------------------------------
# Public helper for conf.py: build rst_epilog from links / shortcuts files
# ---------------------------------------------------------------------------

def build_rst_epilog(doc_dir: Path, substitutions: list) -> str:
    """Return a pre-expanded ``rst_epilog`` string.

    Reads ``links.txt`` and ``shortcuts.txt`` from *doc_dir*, expands
    ``|name|`` markers defined in *substitutions*, validates that no
    unresolved markers remain in ``links.txt`` (broken URL guard), then
    returns the combined text for assignment to ``rst_epilog`` in
    ``conf.py``.

    Only ``links.txt`` is checked for leftovers: ``shortcuts.txt``
    legitimately defines new ``|name|`` substitutions of its own (inline-
    text shortcuts later expanded by Sphinx), so leftover markers there
    are expected and must not raise.
    """
    links_resolved = _expand_placeholders(
        (doc_dir / "links.txt").read_text(encoding="utf-8"), substitutions
    )
    _assert_no_unresolved_markers(links_resolved, str(doc_dir / "links.txt"))

    shortcuts_resolved = _expand_placeholders(
        (doc_dir / "shortcuts.txt").read_text(encoding="utf-8"), substitutions
    )

    return links_resolved + "\n" + shortcuts_resolved


# ---------------------------------------------------------------------------
# Sphinx extension: source-read handler
# ---------------------------------------------------------------------------

def _expand_substitutions_in_source(app: Sphinx, docname: str, source: list) -> None:
    """``source-read`` handler: resolve ``|name|`` markers in raw RST text.

    Sphinx passes the file content as a single-element list; mutating
    ``source[0]`` is the documented way to rewrite the input that docutils
    will subsequently parse.

    Tokens whose name is NOT in ``sm_substitutions`` pass through
    unchanged and are resolved later by docutils via ``rst_epilog``
    (e.g. the inline-text shortcuts defined in ``shortcuts.txt``).
    """
    subs = app.config.sm_substitutions
    if not subs:
        return
    text = source[0]
    if "|" not in text:
        return
    source[0] = _expand_placeholders(text, subs)


def _validate_substitutions(substitutions: list) -> None:
    """Raise if *substitutions* contains duplicate names."""
    seen: set[str] = set()
    for name, _ in substitutions:
        if name in seen:
            raise RuntimeError(
                f"sm_substitutions contains duplicate name: {name!r}. "
                f"Check SUBSTITUTIONS in conf.py."
            )
        seen.add(name)


def setup(app: Sphinx):
    """Sphinx extension entry point."""
    app.add_config_value("sm_substitutions", [], "env", types=[list])
    app.connect("builder-inited", lambda app: _validate_substitutions(
        app.config.sm_substitutions
    ))
    app.connect("source-read", _expand_substitutions_in_source)
    return {
        "version": __version__,
        "parallel_read_safe": True,
        "parallel_write_safe": True,
    }
