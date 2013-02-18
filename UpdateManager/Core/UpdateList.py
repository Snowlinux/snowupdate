# UpdateList.py
# -*- Mode: Python; indent-tabs-mode: nil; tab-width: 4; coding: utf-8 -*-
#
#  Copyright (c) 2004-2013 Canonical
#
#  Author: Michael Vogt <mvo@debian.org>
#          Dylan McCall <dylanmccall@ubuntu.com>
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

from __future__ import print_function

import warnings
warnings.filterwarnings("ignore", "Accessed deprecated property",
                        DeprecationWarning)

from gettext import gettext as _
import apt
import logging
import operator
import itertools
import platform
import subprocess
import os
import random
import glob

from gi.repository import Gio

from UpdateManager.Core import utils


class UpdateItem():
    def __init__(self, pkg, name, icon):
        self.icon = icon
        self.name = name
        self.pkg = pkg


class UpdateGroup(UpdateItem):
    def __init__(self, pkg, name, icon):
        UpdateItem.__init__(self, pkg, name, icon)
        self._items = set()
        self.core_item = None
        if pkg is not None:
            self.core_item = UpdateItem(pkg, name, icon)
            self._items.add(self.core_item)

    @property
    def items(self):
        all_items = []
        all_items.extend(self._items)
        return sorted(all_items, key=lambda a: a.name.lower())

    def add(self, pkg):
        name = utils.get_package_label(pkg)
        icon = Gio.ThemedIcon.new("package")
        self._items.add(UpdateItem(pkg, name, icon))

    def contains(self, item):
        return item in self._items

    def _is_dependency_helper(self, cache, pkg, dep, seen=set()):
        if pkg is None or pkg.candidate is None or pkg in seen:
            return False
        elif pkg.name == dep.name:
            return True
        seen.add(pkg)
        candidate = pkg.candidate
        dependencies = candidate.get_dependencies('Depends', 'Recommends')
        for dependency_pkg in itertools.chain.from_iterable(dependencies):
            if dependency_pkg.name in cache and \
               self._is_dependency_helper(cache, cache[dependency_pkg.name],
                                          dep, seen):
                return True
        return False

    def is_dependency(self, cache, maybe_dep):
        # This is a recursive dependency check.  TODO: We do this many times
        # when grouping packages, and it could be made more efficient.
        seen = set()
        for item in self._items:
            if self._is_dependency_helper(cache, item.pkg, maybe_dep,
                                          seen=seen):
                return True
        return False

    def packages_are_selected(self):
        for item in self.items:
            if item.pkg.marked_install or item.pkg.marked_upgrade:
                return True
        return False

    def selection_is_inconsistent(self):
        pkgs_installing = [item for item in self.items
            if item.pkg.marked_install or item.pkg.marked_upgrade]
        return (len(pkgs_installing) > 0 and
                len(pkgs_installing) < len(self.items))

    def get_total_size(self):
        size = 0
        for item in self.items:
            size += getattr(item.pkg.candidate, "size", 0)
        return size


class UpdateApplicationGroup(UpdateGroup):
    def __init__(self, pkg, application):
        name = application.get_display_name()
        icon = application.get_icon()
        super(UpdateApplicationGroup, self).__init__(pkg, name, icon)


class UpdatePackageGroup(UpdateGroup):
    def __init__(self, pkg):
        name = utils.get_package_label(pkg)
        icon = Gio.ThemedIcon.new("package")
        super(UpdatePackageGroup, self).__init__(pkg, name, icon)


class UpdateSystemGroup(UpdateGroup):
    def __init__(self, cache):
        # Translators: the %s is a distro name, like 'Ubuntu' and 'base' as in
        # the core components and packages.
        name = _("%s base") % utils.get_ubuntu_flavor_name(cache=cache)
        icon = Gio.ThemedIcon.new("distributor-logo")
        super(UpdateSystemGroup, self).__init__(None, name, icon)


class UpdateOrigin():
    def __init__(self, desc, importance):
        self.packages = []
        self.importance = importance
        self.description = desc


