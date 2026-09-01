# Configuration file for the Sphinx documentation builder.
#
# For the full list of built-in configuration values, see the documentation:
# https://www.sphinx-doc.org/en/master/usage/configuration.html

from pathlib import Path
import sys
import os

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
    # 'oneside' drops the blank verso pages; 'openany' lets chapters start on
    # either side instead of forcing them onto a right-hand page.
    'extraclassoptions': 'openany,oneside',
    'sphinxsetup': 'TableRowColorHeader={RGB}{0,162,198}',
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
  \noindent\colorbox{nordicblue}{%
    \begin{minipage}[t][0.55\textheight][b]{\dimexpr\textwidth-2\fboxsep\relax}
      \sffamily\color{white}
      {\Huge\@title\par}
      \vspace{2.5em}
      \begin{flushright}
        {\Large\bfseries\@author\par}
        \vspace{0.4em}
        {\large\py@release\releaseinfo\par}
      \end{flushright}
      \vspace{2em}
    \end{minipage}%
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

\definecolor{nordicblue}{RGB}{0,162,198}

% hyperref is already loaded at this point, so these settings win. Without
% colorlinks it frames every link with a border instead of tinting the text.
\hypersetup{colorlinks=true, allcolors=nordicblue, pdfborder={0 0 0}}

\chapterfont{\color{nordicblue}}
\sectionfont{\color{nordicblue}}
\subsectionfont{\color{nordicblue}}

% \fancypagestyle replaces a style outright, so Sphinx's page numbers and
% running heads are restated here alongside the footer logo. 'normal' covers
% body pages and 'plain' the contents pages; the title page uses 'empty' and
% therefore has no footer at all.
\makeatletter
\newcommand{\nordicfooterlogo}{\includegraphics[height=10mm]{logo.png}}
\fancypagestyle{normal}{
  \fancyhf{}
  \fancyfoot[L]{{\py@HeaderFamily\thepage}}
  \fancyfoot[C]{{\py@HeaderFamily\nouppercase{\rightmark}}}
  \fancyfoot[R]{\nordicfooterlogo}
  \fancyhead[R]{{\py@HeaderFamily \@title\sphinxheadercomma\py@release}}
  \renewcommand{\headrulewidth}{0.4pt}
  \renewcommand{\footrulewidth}{0.4pt}
}
\fancypagestyle{plain}{
  \fancyhf{}
  \fancyfoot[L]{{\py@HeaderFamily\thepage}}
  \fancyfoot[R]{\nordicfooterlogo}
  \renewcommand{\headrulewidth}{0pt}
  \renewcommand{\footrulewidth}{0.4pt}
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
