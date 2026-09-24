.. _SM_AT_syntax:

AT command syntax
#################

The AT Commands have standardized syntax rules.

Words enclosed in <angle brackets> are references to syntactical elements.
Words enclosed in [square brackets] represent optional items that can be left out of the command line at the specified point.
The brackets are not used when the words appear in the command line.

``<CR>``, ``<LF>`` and ``<CR><LF>`` are allowed in an AT command sent by an application.

A string type parameter input must be enclosed between quotation marks (``"string"``).

There are 3 types of AT commands:

* Set command ``<CMD>[=...]``.
  Set commands set values or perform actions.
* Read command ``<CMD>?``.
  Read commands check the current values of subparameters.
* Test command ``<CMD>=?``.
  Test commands test the existence of the command and provide information about the type of its subparameters.
  Some test commands can also have other functionality.

AT responds to all commands with a final response.

The maximum AT command length is 8190 bytes, including the terminator character.
