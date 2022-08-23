from typing import Any, Callable, Literal, TypedDict


def test() -> Literal[1]: ...

JSValue = None | bool | int | float | str | Object

class Object():
    def json(self) -> str: ...
    def __call__(self, *args: Any) -> Any: ...

class MemoryDict(TypedDict):
    malloc_size: int
    malloc_limit: int
    memory_used_size: int
    malloc_count: int
    memory_used_count: int
    atom_count: int
    atom_size: int
    str_count: int
    str_size: int
    obj_count: int
    obj_size: int
    prop_count: int
    prop_size: int
    shape_count: int
    shape_size: int
    js_func_count: int
    js_func_size: int
    js_func_code_size: int
    js_func_pc2line_count: int
    js_func_pc2line_size: int
    c_func_count: int
    array_count: int
    fast_array_count: int
    fast_array_elements: int
    binary_object_count: int
    binary_object_size: int

class Context:
    def eval(self, code: str) -> JSValue: ...
    def module(self, code: str) -> JSValue: ...
    def execute_pending_job(self) -> bool: ...
    def parse_json(self, data: str) -> JSValue: ...
    def get(self, name: str) -> JSValue: ...
    def set(self, name: str, item: JSValue) -> None: ...
    def set_memory_limit(self, limit: int) -> None: ...
    def set_time_limit(self, limit: int) -> None: ...
    def set_max_stack_size(self, limit: int) -> None: ...
    def memory(self) -> MemoryDict: ...
    def gc(self) -> None: ...
    def add_callable(self, name: str, callable: Callable[..., Any]) -> None: ...
    @property
    def globalThis(self) -> Object: ...

class JSException(Exception):
    pass

class StackOverflow(Exception):
    pass
