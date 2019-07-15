import json

import _quickjs


def test():
    return _quickjs.test()


Context = _quickjs.Context
Object = _quickjs.Object
JSException = _quickjs.JSException


class Function:
    def __init__(self, name: str, code: str) -> None:
        self.context = Context()
        self.context.eval(code)
        self.f = self.context.get(name)

    def __call__(self, *args):
        def convert_arg(arg):
            if isinstance(arg, (str, int)):
                return arg
            else:
                # More complex objects are passed through JSON.
                return self.context.eval("(" + json.dumps(arg) + ")")

        result = self.f(*[convert_arg(a) for a in args])
        if isinstance(result, Object):
            result = json.loads(result.json())
        return result