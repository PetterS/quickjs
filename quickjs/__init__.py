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
        result = self.f(*args)
        if isinstance(result, Object):
            result = json.loads(result.json())
        return result