#
# Copyright (c) 2026, Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

"""Docset and publication metadata shared by all documentation builds.

A docset is a convention of the ``sphinx_ncs_theme``, not a Sphinx feature. The
theme renders the docset switcher from the ``docsets`` theme option and links to
``../<docset>/<home>.html``, so every docset must be built into its own
directory next to the other docsets of the same publication, and those docsets
must all declare the same ``docsets`` dictionary. Keeping :data:`ALL_DOCSETS`
and :data:`PUBLICATIONS` here is what guarantees the latter.

A publication is a document that is hosted on its own, under an add-on
directory of its own on the documentation site. Its docsets are siblings below
its root, where its landing page redirects to the first of them, and a docset
of another publication is reachable only through an absolute URL. Splitting a
docset into a publication of its own is therefore what turns it into a separate
document rather than a tab of an existing one.

A publication that holds a single docset can drop the docset directory and be
published from the docset itself, which is what :attr:`Publication.docset_at_root`
and :func:`publish_dir` describe.
"""

import json
import sys
from pathlib import Path
from typing import NamedTuple

import pdf_revision  # noqa: E402 — same directory as this module
from sphinx.cmd.build import get_parser

DOC_BASE = Path(__file__).resolve().parents[1]

# Docset directory name mapped to its title in the docset switcher and the
# docname of its home page.
ALL_DOCSETS = {
    "main": ("Serial Modem", "index"),
    "nrf91m1": ("nRF91M1 AT Commands", "index_nrf91m1"),
}


class Publication(NamedTuple):
    """How one hosted document is assembled from docsets.

    Attributes:
        docsets: Docsets of the publication, in the order they appear in the
            docset switcher. The landing page redirects to the first one.
        url: Base URL of the hosted publication, which the links that cross a
            publication boundary are built from because they cannot be
            relative. It ends in the version directory of the current version,
            which the theme calls ``latest``.
        versions: Version list of the publication, as a path below :file:`doc`,
            or ``None`` for a publication that is hosted in one version. The
            theme renders its version dropdown from the file, which is copied
            next to the docset directories, so a publication without one hides
            the dropdown instead, see
            :file:`_static/css/no_version_switcher.css`.
        docset_at_root: Whether the single docset of the publication is
            published at the root of the publication, so that its pages sit in
            the version directory itself instead of a docset directory below
            it. Only a publication that holds one docset can be published this
            way, and it also loses the docset switcher of the theme, which can
            only link to a sibling directory, see
            :file:`_static/css/no_docset_switcher.css`.
        branch: Branch whose documentation is published as the current version
            of the publication, or ``None`` for a publication that every
            branch publishes. Setting it limits the published documentation to
            that branch and to the tags of the versions in :attr:`versions`,
            but not the pull request previews, see :func:`publish_version`.
    """

    docsets: tuple[str, ...]
    url: str
    versions: str | None = None
    docset_at_root: bool = False
    branch: str | None = None


# Publication directory name mapped to its contents. The name is also the
# add-on directory the publication is uploaded to, see
# .github/workflows/doc-publish.yml.
PUBLICATIONS = {
    "serial_modem": Publication(
        docsets=("main",),
        url="https://nrfconnectdocs.nordicsemi.com/addons/addon-serial_modem/latest",
        versions="versions.json",
    ),
    "nrf91m1": Publication(
        docsets=("nrf91m1",),
        url="https://nrfconnectdocs.nordicsemi.com/addons/addon-nrf91m1/latest",
        versions="nrf91m1/versions.json",
        docset_at_root=True,
        branch="v2-branch",
    ),
}


def publication_of(docset: str) -> str:
    """Return the publication a docset belongs to.

    Args:
        docset: Docset name.

    Returns:
        Publication name.

    Raises:
        KeyError: The docset belongs to no publication.
    """

    for name, publication in PUBLICATIONS.items():
        if docset in publication.docsets:
            return name

    raise KeyError(f"Docset {docset} belongs to no publication")


