#!/usr/bin/env python
# -*- Mode: Python; indent-tabs-mode: nil; tab-width: 4; coding: utf-8 -*-
# (c) 2005-2012 Canonical, GPL
# (C) 2008-2009 Sebastian Heinlein <devel@glatzor.de>

from __future__ import print_function

from gi.repository import Gtk
from gi.repository import Gdk

from aptdaemon import client, errors
from defer import inline_callbacks
from aptdaemon.gtk3widgets import (AptCancelButton,
                                   AptConfigFileConflictDialog,
                                   AptDetailsExpander,
                                   AptMediumRequiredDialog,
                                   AptProgressBar)
from aptdaemon.enums import (EXIT_SUCCESS,
                             EXIT_FAILED,
                             STATUS_COMMITTING,
                             get_error_description_from_enum,
                             get_error_string_from_enum,
                             get_status_string_from_enum)

from UpdateManager.backend import InstallBackend
from UpdateManager.UnitySupport import UnitySupport

from gettext import gettext as _

import apt
import dbus
import sys


class InstallBackendAptdaemon(InstallBackend):

    """Makes use of aptdaemon to refresh the cache and to install updates."""

    def __init__(self, datadir, window_main):
        InstallBackend.__init__(self, datadir, window_main)
        self.client = client.AptClient()
        self.unity = UnitySupport()
        self._expanded_size = None
        self.button_cancel = None

    def close(self):
        if self.button_cancel:
            self.button_cancel.clicked()
            return True
        else:
            return False

    @inline_callbacks
    def update(self):
        """Refresh the package list"""
        try:
            apt.apt_pkg.pkgsystem_unlock()
        except SystemError:
            pass
        try:
            trans = yield self.client.update_cache(defer=True)
            yield self._run_in_dialog(trans, self.UPDATE,
                                      _("Checking for updates…"),
                                      False, False)
        except errors.NotAuthorizedError:
            self.emit("action-done", self.UPDATE, False, False, None, None)
        except:
            self.emit("action-done", self.UPDATE, True, False, None, None)
            raise

    @inline_callbacks
    def commit(self, pkgs_install, pkgs_upgrade, close_on_done):
        """Commit a list of package adds and removes"""
        try:
            apt.apt_pkg.pkgsystem_unlock()
        except SystemError:
            pass
        try:
            reinstall = remove = purge = downgrade = []
            trans = yield self.client.commit_packages(
                pkgs_install, reinstall, remove, purge, pkgs_upgrade,
                downgrade, defer=True)
            trans.connect("progress-changed", self._on_progress_changed)
            yield self._run_in_dialog(trans, self.INSTALL,
                                      _("Installing updates…"),
                                      True, close_on_done)
        except errors.NotAuthorizedError as e:
            self.emit("action-done", self.INSTALL, False, False, None, None)
        except dbus.DBusException as e:
            #print(e, e.get_dbus_name())
            if e.get_dbus_name() != "org.freedesktop.DBus.Error.NoReply":
                raise
            self.emit("action-done", self.INSTALL, False, False, None, None)
        except Exception as e:
            self.emit("action-done", self.INSTALL, True, False, None, None)
            raise

    def _on_progress_changed(self, trans, progress):
        #print("_on_progress_changed", progress)
        self.unity.set_progress(progress)

    def _on_details_changed(self, trans, details, label_details):
        label_details.set_label(details)

    def _on_status_changed(self, trans, status, label_details, expander):
        label_details.set_label(get_status_string_from_enum(status))
        # Also resize the window if we switch from download details to
        # the terminal window
        if (status == STATUS_COMMITTING and expander and
                expander.terminal.get_visible()):
            self._resize_to_show_details(expander)

    @inline_callbacks
    def _run_in_dialog(self, trans, action, header, show_details,
                       close_on_done):
        builder = Gtk.Builder()
        builder.set_translation_domain("update-manager")
        builder.add_from_file(self.datadir + "/gtkbuilder/UpdateProgress.ui")

        label_header = builder.get_object("label_header")
        label_header.set_label(header)

        progressbar = AptProgressBar(trans)
        progressbar.show()
        progressbar_slot = builder.get_object("progressbar_slot")
        progressbar_slot.add(progressbar)

        self.button_cancel = AptCancelButton(trans)
        if action == self.UPDATE:
            self.button_cancel.set_label(Gtk.STOCK_STOP)
        self.button_cancel.show()
        button_cancel_slot = builder.get_object("button_cancel_slot")
        button_cancel_slot.add(self.button_cancel)

        label_details = builder.get_object("label_details")

        if show_details:
            expander = AptDetailsExpander(trans)
            expander.set_vexpand(True)
            expander.set_hexpand(True)
            expander.show_all()
            expander.connect("notify::expanded", self._on_expanded)
            expander_slot = builder.get_object("expander_slot")
            expander_slot.add(expander)
            expander_slot.show()
        else:
            expander = None

        trans.connect("status-details-changed", self._on_details_changed,
                      label_details)
        trans.connect("status-changed", self._on_status_changed, label_details,
                      expander)
        trans.connect("finished", self._on_finished, action, close_on_done)
        trans.connect("medium-required", self._on_medium_required)
        trans.connect("config-file-conflict", self._on_config_file_conflict)

        yield trans.set_debconf_frontend("gnome")
        yield trans.run()

        self.window_main.push(builder.get_object("pane_update_progress"),
                              self)

        self.window_main.get_window().set_functions(Gdk.WMFunction.MOVE |
                                                    Gdk.WMFunction.RESIZE |
                                                    Gdk.WMFunction.MINIMIZE)

    def _on_expanded(self, expander, param):
        # Make the dialog resizable if the expander is expanded
        # try to restore a previous size
        if not expander.get_expanded():
            self._expanded_size = (expander.terminal.get_visible(),
                                   self.window_main.get_size())
            self.window_main.set_resizable(False)
        elif self._expanded_size:
            self.window_main.set_resizable(True)
            term_visible, (stored_width, stored_height) = self._expanded_size
            # Check if the stored size was for the download details or
            # the terminal widget
            if term_visible != expander.terminal.get_visible():
                # The stored size was for the download details, so we need
                # get a new size for the terminal widget
                self._resize_to_show_details(expander)
            else:
                self.window_main.resize(stored_width, stored_height)
        else:
            self.window_main.set_resizable(True)
            self._resize_to_show_details(expander)

    def _resize_to_show_details(self, expander):
        """Resize the window to show the expanded details.

        Unfortunately the expander only expands to the preferred size of the
        child widget (e.g showing all 80x24 chars of the Vte terminal) if
        the window is rendered the first time and the terminal is also visible.
        If the expander is expanded afterwards the window won't change its
        size anymore. So we have to do this manually. See LP#840942
        """
        win_width, win_height = self.window_main.get_size()
        exp_width = expander.get_allocation().width
        exp_height = expander.get_allocation().height
        if expander.terminal.get_visible():
            terminal_width = expander.terminal.get_char_width() * 80
            terminal_height = expander.terminal.get_char_height() * 24
            self.window_main.resize(terminal_width - exp_width + win_width,
                                    terminal_height - exp_height + win_height)
        else:
            self.window_main.resize(win_width + 100, win_height + 200)

    def _on_medium_required(self, transaction, medium, drive):
        dialog = AptMediumRequiredDialog(medium, drive, self.window_main)
        res = dialog.run()
        dialog.hide()
        if res == Gtk.ResponseType.OK:
            transaction.provide_medium(medium)
        else:
            transaction.cancel()

    def _on_config_file_conflict(self, transaction, old, new):
        dialog = AptConfigFileConflictDialog(old, new, self.window_main)
        res = dialog.run()
        dialog.hide()
        if res == Gtk.ResponseType.YES:
            transaction.resolve_config_file_conflict(old, "replace")
        else:
            transaction.resolve_config_file_conflict(old, "keep")

    def _on_finished(self, trans, status, action, close_on_done):
        error_string = None
        error_desc = None
        if status == EXIT_FAILED:
            error_string = get_error_string_from_enum(trans.error.code)
            error_desc = get_error_description_from_enum(trans.error.code)
        elif status == EXIT_SUCCESS and close_on_done:
            sys.exit(0)
        # tell unity to hide the progress again
        self.unity.set_progress(-1)
        self.emit("action-done", action, True, status == EXIT_SUCCESS,
                  error_string, error_desc)

if __name__ == "__main__":
    b = InstallBackendAptdaemon(None)
    b.commit(["2vcard"], [], False)
    Gtk.main()
