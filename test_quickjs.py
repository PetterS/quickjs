import unittest

import quickjs


class LoadModule(unittest.TestCase):
    def test_42(self):
        self.assertEqual(quickjs.test(), 42)


class Context(unittest.TestCase):
    def setUp(self):
        self.context = quickjs.Context()

    def test_eval_int(self):
        self.assertEqual(self.context.eval("40 + 2"), 42)

    def test_eval_float(self):
        self.assertEqual(self.context.eval("40.0 + 2.0"), 42.0)

    def test_eval_str(self):
        self.assertEqual(self.context.eval("'4' + '2'"), "42")

    def test_eval_bool(self):
        self.assertEqual(self.context.eval("true || false"), True)

    def test_eval_null(self):
        self.assertIsNone(self.context.eval("null"))

    def test_eval_undefined(self):
        self.assertIsNone(self.context.eval("undefined"))

    def test_wrong_type(self):
        with self.assertRaises(TypeError):
            self.assertEqual(self.context.eval(1), 42)

    def test_context_between_calls(self):
        self.context.eval("x = 40; y = 2;")
        self.assertEqual(self.context.eval("x + y"), 42)

    def test_function(self):
        self.context.eval("""
        function special(x) {
            return 40 + x;
        }
        """)
        self.assertEqual(self.context.eval("special(2)"), 42)


    def test_error(self):
        with self.assertRaisesRegex(quickjs.JSException, "ReferenceError: missing is not defined"):
            self.context.eval("missing + missing")
