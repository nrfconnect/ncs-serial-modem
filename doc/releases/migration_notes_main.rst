Migration notes for |SM| ``main`` branch
########################################

.. contents::
   :local:
   :depth: 3

This document lists the changes in the ``main`` branch that are not compatible with the v2.x.x releases and the ``v2-branch``.
These changes are not part of any release yet, and the version of the release that will include them has not been decided.
The list can still change before the release.

If you use a v2.x.x release or build from the ``v2-branch``, you do not need to make any of these changes.

Required changes
****************

The following changes are mandatory to make your application work in the same way as in previous releases.

* ``AT#XPING`` - The ``<cid>`` parameter is now the ``+CGDCONT`` cid, instead of the PDN ID.
  The cid and the PDN ID are the same for the default PDN connection (``0``), and also for the other PDN connections if the modem has numbered them alike.
  If they differ, pass the cid and not the PDN ID.
  Previously, the value was used as the PDN ID, so a PDN ID that differs from the cid worked and the cid did not.

  You can find the PDN ID of a cid with ``AT%XGETPDNID=<cid>``.
  The command fails with an error if the modem does not know the cid.

  See :ref:`SM_AT_ICMP` for more information.
