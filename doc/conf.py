# Configuration file for the Sphinx documentation builder.
#
# For the full list of built-in configuration values, see the documentation:
# https://www.sphinx-doc.org/en/master/usage/configuration.html

from pathlib import Path
import sys
import os

from docutils import nodes
from sphinx.transforms.post_transforms import SphinxPostTransform

# -- Project information -----------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#project-information

project = 'Serial Modem'
copyright = '2026, Nordic Semiconductor'
author = 'Nordic Semiconductor'
version = release = os.environ.get('VERSION', 'latest')

# Paths

DOC_BASE = Path(__file__).absolute()

# -- General configuration ---------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#general-configuration

sys.path.insert(0, str("_extensions"))

extensions = [
    'breathe',
    'sphinx_tabs.tabs',
    'sphinx_togglebutton',
    "sphinxcontrib.jquery",
    "sphinx_copybutton",
    'sphinx.ext.imgconverter',
]

templates_path = ['_templates']
exclude_patterns = ['_build_sphinx', 'Thumbs.db', '.DS_Store']

# -- Options for HTML output -------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#options-for-html-output

html_theme = 'sphinx_ncs_theme'
html_show_sphinx = False

html_theme_options = {'docsets': {},"addons_url": "https://nrfconnect.github.io/ncs-app-index/","bare_metal_url": "","ncs_url": "https://nrfconnectdocs.nordicsemi.com/ncs/latest/nrf/index.html", "ncs_label": "nRF Connect SDK Docs", "logo_url": "https://docs.nordicsemi.com/"}

html_extra_path = ['versions.json']

html_static_path = ['_static']

html_css_files = ['css/custom.css']

## -- Options for Breathe ----------------------------------------------------
# https://breathe.readthedocs.io/en/latest/index.html
#
# WARNING: please, check breathe maintainership status before using this
# extension in production!

breathe_projects = {'ncs-serial-modem': '_build_doxygen/xml'}
breathe_default_project = 'ncs-serial-modem'
breathe_default_members = ('members', )

# Include following files at the end of each .rst file.
rst_epilog = """
.. include:: /links.txt
.. include:: /shortcuts.txt
"""

# -- Options for LaTeX output -------------------------------------------------

latex_engine = 'xelatex'

latex_use_xindy = False

# 'colorrows' is required for the TableRowColorHeader setting below to apply.
latex_table_style = ['booktabs', 'colorrows']

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
    # Sphinx frames code blocks in a 0.4pt black rule over white; the Nordic
    # style is a flat grey panel with no border. Padding measured off the
    # reference is about 11pt left of the text and 9pt above it, and one key
    # covers all four sides. The verbatim* keys blank out the red
    # visible-space and hook-arrow markers Sphinx prints where it wraps a long
    # line, which the reference does not show either.
    # By default a code-block line only breaks at a space or at ASCII
    # punctuation, so a long run of letters and digits with no break point
    # overflows the grey panel into the margin. verbatimforcewraps lets Sphinx
    # fall back to breaking such a line at any character, and maxoverfull=0
    # applies that fallback as soon as the line is wider than the panel rather
    # than tolerating the default three characters of overhang.
    'sphinxsetup': ('TableRowColorHeader={RGB}{0,162,198},'
                    'VerbatimColor={RGB}{240,240,240},'
                    'verbatimborder=0pt,'
                    'verbatimsep=10pt,'
                    'verbatimvisiblespace={},'
                    'verbatimcontinued={},'
                    'verbatimforcewraps=true,'
                    'verbatimmaxoverfull=0'),
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
          {\Huge\@title\par}
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
% upstream's catcode setup untouched, and \begingroup keeps \fboxsep from
% leaking, since the hook runs before the environment opens its own group.
\definecolor{nordiccodebg}{RGB}{240,240,240}
\BeforeBeginEnvironment{sphinxalltt}{%
  \begingroup
    \colorlet{shadecolor}{nordiccodebg}%
    \setlength{\fboxsep}{10pt}%
    \begin{snugshade*}%
}
\AfterEndEnvironment{sphinxalltt}{%
    \end{snugshade*}%
  \endgroup
}

% The console, shell and Python lexers all read '#' as the start of a comment,
% so an AT command example such as AT#XSOCKET=0 is highlighted as a comment
% from the '#' onwards and comes out teal and italic. Emptying the comment
% token macros drops the colour and the shape together, which leaves those
% tokens in the upright black of the surrounding block; every other token type
% keeps its highlighting. The names below are the abbreviations Pygments uses
% for Comment and its subtypes. They are defined in sphinxhighlight.sty, which
% Sphinx loads after this preamble, so the redefinition is deferred to the
% start of the document.
\makeatletter
\AtBeginDocument{%
  \@namedef{PYG@tok@c}{}%
  \@namedef{PYG@tok@c1}{}%
  \@namedef{PYG@tok@ch}{}%
  \@namedef{PYG@tok@cm}{}%
  \@namedef{PYG@tok@cp}{}%
  \@namedef{PYG@tok@cpf}{}%
  \@namedef{PYG@tok@cs}{}%
}
\makeatother
"""
}

latex_logo = "images/logo.png"

latex_documents = [
    ('index', 'ncs-serial-modem.tex', f'{project} Documentation',
     author, 'manual'),
]

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
