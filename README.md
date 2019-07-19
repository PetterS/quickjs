Just install with

	pip install quickjs

Windows binaries are provided for Python 3.7, 64-bit.

# Usage

```python
from quickjs import Function

f = Function("f", """
    function adder(a, b) {
        return a + b;
    }
    
    function f(a, b) {
        return adder(a, b);
    }
    """)

assert f(1, 2) == 3
```

Simple types like int, floats and strings are converted directly. Other types (dicts, lists) are converted via JSON by the `Function` class.
The library is thread-safe if `Function` is used (it has locks). If the `Context` class is used directly, two threads can not use the same context or its objects directly.

Both `Function` and `Context` expose `set_memory_limit` and `set_time_limit` functions that allow limits for code running in production.

For full functionality, please see `test_quickjs.py`.

# Developing
Use a `pipenv shell` and `make test` should work from inside its virtual environment.
