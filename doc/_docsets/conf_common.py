#
# Copyright (c) 2026, Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

"""Sphinx configuration shared by all Serial Modem docsets.

Every docset has its own configuration directory next to this file. Those
configuration files import everything from this module and then set the few
values that differ between docsets. The reStructuredText sources are shared and
stay in :file:`doc/`.

Relative paths in a Sphinx configuration file resolve against the configuration
directory, which is no longer the source directory, so all paths here are
anchored to :file:`doc/` explicitly.
"""

from pathlib import Path
import os
import sys

from docutils import nodes
from sphinx.transforms.post_transforms import SphinxPostTransform

DOC_BASE = Path(__file__).absolute().parents[1]

sys.path.insert(0, str(DOC_BASE / "_extensions"))
sys.path.insert(0, str(DOC_BASE / "_utils"))

import docsets  # noqa: E402

# -- Project information -----------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#project-information

copyright = "2026, Nordic Semiconductor"
author = "Nordic Semiconductor"
version = release = os.environ.get("VERSION", "latest")

# -- General configuration ---------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#general-configuration

extensions = [
    "breathe",
    "sphinx_tabs.tabs",
    "sphinx_togglebutton",
    "sphinxcontrib.jquery",
    "sphinx_copybutton",
    "sphinx.ext.intersphinx",
    # Converts the SVG figures to PDF for the LaTeX builder, which cannot embed
    # SVG. Comes from sphinxcontrib-svg2pdfconverter and shells out to Inkscape.
    # It does nothing in an HTML build, so Inkscape is only needed for the PDF.
    "sphinxcontrib.inkscapeconverter",
]

templates_path = [str(DOC_BASE / "_templates")]

# Directories that hold build output, tooling, or content copied verbatim, none
# of which Sphinx must read as source.
COMMON_EXCLUDE_PATTERNS = [
    "build",
    "_build_doxygen",
    "_build_sphinx",
    "_docsets",
    "_scripts",
    "_static",
    "_utils",
    "Thumbs.db",
    ".DS_Store",
]

# -- Options for HTML output -------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#options-for-html-output

html_theme = "sphinx_ncs_theme"
html_show_sphinx = False

html_theme_options = {
    "docsets": docsets.ALL_DOCSETS,
    "addons_url": "https://nrfconnect.github.io/ncs-app-index/",
    "bare_metal_url": "",
    "ncs_url": "https://nrfconnectdocs.nordicsemi.com/ncs/latest/nrf/index.html",
    "ncs_label": "nRF Connect SDK Docs",
    "logo_url": "https://docs.nordicsemi.com/",
}

html_static_path = [str(DOC_BASE / "_static")]

html_css_files = ['css/custom.css']

# The theme fetches versions.json from the version root, one level above the
# docset directories, so html_extra_path cannot deliver it. It is copied there
# by _scripts/build_docsets.py instead.

## -- Options for Breathe ----------------------------------------------------
# https://breathe.readthedocs.io/en/latest/index.html
#
# WARNING: please, check breathe maintainership status before using this
# extension in production!

breathe_projects = {"ncs-serial-modem": str(DOC_BASE / "_build_doxygen" / "xml")}
breathe_default_project = "ncs-serial-modem"
breathe_default_members = ("members",)

# Include following files at the end of each .rst file.
rst_epilog = """
.. include:: /links.txt
.. include:: /shortcuts.txt
"""


# -- Options for LaTeX output -------------------------------------------------
#
# Every docset renders its own PDF from these settings. The only part that
# differs between them is latex_documents, which docset_latex_documents()
# below derives from the docset name.

latex_engine = 'xelatex'

latex_use_xindy = False

# 'colorrows' is required for the TableRowColorHeader setting below to apply.
latex_table_style = ['booktabs', 'colorrows']

# The notice types that the sphinxadmonition environment dispatches to. The
# seealso and todo directives are styled separately by Sphinx and are not used
# by these docs, so they are left out.
NOTICE_TYPES = ('note', 'hint', 'important', 'tip', 'attention', 'caution',
                'danger', 'error', 'warning')

