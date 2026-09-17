#!/usr/bin/env python3
#
# Copyright (c) 2026, Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

"""
Serial Modem documentation build utility
========================================

Builds every docset listed in :data:`docsets.ALL_DOCSETS` from the shared
sources in :file:`doc/`, one Sphinx run per docset and output format, and
assembles the directory layout that gets published::

    <build>/html/index.html         redirect to the default docset
    <build>/html/versions.json      version list read by the theme
    <build>/html/<docset>/          one directory per docset
    <build>/pdf/<docset>/latex/     LaTeX sources and PDF of one docset

The docsets must be siblings below the HTML root because that is where the
docset switcher of ``sphinx_ncs_theme`` looks for them, and versions.json must
sit next to them for the same reason.

Only the HTML is built by default. The PDF additionally needs a LaTeX
installation, Latexmk and Inkscape, so it is normally left to the doc-build
workflow. Building both formats also copies each PDF into the HTML of its
docset, which is where the "Download PDF" link of the pages points.

Run :program:`doxygen` before this script; Breathe reads its XML output.

Usage
*****

python _scripts/build_docsets.py [-b build] [--docset NAME] [--format FORMAT]
                                 [sphinx options]

Any unrecognized option is forwarded to :program:`sphinx-build`, for example::

    python _scripts/build_docsets.py -W --keep-going
"""

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

DOC_BASE = Path(__file__).absolute().parents[1]

sys.path.insert(0, str(DOC_BASE / "_scripts"))
sys.path.insert(0, str(DOC_BASE / "_utils"))

import docsets  # noqa: E402
import merge_search_indexes  # noqa: E402


# Output formats, in the order they are built. HTML comes first because a
# docset can use intersphinx to reference the HTML inventory of another one.
ALL_FORMATS = ("html", "pdf")


def build_docset(docset: str, fmt: str, build_dir: Path, sphinx_opts: list[str]) -> None:
    """Build a single docset in a single output format.

    Args:
        docset: Docset name.
        fmt: Output format, one of :data:`ALL_FORMATS`.
        build_dir: Documentation build directory.
        sphinx_opts: Extra options for sphinx-build.
    """

    # Every docset has a configuration directory of its own, which is what
    # makes the shared configuration in _docsets/conf_common.py produce a
    # different document per docset.
    #
    # The docset name is also set as a Sphinx tag, so that a shared page can
    # vary per docset with ".. only:: <docset>" instead of being forked.
    conf_opts = ["-c", str(DOC_BASE / "_docsets" / docset), "-t", docset]

    if fmt == "html":
        # Each docset needs its own doctree cache. Sharing one between builds
        # of the same source directory makes them invalidate each other
        # continuously.
        cmd = [
            sys.executable,
            "-m",
            "sphinx",
            "-b",
            "html",
            *conf_opts,
            "-d",
            str(build_dir / "doctrees" / docset),
            *sphinx_opts,
            str(DOC_BASE),
            str(build_dir / "html" / docset),
        ]
    else:
        # Make mode runs Latexmk over the generated sources itself, so that the
        # PDF needs no second step and picks up the latex_engine setting. It
        # only accepts its arguments in this order, keeps its own doctree cache
        # below the output directory, and writes into an extra latex/
        # subdirectory.
        cmd = [
            sys.executable,
            "-m",
            "sphinx",
            "-M",
            "latexpdf",
            str(DOC_BASE),
            str(build_dir / "pdf" / docset),
            *conf_opts,
            *sphinx_opts,
        ]

    print(f"Building the {fmt} of the {docset} docset:", " ".join(cmd), flush=True)
    subprocess.run(cmd, check=True, cwd=DOC_BASE)


def copy_pdf(docset: str, build_dir: Path) -> None:
    """Copy the PDF of a docset next to its HTML.

    The "Download PDF" link that :file:`_templates/breadcrumbs.html` adds to
    every page resolves against the root of the docset, so the PDF has to sit
    there. Does nothing if the HTML of the docset has not been built.

    Args:
        docset: Docset name.
        build_dir: Documentation build directory.
    """

    html_dir = build_dir / "html" / docset
    if not html_dir.is_dir():
        print(f"Skipping the PDF copy of the {docset} docset: no HTML built", flush=True)
        return

    pdf = build_dir / "pdf" / docset / "latex" / "ncs-serial-modem.pdf"
    print(f"Copying {pdf.name} into the {docset} docset", flush=True)
    shutil.copy(pdf, html_dir / pdf.name)


def copy_extra_content(build_dir: Path) -> None:
    """Copy the content that belongs next to the docsets instead of inside one.

    Args:
        build_dir: Documentation build directory.
    """

    html_dir = build_dir / "html"
    for src in (DOC_BASE / "_static" / "html" / "index.html", DOC_BASE / "versions.json"):
        print(f"Copying {src.name} to the HTML root", flush=True)
        shutil.copy(src, html_dir / src.name)


def main() -> None:
    """Entry point."""

    parser = argparse.ArgumentParser(allow_abbrev=False)
    parser.add_argument(
        "-b",
        "--build-dir",
        type=Path,
        default=DOC_BASE / "build",
        help="Documentation build directory (default: doc/build)",
    )
    parser.add_argument(
        "--docset",
        action="append",
        choices=list(docsets.ALL_DOCSETS),
        help="Build only this docset. Can be given more than once. "
        "The default is to build all of them.",
    )
    parser.add_argument(
        "--format",
        action="append",
        choices=list(ALL_FORMATS),
        help="Build this output format. Can be given more than once. "
        "The default is to build the HTML only.",
    )

    args, sphinx_opts = parser.parse_known_args()

    build_dir = args.build_dir.absolute()

    # Keep the order of ALL_DOCSETS: a docset can use intersphinx to reference a
    # docset that comes before it, which requires its objects.inv to exist.
    selected = [name for name in docsets.ALL_DOCSETS if not args.docset or name in args.docset]
    formats = [fmt for fmt in ALL_FORMATS if fmt in (args.format or ["html"])]

    for fmt in formats:
        for docset in selected:
            build_docset(docset, fmt, build_dir, sphinx_opts)

    if "html" in formats:
        copy_extra_content(build_dir)

        # Merging rewrites every index in terms of the others, so it only makes
        # sense once all of them are up to date.
        if len(selected) == len(docsets.ALL_DOCSETS):
            print("Merging the search indexes", flush=True)
            merge_search_indexes.main(build_dir)

    if "pdf" in formats:
        for docset in selected:
            copy_pdf(docset, build_dir)


if __name__ == "__main__":
    main()
