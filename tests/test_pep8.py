#!/usr/bin/python3
# -*- Mode: Python; indent-tabs-mode: nil; tab-width: 4; coding: utf-8 -*-

# Partly based on a script from Review Board, MIT license; but modified to
# act as a unit test.

from __future__ import print_function

import os
import subprocess
import unittest

CURDIR = os.path.dirname(os.path.abspath(__file__))


class TestPep8Clean(unittest.TestCase):
    """ ensure that the tree is pep8 clean """

    def test_pep8_clean(self):
        # mvo: type -f here to avoid running pep8 on imported files
        #      that are symlinks to other packages
        cmd = 'find %s/.. -type f -name "*.py" | xargs pep8' % CURDIR
        p = subprocess.Popen(
            cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            close_fds=True, shell=True, universal_newlines=True)
        contents = p.communicate()[0].splitlines()
        for line in contents:
            print(line)
        self.assertEqual(0, len(contents))

if __name__ == "__main__":
    import logging
    logging.basicConfig(level=logging.DEBUG)
    unittest.main()
