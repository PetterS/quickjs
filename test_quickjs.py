import unittest

import quickjs


class LoadModule(unittest.TestCase):
    def test_42(self):
        self.assertEqual(quickjs.test(), 42)