# Fill of a notice panel, in the \definecolor syntax the sphinxsetup colour
# keys expect. Every type shares one fill except caution, which the reference
# sets apart with a darker one.
NOTICE_COLOR = '{RGB}{221,239,246}'
NOTICE_COLOR_OVERRIDES = {'caution': '{RGB}{200,215,222}'}


def notice_box_keys() -> list[str]:
    """Return the sphinxsetup keys that flatten the notices into one panel.

    Sphinx gives each notice type a fill, a border and, for the note-like
    ones, rounded corners of its own. The reference draws them all as a flat
    panel with square corners and no border, and gives them all the same fill
    apart from caution, so the keys are generated per type rather than
    repeated by hand.

    Returns:
        One ``sphinxsetup`` key per entry, for every type in
        :data:`NOTICE_TYPES`.
    """

    return [
        f'div.{notice}_{key}'
        for notice in NOTICE_TYPES
        for key in ('background-TeXcolor='
                    + NOTICE_COLOR_OVERRIDES.get(notice, NOTICE_COLOR),
                    'border-width=0pt',
                    'border-radius=0pt')
    ]


def notice_title_commands() -> str:
    """Return the LaTeX that prints a notice heading as bold body text.

    Since Sphinx 7.4 a heading goes in a coloured row of its own, with an icon
    and without the colon that the LaTeX writer appends to it. The reference
    runs it as bold text on the panel instead, colon included, so every
    ``\\sphinxstyle<type>title`` command is redefined. That is the hook Sphinx
    documents for this, and it leaves the title of a topic or a sidebar alone,
    which the underlying ``\\sphinxdotitlerow`` would not.

    Returns:
        One ``\\renewcommand`` per line, for every type in
        :data:`NOTICE_TYPES`.
    """

    return '\n'.join(
        rf'\renewcommand\sphinxstyle{notice}title[1]'
        r'{\noindent\sphinxstrong{#1}\par\nobreak\medskip}'
        for notice in NOTICE_TYPES
    )


