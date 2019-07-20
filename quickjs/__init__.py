import json
import threading

import _quickjs


def test():
    return _quickjs.test()


Context = _quickjs.Context
Object = _quickjs.Object
JSException = _quickjs.JSException
StackOverflow = _quickjs.StackOverflow


class Function:
    def __init__(self, name: str, code: str) -> None:
        self._context = Context()
        self._context.eval(code)
        self._f = self._context.get(name)
        self._lock = threading.Lock()

    def __call__(self, *args, run_gc=True):
        with self._lock:
            return self._call(*args, run_gc=run_gc)

    def set_memory_limit(self, limit):
        with self._lock:
            return self._context.set_memory_limit(limit)

    def set_time_limit(self, limit):
        with self._lock:
            return self._context.set_time_limit(limit)

    def set_max_stack_size(self, limit):
        with self._lock:
            return self._context.set_max_stack_size(limit)

    def memory(self):
        with self._lock:
            return self._context.memory()

    def gc(self):
        """Manually run the garbage collection.

        It will run by default when calling the function unless otherwise specified.
        """
        with self._lock:
            self._context.gc()

    def _call(self, *args, run_gc=True):
        def convert_arg(arg):
            if isinstance(arg, (type(None), str, bool, float, int)):
                return arg
            else:
                # More complex objects are passed through JSON.
                return self._context.eval("(" + json.dumps(arg) + ")")

        try:
            result = self._f(*[convert_arg(a) for a in args])
            if isinstance(result, Object):
                result = json.loads(result.json())            
            return result
        finally:
            if run_gc:
                self._context.gc()
