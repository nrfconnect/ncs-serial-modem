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
]

templates_path = ['_templates']
exclude_patterns = ['_build_sphinx', 'Thumbs.db', '.DS_Store']

# -- Options for HTML output -------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#options-for-html-output

html_theme = 'sphinx_ncs_theme'
html_show_sphinx = False

html_theme_options = {'docsets': {},"addons_url": "https://nrfconnect.github.io/ncs-app-index/","bare_metal_url": "","ncs_url": "https://nrfconnectdocs.nordicsemi.com/ncs/latest/nrf/index.html", "ncs_label": "nRF Connect SDK Docs", "logo_url": "https://nordic-semiconductor.fluidtopics.net/"}

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
