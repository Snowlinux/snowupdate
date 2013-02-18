#!/usr/bin/env python
# -*- Mode: Python; indent-tabs-mode: nil; tab-width: 4; coding: utf-8 -*-

"""Integration of package managers into UpdateManager"""
# (c) 2005-2009 Canonical, GPL

from __future__ import absolute_import

import os

from gi.repository import GObject


class InstallBackend(GObject.GObject):
    """The abstract backend that can install/remove packages"""

    __gsignals__ = {"action-done": (GObject.SignalFlags.RUN_FIRST,
                                    None,
                                    (GObject.TYPE_INT,      # action id
                                     GObject.TYPE_BOOLEAN,  # authorized
                                     GObject.TYPE_BOOLEAN,  # success
                                     GObject.TYPE_STRING,   # error string
                                     GObject.TYPE_STRING)   # error desc
                                    ),
                    }

    (INSTALL, UPDATE) = range(2)

    def __init__(self, datadir, window_main):
        """init backend
        takes a gtk main window as parameter
        """
        GObject.GObject.__init__(self)
        self.datadir = datadir
        self.window_main = window_main

    def commit(self, pkgs_install, pkgs_upgrade, close_on_done):
        """Commit the cache changes """
        raise NotImplemented

    def update(self):
        """Run a update to refresh the package list"""
        raise NotImplemented


def get_backend(*args, **kwargs):
    """Select and return a package manager backend."""
    # try aptdaemon
    if (os.path.exists("/usr/sbin/aptd") and
            not "UPDATE_MANAGER_FORCE_BACKEND_SYNAPTIC" in os.environ):
        # check if the gtkwidgets are installed as well
        try:
            from .InstallBackendAptdaemon import InstallBackendAptdaemon
            return InstallBackendAptdaemon(*args, **kwargs)
        except ImportError:
            import logging
            logging.exception("importing aptdaemon")
    # try synaptic
    if (os.path.exists("/usr/sbin/synaptic") and
            not "UPDATE_MANAGER_FORCE_BACKEND_APTDAEMON" in os.environ):
        from .InstallBackendSynaptic import InstallBackendSynaptic
        return InstallBackendSynaptic(*args, **kwargs)
    # nothing found, raise
    raise Exception("No working backend found, please try installing "
                    "synaptic or aptdaemon")