latex_elements = {
    'papersize': 'a4paper',
    'pointsize': '11pt',
    # Sphinx's xelatex default is FreeSerif; Open Sans is closer to the Nordic
    # template. Keep the defaults as fallback so builds work without the font.
    'fontpkg': r"""
\IfFontExistsTF{Open Sans}
  {\setmainfont{Open Sans}\setsansfont{Open Sans}}
  {\setmainfont{FreeSerif}\setsansfont{FreeSans}}
\IfFontExistsTF{DejaVu Sans Mono}
  {\setmonofont{DejaVu Sans Mono}}
  {\setmonofont{FreeMono}}
""",
    # Sphinx defaults to the Bjarne fncychap style, which is what prints the
    # "Chapter" label above the title. The preamble styles the heads instead.
    'fncychap': '',
    # 'oneside' drops the blank verso pages; 'openany' lets chapters start on
    # either side instead of forcing them onto a right-hand page.
    'extraclassoptions': 'openany,oneside',
    # Sphinx frames code blocks in a 0.4pt rule over white with 3pt rounded
    # corners; the Nordic style is a flat grey panel with no border, so
    # verbatimwithframe drops the rule outright. That key deliberately leaves
    # the corner radius alone, because a rounded corner still shows once the
    # panel has a background, so pre_border-radius has to square the panel off
    # separately. The framed package stays in use either way, because that is
    # what paints the background. pre_TeXcolor is the text colour, and it
    # reaches the whole block because the preamble below stops Pygments from
    # colouring any token; on its own it would only catch the tokens that
    # highlighting leaves alone. Padding measured off the reference is about
    # 11pt left of the text and 9pt above it, and one key covers all four
    # sides. The verbatim* keys blank out the red visible-space and hook-arrow
    # markers Sphinx prints where it wraps a long line, which the reference
    # does not show either.
    # By default a code-block line only breaks at a space or at ASCII
    # punctuation, so a long run of letters and digits with no break point
    # overflows the grey panel into the margin. verbatimforcewraps lets Sphinx
    # fall back to breaking such a line at any character, and maxoverfull=0
    # applies that fallback as soon as the line is wider than the panel rather
    # than tolerating the default three characters of overhang.
    # notice_box_keys() adds the notice panels. Their headings need LaTeX and
    # are in the preamble instead.
    'sphinxsetup': ','.join([
        'TableRowColorHeader={RGB}{0,162,198}',
        'VerbatimColor={RGB}{240,240,240}',
        'pre_TeXcolor={RGB}{112,112,112}',
        'verbatimwithframe=false',
        'pre_border-radius=0pt',
        'verbatimsep=10pt',
        'verbatimvisiblespace={}',
        'verbatimcontinued={}',
        'verbatimforcewraps=true',
        'verbatimmaxoverfull=0',
        *notice_box_keys(),
    ]),
    # Replaces \sphinxmaketitle. The page-anchor and clearpage handling around
    # the titlepage is what Sphinx itself does; only the layout differs.
    'maketitle': r"""
\makeatletter
\let\sphinxrestorepageanchorsetting\relax
\ifHy@pageanchor\def\sphinxrestorepageanchorsetting{\Hy@pageanchortrue}\fi
\hypersetup{pageanchor=false}
\begin{titlepage}
  \begingroup
    \def\endgraf{ }\def\and{\& }%
    \pdfstringdefDisableCommands{\def\\{, }}%
    \hypersetup{pdfauthor={\@author}, pdftitle={\@title}}%
  \endgroup
  % The band bleeds off the left paper edge, which sits 1in+\oddsidemargin
  % outside the text block. \makebox keeps the line exactly \linewidth wide so
  % the overhang does not report an overfull box. \fboxsep becomes the band's
  % inner padding, keeping the title clear of the paper edge, and the minipage
  % is sized so the band itself stays 0.90\paperwidth by 0.32\paperheight.
  \begingroup
    \setlength{\fboxsep}{16mm}%
    \setlength{\nordicbandwidth}{\dimexpr0.90\paperwidth-2\fboxsep\relax}%
    \setlength{\nordicbandheight}{\dimexpr0.32\paperheight-2\fboxsep\relax}%
    \noindent\makebox[\linewidth][l]{%
      \hspace*{\dimexpr-1in-\oddsidemargin\relax}%
      \colorbox{nordicblue}{%
        \begin{minipage}[t][\nordicbandheight][b]{\nordicbandwidth}
          \sffamily\color{white}
          {\Huge\raggedright\hyphenpenalty=10000\exhyphenpenalty=10000
           \@title\par}
          \vspace{2.5em}
          \begin{flushright}
            {\Large\bfseries\@author\par}
            \vspace{0.4em}
            {\large\py@release\releaseinfo\par}
          \end{flushright}
        \end{minipage}%
      }%
    }%
  \endgroup
  \vfill
  % logo.png surrounds the artwork with white padding, so the drawn logo is
  % only 0.82 of the file height: 35mm here renders it about 28mm tall, the
  % size in the reference. The negative kern carries it past the 1in text
  % margin so it ends about 13mm from the paper's right edge.
  \noindent\makebox[\linewidth][r]{%
    \includegraphics[height=35mm]{logo.png}\hspace*{-14mm}%
  }
\end{titlepage}
\setcounter{footnote}{0}%
\let\thanks\relax\let\maketitle\relax
\clearpage
\ifdefined\sphinxbackoftitlepage\sphinxbackoftitlepage\fi
\if@openright\cleardoublepage\else\clearpage\fi
\sphinxrestorepageanchorsetting
\makeatother
""",
    "preamble": r"""
\usepackage{sectsty}
\usepackage{etoolbox}

\definecolor{nordicblue}{RGB}{0,162,198}

% Title page band geometry, computed in \sphinxmaketitle from \fboxsep.
\newlength{\nordicbandwidth}
\newlength{\nordicbandheight}

% hyperref is already loaded at this point, so these settings win. Without
% colorlinks it frames every link with a border instead of tinting the text.
\hypersetup{colorlinks=true, allcolors=nordicblue, pdfborder={0 0 0}}

% Contents entries are hyperlinks, so linkcolor is what colours them, and the
% class numbers these pages in roman. Sphinx runs this hook inside the group
% around \tableofcontents and restores arabic numbering after it, so both
% overrides stay confined to the contents pages. Emptying \thepage would
% otherwise leave hyperref writing a "page." destination per page, so its
% page anchors go too.
\makeatletter
\g@addto@macro\sphinxtableofcontentshook{%
  \hypersetup{linkcolor=black, pageanchor=false}%
  \pagenumbering{gobble}%
}
\makeatother

% Chapter heads need the number and title on one line, which means replacing
% the macro the class provides rather than tinting it with \chapterfont.
% Open Sans SemiBold where the system has it, plain regular weight otherwise:
% the heads are blue already, so bold adds nothing.
\IfFontExistsTF{Open Sans SemiBold}
  {\newfontfamily\nordicheadingfont{Open Sans SemiBold}}
  {\newcommand{\nordicheadingfont}{\mdseries}}
\makeatletter
\newcommand{\nordicchapterhead}[1]{%
  \vspace*{50\p@}%
  {\parindent\z@\raggedright\normalfont\sffamily\nordicheadingfont
   \Huge\color{nordicblue}%
   \interlinepenalty\@M
   #1\par\nobreak
   \vskip 40\p@}%
}
% \@chapapp is the "Chapter" label; dropping it leaves "1<gap>Title".
\renewcommand{\@makechapterhead}[1]{%
  \nordicchapterhead{\ifnum\c@secnumdepth>\m@ne\thechapter\hskip0.75em\fi#1}%
}
% Unnumbered chapters, such as the contents and index heads.
\renewcommand{\@makeschapterhead}[1]{\nordicchapterhead{#1}}
\makeatother

\sectionfont{\color{nordicblue}}
\subsectionfont{\color{nordicblue}}

% \fancypagestyle replaces a style outright, so Sphinx's page numbers and
% running heads are restated here alongside the footer logo. 'normal' covers
% body pages and 'plain' the contents pages; the title page uses 'empty' and
% therefore has no footer at all.
\makeatletter
\newcommand{\nordicfooterlogo}{\includegraphics[height=10mm]{logo.png}}
% A 10mm logo is taller than the default footer skip, which fancyhdr warns
% about, so reserve enough room for it.
\setlength{\footskip}{34pt}
\fancypagestyle{normal}{
  \fancyhf{}
  \fancyfoot[C]{{\py@HeaderFamily\thepage}}
  \fancyfoot[R]{\nordicfooterlogo}
  \fancyhead[L]{{\py@HeaderFamily\py@release}}
  \fancyhead[R]{{\py@HeaderFamily\leftmark}}
  % \leftmark carries whatever \chaptermark marked. fancyhdr's stock version
  % uppercases the title and prefixes it with "Chapter N.", so it is replaced
  % with the bare title. The parameter is doubled because this whole body is
  % stored as the \ps@normal macro.
  \renewcommand{\chaptermark}[1]{\markboth{##1}{}}
  \renewcommand{\headrulewidth}{0.4pt}
  \renewcommand{\footrulewidth}{0.4pt}
}
\fancypagestyle{plain}{
  \fancyhf{}
  \fancyfoot[C]{{\py@HeaderFamily\thepage}}
  \fancyfoot[R]{\nordicfooterlogo}
  \renewcommand{\headrulewidth}{0pt}
  \renewcommand{\footrulewidth}{0.4pt}
}
\makeatother

% A parsed-literal block is emitted as sphinxalltt, which is plain alltt with
% no box, so it misses the grey panel that VerbatimColor gives a code block.
% Give it an equivalent panel. 'snugshade*' comes from the framed package that
% Sphinx already loads for warning-style admonitions: it fills the current
% text width, draws no rule and permits a page break inside. It is the variant
% that keeps the panel aligned with the surrounding list indentation, which
% every parsed-literal here relies on, and it takes its padding from \fboxsep,
% matched to verbatimsep above. Hooking the environment from outside leaves
% upstream's catcode setup untouched, and \begingroup keeps \fboxsep and the
% colour from leaking, since the hook runs before the environment opens its
% own group. Sphinx colours nothing in a sphinxalltt, so pre_TeXcolor does not
% reach a parsed-literal and the hook has to set the same grey itself. Both
% values have to stay in step with their code-block counterparts.
\definecolor{nordiccodebg}{RGB}{240,240,240}
\definecolor{nordiccodetext}{RGB}{112,112,112}
\BeforeBeginEnvironment{sphinxalltt}{%
  \begingroup
    \colorlet{shadecolor}{nordiccodebg}%
    \setlength{\fboxsep}{10pt}%
    \begin{snugshade*}%
    \color{nordiccodetext}%
}
\AfterEndEnvironment{sphinxalltt}{%
    \end{snugshade*}%
  \endgroup
}

% The reference prints code examples as plain text, so the PDF drops the
% syntax highlighting Sphinx applies by default. Highlighting also misreads
% these examples: the console, shell and Python lexers all read '#' as the
% start of a comment, so an AT command such as AT#XSOCKET=0 comes out teal and
% italic from the '#' onwards. \PYG is the macro Pygments wraps around every
% token, taking the token type and then the text; keeping only the text leaves
% the whole block upright and in the grey set above, which is also what a
% parsed-literal gets, since that never goes through Pygments. It comes from
% sphinxhighlight.sty, so the redefinition waits for the start of the document
% rather than depending on when Sphinx loads that package. This is a LaTeX
% element, so the HTML keeps its highlighting.
\AtBeginDocument{\def\PYG#1#2{#2}}

% Notice headings, generated because there is one command per notice type. The
% panels themselves come from the sphinxsetup keys above.
""" + notice_title_commands() + "\n",
}

