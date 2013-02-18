#!/usr/bin/python3
# -*- Mode: Python; indent-tabs-mode: nil; tab-width: 4; coding: utf-8 -*-

import random
import logging
import multiprocessing
import os
import sys
import time
try:
    from urllib.error import HTTPError
    from urllib.request import urlopen
except ImportError:
    from urllib2 import HTTPError, urlopen
import unittest

try:
    from http.server import BaseHTTPRequestHandler
except ImportError:
    from BaseHTTPServer import BaseHTTPRequestHandler
try:
    from socketserver import TCPServer
except ImportError:
    from SocketServer import TCPServer

CURDIR = os.path.dirname(os.path.abspath(__file__))


class SillyProxyRequestHandler(BaseHTTPRequestHandler):
    def do_HEAD(self):
        code = 200
        info = ""
        try:
            f = urlopen(self.path)
            info = f.info()
        except HTTPError as e:
            code = e.code
        s = "HTTP/1.0 %s OK\n%s" % (code, info)
        self.wfile.write(s)
    # well, good enough
    do_GET = do_HEAD

PORT = random.randint(1025, 65535)
Handler = SillyProxyRequestHandler
httpd = TCPServer(("", PORT), Handler)


from UpdateManager.Core.MetaRelease import MetaReleaseCore, Dist


def get_new_dist(current_release):
    """
    common code to test new dist fetching, get the new dist information
    for hardy+1
    """
    meta = MetaReleaseCore()
    #meta.DEBUG = True
    meta.current_dist_name = current_release
    fake_metarelease = os.path.join(CURDIR, "test-data",
                                    "meta-release")
    meta.METARELEASE_URI = "file://%s" % fake_metarelease
    while meta.downloading:
        time.sleep(0.1)
    meta._buildMetaReleaseFile()
    meta.download()
    return meta.new_dist


class TestMetaReleaseCore(unittest.TestCase):

    def setUp(self):
        self.new_dist = None
        self.httpd = multiprocessing.Process(target=httpd.serve_forever)
        self.httpd.start()

    def tearDown(self):
        self.httpd.terminate()
        self.httpd.join()

    def testnewdist(self):
        """ test that upgrades offer the right upgrade path """
        for (current, next) in [("dapper", "edgy"),
                                ("hardy", "lucid"),
                                ("intrepid", "jaunty"),
                                ("jaunty", "karmic"),
                                ("karmic", "lucid")]:
            new_dist = get_new_dist(current)
            self.assertEqual(next, new_dist.name,
                             "New dist name for %s is '%s', "
                             "but expected '%s''" % (current, new_dist.name,
                                                     next))

    def test_url_downloadable(self):
        from UpdateManager.Core.utils import url_downloadable
        logging.debug("proxy 1")
        os.environ["http_proxy"] = "http://localhost:%s/" % PORT
        self.assertTrue(url_downloadable("http://www.ubuntu.com/desktop",
                                         logging.debug),
                        "download with proxy %s failed" %
                        os.environ["http_proxy"])
        logging.debug("proxy 2")
        os.environ["http_proxy"] = "http://localhost:%s" % PORT
        self.assertTrue(url_downloadable("http://www.ubuntu.com/desktop",
                        logging.debug),
                        "download with proxy %s failed" %
                        os.environ["http_proxy"])
        logging.debug("no proxy")
        del os.environ["http_proxy"]
        self.assertTrue(url_downloadable("http://www.ubuntu.com/desktop",
                        logging.debug),
                        "download with no proxy failed")

        logging.debug("no proxy, no valid adress")
        self.assertFalse(url_downloadable("http://www.ubuntu.com/xxx",
                                          logging.debug),
                         "download with no proxy failed")

        logging.debug("proxy, no valid adress")
        os.environ["http_proxy"] = "http://localhost:%s" % PORT
        self.assertFalse(url_downloadable("http://www.ubuntu.com/xxx",
                                          logging.debug),
                         "download with no proxy failed")

    def test_get_uri_query_string(self):
        # test with fake data
        d = Dist("oneiric", "11.10", "2011-10-10", True)
        meta = MetaReleaseCore()
        q = meta._get_release_notes_uri_query_string(d)
        self.assertTrue("os=ubuntu" in q)
        self.assertTrue("ver=11.10" in q)

    def test_html_uri_real(self):
        os.environ["http_proxy"] = ""
        os.environ["META_RELEASE_FAKE_CODENAME"] = "lucid"
        # useDevelopmentRelease=True is only needed until precise is
        # released
        meta = MetaReleaseCore(forceDownload=True, forceLTS=True,
                               useDevelopmentRelease=True)
        while meta.downloading:
            time.sleep(0.1)
        uri = meta.new_dist.releaseNotesHtmlUri
        f = urlopen(uri)
        data = f.read().decode("UTF-8")
        self.assertTrue(len(data) > 0)
        self.assertTrue("<html>" in data)
        del os.environ["META_RELEASE_FAKE_CODENAME"]

if __name__ == '__main__':
    if len(sys.argv) > 1 and sys.argv[1] == "-v":
        logging.basicConfig(level=logging.DEBUG)
    unittest.main()
