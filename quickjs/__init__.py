import json
import threading

import _quickjs


def test():
    return _quickjs.test()


Context = _quickjs.Context
Object = _quickjs.Object
JSException = _quickjs.JSException


class Function:
    def __init__(self, name: str, code: str) -> None:
        self._context = Context()
        self._context.eval(code)
        self._f = self._context.get(name)
        self._lock = threading.Lock()

    def __call__(self, *args):
        with self._lock:
            return self._call(*args)

    def set_memory_limit(self, limit):
        with self._lock:
            return self._context.set_memory_limit(limit)

    def set_time_limit(self, limit):
        with self._lock:
            return self._context.set_time_limit(limit)

    def _call(self, *args):
        def convert_arg(arg):
            if isinstance(arg, (type(None), str, bool, float, int)):
                return arg
            else:
                # More complex objects are passed through JSON.
                return self._context.eval("(" + json.dumps(arg) + ")")

        result = self._f(*[convert_arg(a) for a in args])
        if isinstance(result, Object):
            result = json.loads(result.json())
        return result