def docset_url(docset: str) -> str:
    """Return the absolute base URL of a hosted docset.

    The pages of a docset sit in a directory of its own below the root of its
    publication, unless the publication is published from the docset itself.

    Args:
        docset: Docset name.

    Returns:
        URL the pages of the docset are hosted below.
    """

    publication = PUBLICATIONS[publication_of(docset)]
    if publication.docset_at_root:
        return publication.url

    return f"{publication.url}/{docset}"


def home_url(docset: str) -> str:
    """Return the absolute URL of the home page of a hosted docset.

    Used for the links that cross a publication boundary, which have no
    relative path to follow.

    Args:
        docset: Docset name.

    Returns:
        URL of the home page on the documentation site.
    """

    return f"{docset_url(docset)}/{ALL_DOCSETS[docset][1]}.html"


def publication_docsets(docset: str) -> dict[str, tuple[str, str]]:
    """Return the docsets that share a publication with a docset.

    This is the ``docsets`` theme option, so it is also what the docset
    switcher offers. A docset of another publication is left out, because the
    switcher can only link to a sibling directory.

    Args:
        docset: Docset name.

    Returns:
        Subset of :data:`ALL_DOCSETS`.
    """

    return {
        name: ALL_DOCSETS[name]
        for name in PUBLICATIONS[publication_of(docset)].docsets
    }


def versions_file(publication: str) -> Path | None:
    """Return the version list of a publication.

    Args:
        publication: Publication name.

    Returns:
        Path of the file the theme renders the version dropdown from, or
        ``None`` for a publication that is hosted in one version.
    """

    versions = PUBLICATIONS[publication].versions
    return DOC_BASE / versions if versions else None


def hosted_versions(publication: str) -> list[str]:
    """Return the versions a publication is hosted in.

    The first one is the current version, which the theme both labels and
    hosts as ``latest``; every other one is hosted in a directory named after
    it, see :func:`publish_version`.

    Args:
        publication: Publication name.

    Returns:
        Contents of the version list, or an empty list for a publication that
        is hosted in one version.
    """

    versions = versions_file(publication)
    return json.loads(versions.read_text()) if versions else []


def publish_version(publication: str, version: str, branch: str) -> str | None:
    """Return the directory of the add-on that a build publishes into.

    A pull request previews every publication, whatever branch it is against,
    because a preview directory of its own overwrites nothing. Of the other
    builds, a publication that every branch publishes follows the version of
    the build, and a publication that names a branch is published from that
    branch only, as the current version, and from the tags of the releases it
    is hosted in, which keeps an older release from being overwritten by a
    branch it was not built from.

    Args:
        publication: Publication name.
        version: Directory the ref of the build maps to, as
            :file:`.github/workflows/doc-build.yml` derives it.
        branch: Branch of the build, which is the base branch of a pull
            request and the tag name of a tag build.

    Returns:
        Directory below the add-on directory of the publication, or ``None``
        when this build does not publish it.
    """

    spec = PUBLICATIONS[publication]
    if not version:
        return None
    if spec.branch is None or version.startswith("PR-"):
        return version
    if branch != spec.branch:
        return version if version in hosted_versions(publication) else None

    return "latest"


# PDF base filename produced for each docset (without extension).
# Used as the LaTeX output filename and as the copy target in the HTML tree.
# When a doc revision is set (see :func:`doc_revision`), :func:`pdf_stem` appends
# :func:`pdf_revision.filename_suffix`.
PDF_FILENAMES = {
    "main": "serial_modem_documentation",
    "nrf91m1": "nrf91m1_at_commands",
}

# Primary title on the PDF front page (top of the blue band).
PDF_TITLES = {
    "main": "Serial Modem Documentation",
    "nrf91m1": "nRF91M1 AT Commands",
}

# Secondary line on the PDF front page (bottom-right of the blue band, where the
# Serial Modem layout prints the author name). When empty, ``@author`` is used.
PDF_GUIDE_TITLES = {
    "main": "",
    "nrf91m1": "Command Reference Guide",
}