class UpdateList():
    """
    class that contains the list of available updates in
    self.pkgs[origin] where origin is the user readable string
    """

    # the key in the debian/control file used to add the phased
    # updates percentage
    PHASED_UPDATES_KEY = "Phased-Update-Percentage"

    # the file that contains the uniq machine id
    UNIQ_MACHINE_ID_FILE = "/var/lib/dbus/machine-id"

    APP_INSTALL_PATH = "/usr/share/app-install/desktop"

    # the configuration key to turn phased-updates always on
    ALWAYS_INCLUDE_PHASED_UPDATES = (
        "Update-Manager::Always-Include-Phased-Updates")
    # ... or always off
    NEVER_INCLUDE_PHASED_UPDATES = (
        "Update-Manager::Never-Include-Phased-Updates")

    def __init__(self, parent, dist=None):
        self.dist = dist if dist else platform.dist()[2]
        self.distUpgradeWouldDelete = 0
        self.update_groups = []
        self.security_groups = []
        self.num_updates = 0
        self.random = random.Random()
        # a stable machine uniq id
        with open(self.UNIQ_MACHINE_ID_FILE) as f:
            self.machine_uniq_id = f.read()

        if 'XDG_DATA_DIRS' in os.environ and os.environ['XDG_DATA_DIRS']:
            data_dirs = os.environ['XDG_DATA_DIRS']
        else:
            data_dirs= '/usr/local/share/:/usr/share/'
        self.application_dirs = [os.path.join(base, 'applications')
                                 for base in data_dirs.split(':')]

        if 'XDG_CURRENT_DESKTOP' in os.environ:
            self.current_desktop = os.environ.get('XDG_CURRENT_DESKTOP')
        else:
            self.current_desktop = ''

    def _file_is_application(self, file_path):
        file_path = os.path.abspath(file_path)
        is_application = False
        for app_dir in self.application_dirs:
            is_application = is_application or file_path.startswith(app_dir)
        extension = os.path.splitext(file_path)[1]
        is_application = is_application and (extension == '.desktop')
        return is_application

    def _rate_application_for_package(self, application, pkg):
        score = 0
        desktop_file = os.path.basename(application.get_filename())
        application_id = os.path.splitext(desktop_file)[0]

        if application.should_show():
            score += 1

            if application_id == pkg.name:
                score += 5

        return score

    def _get_application_for_package(self, pkg):
        desktop_files = []
        rated_applications = []

        for installed_file in pkg.installed_files:
            if self._file_is_application(installed_file):
                desktop_files.append(installed_file)

        app_install_pattern = os.path.join(self.APP_INSTALL_PATH,
                                           '%s:*' % pkg.name)
        for desktop_file in glob.glob(app_install_pattern):
            desktop_files.append(desktop_file)

        for desktop_file in desktop_files:
            try:
                application = Gio.DesktopAppInfo.new_from_filename(
                    desktop_file)
                application.set_desktop_env(self.current_desktop)
            except Exception as e:
                print("Error loading .desktop file %s: %s" %
                      (installed_file, e))
                continue
            score = self._rate_application_for_package(application, pkg)
            if score > 0:
                rated_applications.append((score, application))

        rated_applications.sort(key=lambda app: app[0], reverse=True)
        if len(rated_applications) > 0:
            return rated_applications[0][1]
        else:
            return None

    def _is_security_update(self, pkg):
        """ This will test if the pkg is a security update.
            This includes if there is a newer version in -updates, but also
            an older update available in -security.  For example, if
            installed pkg A v1.0 is available in both -updates (as v1.2) and
            -security (v1.1). we want to display it as a security update.

            :return: True if the update comes from the security pocket
        """
        if not self.dist:
            return False
        inst_ver = pkg._pkg.current_ver
        for ver in pkg._pkg.version_list:
            # discard is < than installed ver
            if (inst_ver and
                    apt.apt_pkg.version_compare(ver.ver_str,
                                                inst_ver.ver_str) <= 0):
                continue
            # check if we have a match
            for (verFileIter, index) in ver.file_list:
                if verFileIter.archive == "%s-security" % self.dist and \
                        verFileIter.origin == "Ubuntu":
                    indexfile = pkg._pcache._list.find_index(verFileIter)
                    if indexfile:  # and indexfile.IsTrusted:
                        return True
        return False

    def _is_ignored_phased_update(self, pkg):
        """ This will test if the pkg is a phased update and if
            it needs to get installed or ignored.

            :return: True if the updates should be ignored
        """
        # allow the admin to override this
        if apt.apt_pkg.config.find_b(
                self.ALWAYS_INCLUDE_PHASED_UPDATES, False):
            return False

        if self.PHASED_UPDATES_KEY in pkg.candidate.record:
            if apt.apt_pkg.config.find_b(
                    self.NEVER_INCLUDE_PHASED_UPDATES, False):
                logging.info("holding back phased update per configuration")
                return True

            # its important that we always get the same result on
            # multiple runs of the update-manager, so we need to
            # feed a seed that is a combination of the pkg/ver/machine
            self.random.seed("%s-%s-%s" % (
                pkg.name, pkg.candidate.version,
                self.machine_uniq_id))
            threshold = pkg.candidate.record[self.PHASED_UPDATES_KEY]
            percentage = self.random.randint(0, 100)
            if percentage > int(threshold):
                logging.info("holding back phased update (%s < %s)" % (
                    threshold, percentage))
                return True
        return False

    def _get_linux_packages(self):
        "Return all binary packages made by the linux-meta source package"
        # Hard code this rather than generate from source info in cache because
        # that might only be available if we have deb-src lines.  I think we
        # could also generate it by iterating over all the binary package info
        # we have, but that is costly.  These don't change often.
        return ['linux', 'linux-image', 'linux-headers-generic',
                'linux-image-generic', 'linux-generic',
                'linux-headers-generic-pae', 'linux-image-generic-pae',
                'linux-generic-pae', 'linux-headers-omap', 'linux-image-omap',
                'linux-omap', 'linux-headers-server', 'linux-image-server',
                'linux-server', 'linux-signed-image-generic',
                'linux-signed-generic', 'linux-headers-virtual',
                'linux-image-virtual', 'linux-virtual',
                'linux-image-extra-virtual']

    def _make_groups(self, cache, pkgs):
        pkgs_by_source = {}
        ungrouped_pkgs = []
        app_groups = []
        pkg_groups = []

        # Index packages by source package name
        for pkg in pkgs:
            srcpkg = pkg.candidate.source_name
            pkgs_by_source.setdefault(srcpkg, []).append(pkg)

        for srcpkg, pkgs in pkgs_by_source.items():
            for pkg in pkgs:
                app = self._get_application_for_package(pkg)
                if app is not None:
                    app_group = UpdateApplicationGroup(pkg, app)
                    app_groups.append(app_group)
                else:
                    ungrouped_pkgs.append(pkg)

        # Stick together applications and their immediate dependencies
        for pkg in list(ungrouped_pkgs):
            dep_groups = []
            for group in app_groups:
                if group.is_dependency(cache, pkg):
                    dep_groups.append(group)
                    if len(dep_groups) > 1:
                        break
            if len(dep_groups) == 1:
                dep_groups[0].add(pkg)
                ungrouped_pkgs.remove(pkg)

        # Separate out system base packages
        system_group = None
        meta_group = UpdateGroup(None, None, None)
        flavor_package = utils.get_ubuntu_flavor_package(cache=cache)
        meta_pkgs = [flavor_package, "ubuntu-standard", "ubuntu-minimal"]
        meta_pkgs.extend(self._get_linux_packages())
        for pkg in meta_pkgs:
            if pkg in cache:
                meta_group.add(cache[pkg])
        for pkg in ungrouped_pkgs:
            if meta_group.contains(pkg) or meta_group.is_dependency(cache, pkg):
                if system_group is None:
                    system_group = UpdateSystemGroup(cache)
                system_group.add(pkg)
            else:
                pkg_groups.append(UpdatePackageGroup(pkg))

        app_groups.sort(key=lambda a: a.name.lower())
        pkg_groups.sort(key=lambda a: a.name.lower())
        if system_group:
            pkg_groups.append(system_group)

        return app_groups + pkg_groups

    def update(self, cache):
        self.held_back = []

        # do the upgrade
        self.distUpgradeWouldDelete = cache.saveDistUpgrade()

        security_pkgs = []
        upgrade_pkgs = []

        # Find all upgradable packages
        for pkg in cache:
            if pkg.is_upgradable or pkg.marked_install:
                if getattr(pkg.candidate, "origins", None) is None:
                    # can happen for e.g. locked packages
                    # FIXME: do something more sensible here (but what?)
                    print("WARNING: upgradable but no candidate.origins?!?: ",
                          pkg.name)
                    continue

                # see if its a phased update and *not* a security update
                is_security_update = self._is_security_update(pkg)
                if (not is_security_update and
                        self._is_ignored_phased_update(pkg)):
                    continue

                if is_security_update:
                    security_pkgs.append(pkg)
                else:
                    upgrade_pkgs.append(pkg)
                self.num_updates = self.num_updates + 1

            if pkg.is_upgradable and not (pkg.marked_upgrade or
                                          pkg.marked_install):
                self.held_back.append(pkg.name)

        self.update_groups = self._make_groups(cache, upgrade_pkgs)
        self.security_groups = self._make_groups(cache, security_pkgs)