latex_logo = str(DOC_BASE / "images" / "logo.png")

figure_align = 'H'

class RemoveLocalContents(SphinxPostTransform):
    """Drop the per-page ``.. contents::`` topics from the PDF.

    The PDF opens with a full table of contents, so a second one at the head
    of every section only repeats it. HTML keeps them.
    """

    default_priority = 800
    formats = ('latex',)

    def run(self, **kwargs):
        for node in list(self.document.findall(nodes.topic)):
            if 'contents' in node['classes']:
                node.parent.remove(node)


def setup(app):
    app.add_post_transform(RemoveLocalContents)

def docset_exclude_patterns(docset: str) -> list[str]:
    """Return the exclude patterns for a docset.

    The sources are shared, so each docset must exclude the home pages that
    belong to the other docsets. Otherwise they end up outside of any toctree.

    Args:
        docset: Docset name.

    Returns:
        Value for the ``exclude_patterns`` configuration option.
    """

    return COMMON_EXCLUDE_PATTERNS + [
        f"{home}.rst"
        for name, (_, home) in docsets.ALL_DOCSETS.items()
        if name != docset
    ]


def docset_latex_documents(docset: str) -> list[tuple[str, str, str, str, str]]:
    """Return the LaTeX documents of a docset.

    A docset covers a different set of pages than the others and starts from
    its own home page, so each one is a document of its own and needs a Sphinx
    run of its own. The file name is the same for all of them because
    :file:`_scripts/build_docsets.py` builds every docset into its own
    directory, and because the "Download PDF" link of
    :file:`_templates/breadcrumbs.html` is shared by all pages.

    Args:
        docset: Docset name.

    Returns:
        Value for the ``latex_documents`` configuration option.
    """

    title, home = docsets.ALL_DOCSETS[docset]

    return [(home, "ncs-serial-modem.tex", f"{title} Documentation", author, "manual")]
