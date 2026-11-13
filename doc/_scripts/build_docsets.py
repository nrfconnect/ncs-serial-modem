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

    <build>/html/<publication>/index.html      redirect to the first docset
    <build>/html/<publication>/versions.json   version list, dropdown only
    <build>/html/<publication>/<docset>/       one directory per docset
    <build>/pdf/<publication>/<docset>/latex/  LaTeX sources and PDF

Each publication is uploaded as a document of its own, so its directory is
what :file:`.github/workflows/doc-build.yml` archives. The docsets must be
siblings below the root of their publication because that is where the docset
switcher of ``sphinx_ncs_theme`` looks for them, and versions.json must sit
next to them for the same reason.

A publication that is published from its single docset is archived from that
docset directory instead, so that the pages end up one level higher than the
layout above, and the two files above are copied into that docset directory.
See :func:`docsets.publish_dir`.

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
import os
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
    out_dir = build_dir / fmt / docsets.publication_of(docset) / docset

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
            str(out_dir),
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
            str(out_dir),
            *conf_opts,
            *sphinx_opts,
        ]

    # The NCS toolchain overwrites PERL5LIB and PERLLIB with its own Perl 5.30
    # paths. The system latexmk uses system perl (>= 5.38 on Ubuntu 24.04),
    # which refuses to load 5.30 modules. Strip those variables so that
    # latexmk finds the correct libraries.
    env = {k: v for k, v in os.environ.items() if k not in ("PERL5LIB", "PERLLIB")}

    print(f"Building the {fmt} of the {docset} docset:", " ".join(cmd), flush=True)
    subprocess.run(cmd, check=True, cwd=DOC_BASE, env=env)


def copy_pdf(docset: str, build_dir: Path) -> None:
    """Copy the PDF of a docset next to its HTML.

    The "Download PDF" link that :file:`_templates/breadcrumbs.html` adds to
    every page resolves against the root of the docset, so the PDF has to sit
    there. Does nothing if the HTML of the docset has not been built.

    Args:
        docset: Docset name.
        build_dir: Documentation build directory.
    """

    html_dir = docsets.html_dir(build_dir, docset)
    if not html_dir.is_dir():
        print(f"Skipping the PDF copy of the {docset} docset: no HTML built", flush=True)
        return

    publication = docsets.publication_of(docset)
    pdf = build_dir / "pdf" / publication / docset / "latex" / f"{docsets.pdf_stem(docset)}.pdf"
    print(f"Copying {pdf.name} into the {docset} docset", flush=True)
    shutil.copy(pdf, html_dir / pdf.name)


def copy_extra_content(publication: str, build_dir: Path) -> None:
    """Copy the content that belongs next to the docsets instead of inside one.

    The landing page is published as the ``index.html`` of the publication, so
    that the hosted root forwards to the first docset. The version list is only
    copied for a publication that is hosted in several versions, because that
    is the one whose theme shows a version dropdown. The theme always reads it
    from the current version, so every version ships a copy.

    Both belong next to the docset directories, which is the root of the
    publication, except for a publication that is published from its single
    docset: there that docset is the root, see :func:`docsets.publish_dir`.

    Args:
        publication: Publication name.
        build_dir: Documentation build directory.
    """

    root = docsets.publish_dir(build_dir, publication)
    sources = {"index.html": DOC_BASE / "_static" / "html" / f"index_{publication}.html"}
    versions = docsets.versions_file(publication)
    if versions:
        sources["versions.json"] = versions

    for name, src in sources.items():
        print(f"Copying {src.name} to the root of the {publication} publication", flush=True)
        shutil.copy(src, root / name)


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

    docsets.apply_build_environment()

    build_dir = args.build_dir.absolute()

    # Keep the order of ALL_DOCSETS: a docset can use intersphinx to reference a
    # docset that comes before it, which requires its objects.inv to exist.
    selected = [name for name in docsets.ALL_DOCSETS if not args.docset or name in args.docset]
    formats = [fmt for fmt in ALL_FORMATS if fmt in (args.format or ["html"])]

    for fmt in formats:
        for docset in selected:
            build_docset(docset, fmt, build_dir, sphinx_opts)

    if "html" in formats:
        for publication, spec in docsets.PUBLICATIONS.items():
            built = [name for name in spec.docsets if name in selected]
            if not built:
                continue

            copy_extra_content(publication, build_dir)

            # Merging rewrites every index in terms of the others, so it only
            # makes sense once every docset of the publication is up to date,
            # and only a publication that holds several of them has anything
            # to merge. A docset never searches outside its publication,
            # because the result links are relative to the docset directory.
            if len(built) == len(spec.docsets) > 1:
                print(f"Merging the search indexes of {publication}", flush=True)
                merge_search_indexes.main(build_dir, publication)

    if "pdf" in formats:
        for docset in selected:
            copy_pdf(docset, build_dir)


if __name__ == "__main__":
    main()
