# UpdateProgress.py
# -*- Mode: Python; indent-tabs-mode: nil; tab-width: 4; coding: utf-8 -*-
#
#  Copyright (c) 2004-2012 Canonical
#                2004 Michiel Sikkes
#                2005 Martin Willemoes Hansen
#                2010 Mohamed Amine IL Idrissi
#
#  Author: Michiel Sikkes <michiel@eyesopened.nl>
#          Michael Vogt <mvo@debian.org>
#          Martin Willemoes Hansen <mwh@sysrq.dk>
#          Mohamed Amine IL Idrissi <ilidrissiamine@gmail.com>
#          Alex Launi <alex.launi@canonical.com>
#          Michael Terry <michael.terry@canonical.com>
#
#  This program is free software; you can redistribute it and/or
#  modify it under the terms of the GNU General Public License as
#  published by the Free Software Foundation; either version 2 of the
#  License, or (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program; if not, write to the Free Software
#  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
#  USA

from __future__ import absolute_import, print_function

import warnings
warnings.filterwarnings("ignore", "Accessed deprecated property",
                        DeprecationWarning)

import os

from .backend import get_backend

from .Core.utils import inhibit_sleep, allow_sleep


class UpdateProgress(object):

    def __init__(self, app):
        self.window_main = app
        self.datadir = app.datadir
        self.options = app.options

        # Used for inhibiting power management
        self.sleep_cookie = None
        self.sleep_dev = None

        # get the install backend
        self.install_backend = get_backend(self.datadir, self.window_main)
        self.install_backend.connect("action-done", self._on_backend_done)

    def invoke_manager(self):
        # don't display apt-listchanges
        os.environ["APT_LISTCHANGES_FRONTEND"] = "none"

        # Do not suspend during the update process
        (self.sleep_dev, self.sleep_cookie) = inhibit_sleep()

        self.install_backend.update()

    def _on_backend_done(self, backend, action, authorized, success,
                         error_string, error_desc):
        # Allow suspend after synaptic is finished
        if self.sleep_cookie:
            allow_sleep(self.sleep_dev, self.sleep_cookie)
            self.sleep_cookie = self.sleep_dev = None

        if error_string:
            self.window_main.start_error(True, error_string, error_desc)
        else:
            self.window_main.start_available(not success)

    def main(self):
        self.invoke_manager()