def apply_build_environment() -> None:
    """Prepare the environment before :program:`sphinx-build` runs.

    Called from :file:`_scripts/build_docsets.py` so the build driver and
    Sphinx subprocess agree on env vars such as ``NRF91M1_DOC_VERSION``.
    """

    pdf_revision.prepare_build_environment()


def pdf_document_title(docset: str) -> str:
    """Return the LaTeX document title (PDF metadata and ``@title``).

    Args:
        docset: Docset name.

    Returns:
        Full document title string.
    """

    title = PDF_TITLES[docset]
    guide = PDF_GUIDE_TITLES.get(docset, "")
    revision = doc_revision(docset)
    rev_label = pdf_revision.band_label(revision)
    if rev_label:
        if guide:
            return f"{title} {guide} {rev_label}"
        return f"{title} {rev_label}"
    if guide:
        return f"{title} {guide}"
    return title


def doc_revision(docset: str) -> str:
    """Return the PDF document revision for a docset (no leading ``v``)."""

    return pdf_revision.revision_for(docset)


def pdf_band_release(docset: str) -> str | None:
    """Return the optional third PDF band line for the guide-style title page.

    Args:
        docset: Docset name.

    Returns:
        Release text for the title page, without LaTeX escaping, or ``None``.
    """

    return pdf_revision.band_label(doc_revision(docset))


def pdf_stem(docset: str) -> str:
    """Return the PDF basename for a docset.

    Args:
        docset: Docset name.

    Returns:
        Filename stem used for LaTeX output and the HTML download link.
    """

    return PDF_FILENAMES[docset] + pdf_revision.filename_suffix(doc_revision(docset))


def get_builddir() -> Path:
    """Return the documentation build directory.

    Docsets are built into ``<build>/<format>/<publication>/<docset>``, so the
    build directory is three levels above the Sphinx output directory.
    """

    argv = sys.argv[1:]

    # The PDF build runs sphinx-build in make mode, whose command line the
    # regular parser rejects. Make mode takes its arguments in a fixed order,
    # "-M <builder> <sourcedir> <outputdir>", before any option.
    if argv[:1] == ["-M"]:
        outputdir = Path(argv[3])
    else:
        outputdir = Path(get_parser().parse_args().outputdir)

    return (outputdir / ".." / ".." / "..").resolve()


def html_dir(build_dir: Path, docset: str) -> Path:
    """Return the HTML output directory of a docset.

    Args:
        build_dir: Documentation build directory.
        docset: Docset name.

    Returns:
        Directory the HTML of the docset is built into.
    """

    return build_dir / "html" / publication_of(docset) / docset


def publish_dir(build_dir: Path, publication: str) -> Path:
    """Return the HTML directory that is published for a publication.

    This is what :file:`.github/workflows/doc-build.yml` archives and what the
    version directory of the publication ends up holding. It is the
    publication root, which holds the docset directories, unless the
    publication is published from its single docset: then that docset is the
    root, and the landing page and the version list are copied into it.

    Args:
        build_dir: Documentation build directory.
        publication: Publication name.

    Returns:
        Directory whose contents are published.
    """

    root = build_dir / "html" / publication
    spec = PUBLICATIONS[publication]
    if spec.docset_at_root:
        return root / spec.docsets[0]

    return root


def get_intersphinx_mapping(from_docset: str, to_docset: str) -> tuple[str, str] | None:
    """Return the intersphinx mapping from one docset to another.

    The inventory is always read from the local build, so a reference resolves
    without network access, but the target it is resolved against depends on
    where the two docsets end up. Within a publication that is the sibling
    directory of the target, and across publications the hosted URL of the
    target, because nothing links the two trees on disk.

    Args:
        from_docset: Docset that holds the reference.
        to_docset: Docset the reference points into.

    Returns:
        Intersphinx mapping, or ``None`` if the target docset has not been
        built yet.
    """

    build_dir = get_builddir()
    inventory = html_dir(build_dir, to_docset) / "objects.inv"
    if not inventory.exists():
        return None

    if publication_of(from_docset) == publication_of(to_docset):
        target = str(Path("..") / to_docset)
    else:
        target = docset_url(to_docset)

    return (target, str(inventory))
