#!/usr/bin/python3
# -*- Mode: Python; indent-tabs-mode: nil; tab-width: 4; coding: utf-8 -*-

import apt
import logging
import os
import sys
import unittest

from mock import Mock

from UpdateManager.Core.MyCache import MyCache

CURDIR = os.path.dirname(os.path.abspath(__file__))


class TestCache(unittest.TestCase):

    def setUp(self):
        # Whenever a test will initialize apt_pkg, we must set the architecture
        # to amd64, because our various test files assume that.  Even if this
        # test doesn't use those files, apt_pkg is only initialized once across
        # tests, so we must be consistent.
        real_arch = apt.apt_pkg.config.find("APT::Architecture")
        apt.apt_pkg.config.set("APT::Architecture", "amd64")
        self.addCleanup(
            lambda: apt.apt_pkg.config.set("APT::Architecture", real_arch))

        # We don't need anything special, but since we modify architecture
        # above, we ought to point to an aptroot that matches the arch
        aptroot = os.path.join(CURDIR, "aptroot-update-list-test")

        self.cache = MyCache(None, rootdir=aptroot)

    def test_https_and_creds_in_changelog_uri(self):
        # credentials in https locations are not supported as they can
        # be man-in-the-middled because of the lack of cert checking in
        # urllib2
        pkgname = "apt"
        uri = "https://user:pass$word@ubuntu.com/foo/bar"
        mock = Mock()
        mock.return_value = uri
        self.cache._guess_third_party_changelogs_uri_by_binary = mock
        mock = Mock()
        mock.return_value = uri
        self.cache._guess_third_party_changelogs_uri_by_source = mock
        self.cache.all_changes[pkgname] = "header\n"
        self.cache._fetch_changelog_for_third_party_package(pkgname)
        self.assertEqual(
            self.cache.all_changes[pkgname],
            "header\n"
            "This update does not come from a source that supports "
            "changelogs.")

if __name__ == '__main__':
    if len(sys.argv) > 1 and sys.argv[1] == "-v":
        logging.basicConfig(level=logging.DEBUG)
    unittest.main()
