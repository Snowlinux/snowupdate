#!/usr/bin/python

import os
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, "../data")
import package_data_downloader

class PackageDataDownloaderTestCase(unittest.TestCase):

    def setUp(self):
        self.tmpdir = tempfile.mkdtemp()
        # stampdir
        stampdir = os.path.join(self.tmpdir, "stampdir")
        os.makedirs(stampdir)
        package_data_downloader.STAMPDIR = stampdir
        # datadir
        datadir = os.path.join(self.tmpdir, "datadir")
        os.makedirs(datadir)
        package_data_downloader.DATADIR = datadir

    def tearDown(self):
        shutil.rmtree(self.tmpdir)

    def test_wrong_template_translations(self):
	package_data_downloader.NOTIFIER_SOURCE_FILE = '../data/package-data-downloads-failed'
	package_data_downloader.NOTIFIER_FILE = self.tmpdir + "/data-downloads-failed"
	package_data_downloader.NOTIFIER_PERMANENT_SOURCE_FILE = package_data_downloader.NOTIFIER_SOURCE_FILE + '-permanently'
	package_data_downloader.NOTIFIER_PERMANENT_FILE = package_data_downloader.NOTIFIER_FILE + '-permanently'
	package_data_downloader.trigger_update_notifier([], False)
	package_data_downloader.trigger_update_notifier([], True)

    def test_permanently_failed(self):
        # create a bunch of files using the provided mechanism
        test_files = ["foo.permanent-failure", "bar.failure", "baz"]
        for f in test_files:
            package_data_downloader.create_or_update_stampfile(
                os.path.join(package_data_downloader.STAMPDIR, f))
        self.assertEqual(
            sorted(os.listdir(package_data_downloader.STAMPDIR)),
            sorted(test_files))
        # test hook_is_permanently_failed()
        self.assertTrue(
            package_data_downloader.hook_is_permanently_failed("foo"))
        self.assertFalse(
            package_data_downloader.hook_is_permanently_failed("bar"))
        self.assertFalse(
            package_data_downloader.hook_is_permanently_failed("baz"))
        # existing_permanent_failures()
        self.assertEqual(
            package_data_downloader.existing_permanent_failures(),
            ["foo"])

    def test_mark_hook_failed(self):
        # prepare
        package_data_downloader.create_or_update_stampfile(
            os.path.join(package_data_downloader.STAMPDIR, "foo"))
        # temp failure
        package_data_downloader.mark_hook_failed("foo")
        self.assertEqual(os.listdir(package_data_downloader.STAMPDIR),
                         ["foo.failed"])
        self.assertFalse(
            package_data_downloader.hook_is_permanently_failed("foo"))
        # permanent
        package_data_downloader.mark_hook_failed("foo", permanent=True)
        self.assertEqual(os.listdir(package_data_downloader.STAMPDIR),
                         ["foo.permanent-failure"])
        self.assertTrue(
            package_data_downloader.hook_is_permanently_failed("foo"))
        
    def test_get_hook_file_names(self):
        # test that .dpkg-* is ignored
        test_datadir_files = ["foo.dpkg-new", "bar"]
        for name in test_datadir_files:
            with open(os.path.join(package_data_downloader.DATADIR, name), "w"):
                pass
        self.assertEqual(package_data_downloader.get_hook_file_names(),
                         ["bar"])

    def test_trigger_update_notifier(self):
        # write to tmpdir
        package_data_downloader.NOTIFIER_FILE = os.path.join(
            self.tmpdir, "data-downloads-failed")
        # point to local repo file
        package_data_downloader.NOTIFIER_SOURCE_FILE = \
            "../data/package-data-downloads-failed.in"
        package_data_downloader.trigger_update_notifier(
            failures=["foo", "bar"], permanent=False)
        data = open(package_data_downloader.NOTIFIER_FILE).read()
        self.assertEqual(data, """Priority: High
_Name: Failure to download extra data files
Terminal: True
Command: pkexec /usr/lib/update-notifier/package-data-downloader
_Description: 
 The following packages requested additional data downloads after package
 installation, but the data could not be downloaded or could not be
 processed.
 .
 .
   foo, bar
 .
 .
 The download will be attempted again later, or you can try the download
 again now.  Running this command requires an active Internet connection.
""")

            
        

if __name__ == "__main__":
    unittest.main()
