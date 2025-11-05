#include <Python.h>
#include <time.h>

#include "quickjs.h"


typedef struct {
    PyTypeObject *QuickJSContextType;
    PyTypeObject *QuickJSObjectType;
	PyObject* JSException;
	PyObject* StackOverflowException;
 	/* this should be threadsafe and not global... */
	JSClassID js_python_function_class_id;
} mod_state;

static inline mod_state *
get_mod_state(PyObject *mod)
{
    mod_state *state = (mod_state *)PyModule_GetState(mod);
    assert(state != NULL);
    return state;
}

static inline mod_state *
get_mod_state_by_cls(PyTypeObject *cls)
{
    mod_state *state = (mod_state *)PyType_GetModuleState(cls);
    assert(state != NULL);
    return state;
}


// Node of Python callable that the context needs to keep available.
typedef struct PythonCallableNode PythonCallableNode;
struct PythonCallableNode {
	PyObject *obj;
	PythonCallableNode *prev;
	PythonCallableNode *next;
};

// Keeps track of the time if we are using a time limit.
typedef struct {
	clock_t start;
	clock_t limit;
} InterruptData;

// The data of the type _quickjs.Context.

/* Renamed to PyJsContextObject to be more Generic and modernized */
typedef struct {
	PyObject_HEAD 
	JSRuntime *runtime;
	JSContext *context;
	int has_time_limit;
	clock_t time_limit;
	// Used when releasing the GIL.
	PyThreadState *thread_state;
	InterruptData interrupt_data;
	// NULL-terminated doubly linked list of callable Python objects that we need to keep track of.
	// We need to store references to callables in a place where we can access them when running
	// Python's GC. Having them stored only in QuickJS' function opaques would create a dependency
	// cycle across Python and QuickJS that neither GC can notice.
	PythonCallableNode *python_callables;

	/* Setup Inspired by multidict for speedier lookups 
	and easier handling of variables to remain threadsafe */
	mod_state* state;
} PyJsContextObject;

// The data of the type _quickjs.Object.
typedef struct {
	PyObject_HEAD;
	PyJsContextObject *runtime_data;
	mod_state* state;
	JSValue object;
} PyJsObject;


// Converts the current Javascript exception to a Python exception via a C string.
static void quickjs_exception_to_python(JSContext *context);

// Converts a JSValue to a Python object.
//
// Takes ownership of the JSValue and will deallocate it (refcount reduced by 1).
static PyObject *quickjs_to_python(PyJsContextObject *runtime_data, JSValue value);

// Returns nonzero if we should stop due to a time limit.
static int js_interrupt_handler(JSRuntime *rt, void *opaque) {
	InterruptData *data = opaque;
	/* ever heard of a powerful tool called ternaries? - Vizonex */
	return (clock() - data->start >= data->limit) ? 1 : 0;
}

// Sets up a context and an InterruptData struct if the context has a time limit.
static void setup_time_limit(PyJsContextObject *runtime_data, InterruptData *interrupt_data) {
	if (runtime_data->has_time_limit) {
		JS_SetInterruptHandler(runtime_data->runtime, js_interrupt_handler, interrupt_data);
		interrupt_data->limit = runtime_data->time_limit;
		interrupt_data->start = clock();
	}
}

// Restores the context if the context has a time limit.
static void teardown_time_limit(PyJsContextObject *runtime_data) {
	if (runtime_data->has_time_limit) {
		JS_SetInterruptHandler(runtime_data->runtime, NULL, NULL);
	}
}

// This method is always called in a context before running JS code in QuickJS. It sets up time
// limites, releases the GIL etc.
static void prepare_call_js(PyJsContextObject *runtime_data) {
	// We release the GIL in order to speed things up for certain use cases.
	assert(!runtime_data->thread_state);
	runtime_data->thread_state = PyEval_SaveThread();
	JS_UpdateSt_cmoduleackTop(runtime_data->runtime);
	setup_time_limit(runtime_data, &runtime_data->interrupt_data);
}

// This method is called right after returning from running JS code. Aquires the GIL etc.
static void end_call_js(PyJsContextObject *runtime_data) {
	teardown_time_limit(runtime_data);
	assert(runtime_data->thread_state);
	PyEval_RestoreThread(runtime_data->thread_state);
	runtime_data->thread_state = NULL;
}

// Called when Python is called again from inside QuickJS.
static void prepare_call_python(PyJsContextObject *runtime_data) {
	assert(runtime_data->thread_state);
	PyEval_RestoreThread(runtime_data->thread_state);
	runtime_data->thread_state = NULL;
}

// Called when the operation started by prepare_call_python is done.
static void end_call_python(PyJsContextObject *runtime_data) {
	assert(!runtime_data->thread_state);
	runtime_data->thread_state = PyEval_SaveThread();
}

// GC traversal.
static int pyjsobject_tp_traverse(PyJsObject *self, visitproc visit, void *arg) {
	Py_VISIT(self->runtime_data);
	return 0;
}

// Creates an instance of the Object class.
static PyObject *pyjsobject_tp_new(PyTypeObject *type, PyObject *args, PyObject *kwds) {
	mod_state* state = get_mod_state_by_cls(type);
	PyJsObject *self = PyObject_GC_New(PyJsObject, type);
	if (self != NULL) {
		self->runtime_data = NULL;
		self->state = state;
	}
	return (PyObject *)self;
}

// Deallocates an instance of the Object class.
static void pyjsobject_tp_dealloc(PyJsObject *self) {
	if (self->runtime_data) {
		PyObject_GC_UnTrack(self);
		JS_FreeValue(self->runtime_data->context, self->object);
		// We incremented the refcount of the runtime data when we created this object, so we should
		// decrease it now so we don't leak memory.
		Py_CLEAR(self->runtime_data);
	}
	PyObject_GC_Del(self);
}

// _quickjs.Object.__call__
static PyObject *pyjsobject_tp_call(PyJsObject *self, PyObject *args, PyObject *kwds);

// _quickjs.Object.json
//
// Returns the JSON representation of the object as a Python string.
static PyObject *pyjsobject_json(PyJsObject *self) {
	JSContext *context = self->runtime_data->context;
	JSValue json_string = JS_JSONStringify(context, self->object, JS_UNDEFINED, JS_UNDEFINED);
	return quickjs_to_python(self->runtime_data, json_string);
}

// All methods of the _quickjs.Object class.
static PyMethodDef object_tp_methods[] = {
    {"json", (PyCFunction)pyjsobject_json, METH_NOARGS, "Converts to a JSON string."},
    {NULL} /* Sentinel */
};

// Define the quickjs.Object type.
static PyTypeObject PyJSObjectType = {
	PyVarObject_HEAD_INIT(NULL, 0)
	.tp_name = "_quickjs.Object",
    .tp_doc = "Quickjs object",
    .tp_basicsize = sizeof(PyJsObject),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    .tp_traverse = (traverseproc)pyjsobject_tp_traverse,
    .tp_new = pyjsobject_tp_new,
    .tp_dealloc = (destructor)pyjsobject_tp_dealloc,
    .tp_call = (ternaryfunc)pyjsobject_tp_call,
    .tp_methods = object_tp_methods
};

// Whether converting item to QuickJS would be possible.
static int python_to_quickjs_possible(PyJsContextObject *ctx, PyObject *item) {
	if (PyBool_Check(item)) {
		return 1;
	} else if (PyLong_Check(item)) {
		return 1;
	} else if (PyFloat_Check(item)) {
		return 1;
	} else if (item == Py_None) {
		return 1;
	} else if (PyUnicode_Check(item)) {
		return 1;
	} else if (Py_IS_TYPE(item, ctx->state->QuickJSObjectType) || PyObject_TypeCheck(item, ctx->state->QuickJSObjectType)) {
		PyJsObject *object = (PyJsObject *)item;
		if (object->runtime_data != ctx) {
			PyErr_Format(PyExc_ValueError, "Can not mix JS objects from different contexts.");
			return 0;
		}
		return 1;
	} else {
		PyErr_Format(PyExc_TypeError,
		             "Unsupported type when converting a Python object to quickjs: %s.",
		             Py_TYPE(item)->tp_name);
		return 0;
	}
}

// Converts item to QuickJS.
//
// If the Python object is not possible to convert to JS, undefined will be returned. This fallback
// will not be used if python_to_quickjs_possible returns 1.
static JSValueConst python_to_quickjs(PyJsContextObject *self, PyObject *item) {
	
	if (PyBool_Check(item)) {
		return JS_MKVAL(JS_TAG_BOOL, item == Py_True ? 1 : 0);
	} else if (PyLong_Check(item)) {
		int overflow;
		long value = PyLong_AsLongAndOverflow(item, &overflow);
		if (overflow) {
			PyObject *float_value = PyNumber_Float(item);
			double double_value = PyFloat_AsDouble(float_value);
			Py_DECREF(float_value);
			return JS_NewFloat64(self->context, double_value);
		} else {
			return JS_MKVAL(JS_TAG_INT, value);
		}
	} else if (PyFloat_Check(item)) {
		return JS_NewFloat64(self->context, PyFloat_AsDouble(item));
	} else if (item == Py_None) {
		return JS_NULL;
	} else if (PyUnicode_Check(item)) {
		return JS_NewString(self->context, PyUnicode_AsUTF8(item));
	} else if (PyObject_TypeCheck(item, self->state->QuickJSObjectType)) {
		return JS_DupValue(self->context, ((PyJsObject *)item)->object);
	} else {
		// Can not happen if python_to_quickjs_possible passes.
		return JS_UNDEFINED;
	}
}

// _quickjs.Object.__call__
/* TODO: Maybe try tp_vector_call? */
static PyObject *pyjsobject_tp_call(PyJsObject *self, PyObject *args, PyObject *kwds) {
	if (self->runtime_data == NULL) {
		// This object does not have a context and has not been created by this module.
		Py_RETURN_NONE;
	}

	// We first loop through all arguments and check that they are supported without doing anything.
	// This makes the cleanup code simpler for the case where we have to raise an error.
	const int nargs = PyTuple_Size(args);
	for (int i = 0; i < nargs; ++i) {
		PyObject *item = PyTuple_GetItem(args, i);
		if (!python_to_quickjs_possible(self->runtime_data, item)) {
			return NULL;
		}
	}

	// Now we know that all arguments are supported and we can convert them.
	JSValueConst *jsargs;
	if (nargs) {
		jsargs = js_malloc(self->runtime_data->context, nargs * sizeof(JSValueConst));
		if (jsargs == NULL) {
			quickjs_exception_to_python(self->runtime_data->context);
			return NULL;
		}
	}
	for (int i = 0; i < nargs; ++i) {
		PyObject *item = PyTuple_GetItem(args, i);
		jsargs[i] = python_to_quickjs(self->runtime_data, item);
	}

	prepare_call_js(self->runtime_data);
	JSValue value;
	value = JS_Call(self->runtime_data->context, self->object, JS_NULL, nargs, jsargs);
	for (int i = 0; i < nargs; ++i) {
		JS_FreeValue(self->runtime_data->context, jsargs[i]);
	}
	if (nargs) {
		js_free(self->runtime_data->context, jsargs);
	}
	end_call_js(self->runtime_data);
	return quickjs_to_python(self->runtime_data, value);
}

// Converts the current Javascript exception to a Python exception via a C string.
static void quickjs_exception_to_python(JSContext *context) {
	PyJsContextObject* self = (PyJsContextObject*)JS_GetContextOpaque(context);
	JSValue exception = JS_GetException(context);
	const char *cstring = JS_ToCString(context, exception);
	const char *stack_cstring = NULL;
	if (!JS_IsNull(exception) && !JS_IsUndefined(exception)) {
		JSValue stack = JS_GetPropertyStr(context, exception, "stack");
		if (!JS_IsException(stack)) {
			stack_cstring = JS_ToCString(context, stack);
			JS_FreeValue(context, stack);
		}
	}
	if (cstring != NULL) {
		const char *safe_stack_cstring = stack_cstring ? stack_cstring : "";
		if (strstr(cstring, "stack overflow") != NULL) {
			PyErr_Format(self->state->StackOverflowException, "%s\n%s", cstring, safe_stack_cstring);
		} else {
			PyErr_Format(self->state->JSException, "%s\n%s", cstring, safe_stack_cstring);
		}
	} else {
		// This has been observed to happen when different threads have used the same QuickJS
		// runtime, but not at the same time.
		// Could potentially be another problem though, since JS_ToCString may return NULL.
		PyErr_Format(self->state->JSException,
					 "(Failed obtaining QuickJS error string. Concurrency issue?)");
	}
	JS_FreeCString(context, cstring);
	JS_FreeCString(context, stack_cstring);
	JS_FreeValue(context, exception);
}

// Converts a JSValue to a Python object.
//
// Takes ownership of the JSValue and will deallocate it (refcount reduced by 1).
static PyObject *quickjs_to_python(PyJsContextObject *runtime_data, JSValue value) {
	JSContext *context = runtime_data->context;
	int tag = JS_VALUE_GET_TAG(value);
	// A return value of NULL means an exception.
	PyObject *return_value = NULL;

	if (tag == JS_TAG_INT) {
		return_value = Py_BuildValue("i", JS_VALUE_GET_INT(value));
	} else if (tag == JS_TAG_BIG_INT) {
		const char *cstring = JS_ToCString(context, value);
		return_value = PyLong_FromString(cstring, NULL, 10);
		JS_FreeCString(context, cstring);
	} else if (tag == JS_TAG_BOOL) {
		return_value = Py_BuildValue("O", JS_VALUE_GET_BOOL(value) ? Py_True : Py_False);
	} else if (tag == JS_TAG_NULL) {
		return_value = Py_None;
	} else if (tag == JS_TAG_UNDEFINED) {
		return_value = Py_None;
	} else if (tag == JS_TAG_EXCEPTION) {
		quickjs_exception_to_python(context);
	} else if (tag == JS_TAG_FLOAT64) {
		return_value = Py_BuildValue("d", JS_VALUE_GET_FLOAT64(value));
	} else if (tag == JS_TAG_STRING) {
		const char *cstring = JS_ToCString(context, value);
		return_value = Py_BuildValue("s", cstring);
		JS_FreeCString(context, cstring);
	} else if (tag == JS_TAG_OBJECT || tag == JS_TAG_MODULE || tag == JS_TAG_SYMBOL) {
		// This is a Javascript object or function. We wrap it in a _quickjs.Object.
		return_value = PyType_GenericAlloc(runtime_data->state->QuickJSObjectType, 0);
		if (return_value == NULL){
			/* error out after releasing object */
			goto finish;
		}
		PyJsObject *object = (PyJsObject *)return_value;
		// This is important. Otherwise, the context may be deallocated before the object, which
		// will result in a segfault with high probability.
		Py_INCREF(runtime_data);
		object->runtime_data = runtime_data;
		PyObject_GC_Track(object);
		object->object = JS_DupValue(context, value);
	} else {
		PyErr_Format(PyExc_TypeError, "Unknown quickjs tag: %d", tag);
	}

	finish:
		JS_FreeValue(context, value);
		if (return_value == Py_None) {
			// Can not simply return PyNone for refcounting reasons.
			Py_RETURN_NONE;
		}
		return return_value;
}

static PyObject *test_cmodule(PyObject *self, PyObject *args) {
	return Py_BuildValue("i", 42);
}


// GC traversal.
static int pyjscontext_tp_traverse(PyJsContextObject *self, visitproc visit, void *arg) {
	PythonCallableNode *node = self->python_callables;
	while (node) {
		Py_VISIT(node->obj);
		node = node->next;
	}
	return 0;
}

// GC clearing. Object does not have a clearing method, therefore dependency cycles
// between Context and Object will always be cleared starting here.
static int pyjscontext_tp_clear(PyJsContextObject *self) {
	PythonCallableNode *node = self->python_callables;
	while (node) {
		Py_CLEAR(node->obj);
		node = node->next;
	}
	return 0;
}


static void js_python_function_finalizer(JSRuntime *rt, JSValue val) {
	PyJsContextObject *runtime_data = JS_GetRuntimeOpaque(rt);
	PythonCallableNode *node = JS_GetOpaque(val, runtime_data->state->js_python_function_class_id);
	
	if (node) {
		// fail safe
		JS_SetOpaque(val, NULL);
		// NOTE: This may be called from e.g. pyjscontext_tp_dealloc, but also from
		// e.g. JS_Eval, so we need to ensure that we are in the correct state.
		// TODO: integrate better with (prepare|end)_call_(python|js).
		if (runtime_data->thread_state) {
			PyEval_RestoreThread(runtime_data->thread_state);
		}
		if (node->prev) {
			node->prev->next = node->next;
		} else {
			runtime_data->python_callables = node->next;
		}
		if (node->next) {
			node->next->prev = node->prev;
		}
		// May have just been cleared in pyjscontext_tp_clear.
		Py_XDECREF(node->obj);
		PyMem_Free(node);
		if (runtime_data->thread_state) {
			runtime_data->thread_state = PyEval_SaveThread();
		}
	};
}

static JSValue js_python_function_call(JSContext *ctx, JSValueConst func_obj,
                                       JSValueConst this_val, int argc, JSValueConst *argv,
                                       int flags) {
	PyJsContextObject *runtime_data = (PyJsContextObject *)JS_GetRuntimeOpaque(JS_GetRuntime(ctx));
	PythonCallableNode *node = JS_GetOpaque(func_obj, runtime_data->state->js_python_function_class_id);
	if (runtime_data->has_time_limit) {
		return JS_ThrowInternalError(ctx, "Can not call into Python with a time limit set.");
	}
	prepare_call_python(runtime_data);

	PyObject *args = PyTuple_New(argc);
	if (!args) {
		end_call_python(runtime_data);
		return JS_ThrowOutOfMemory(ctx);
	}
	int tuple_success = 1;
	for (int i = 0; i < argc; ++i) {
		PyObject *arg = quickjs_to_python(runtime_data, JS_DupValue(ctx, argv[i]));
		if (!arg) {
			tuple_success = 0;
			break;
		}
		PyTuple_SET_ITEM(args, i, arg);
	}
	if (!tuple_success) {
		Py_DECREF(args);
		end_call_python(runtime_data);
		return JS_ThrowInternalError(ctx, "Internal error: could not convert args.");
	}

	PyObject *result = PypyjsObject_tp_CallObject(node->obj, args);
	Py_DECREF(args);
	if (!result) {
		end_call_python(runtime_data);
		return JS_ThrowInternalError(ctx, "Python call failed.");
	}
	JSValue js_result = JS_NULL;
	if (python_to_quickjs_possible(runtime_data, result)) {
		js_result = python_to_quickjs(runtime_data, result);
	} else {
		PyErr_Clear();
		js_result = JS_ThrowInternalError(ctx, "Can not convert Python result to JS.");
	}
	Py_DECREF(result);

	end_call_python(runtime_data);
	return js_result;
}

/* TODO: Move this to the mod_state or allow discrete names for html5 and anti-bot detection */
static JSClassDef js_python_function_class = {
	"PythonFunction",
	.finalizer = js_python_function_finalizer,
	.call = js_python_function_call,
};

/* We should be utilizing Python Memory and not Outside memory, 
it helps the system breathe better */

/* Not in the previous rendition  */

static void *pyjs_def_calloc(void *opaque, size_t count, size_t size)
{
    return PyMem_RawCalloc(count, size);
}

static void *pyjs_def_malloc(void *opaque, size_t size)
{
    return PyMem_RawMalloc(size);
}

static void pyjs_def_free(void *opaque, void *ptr)
{
    PyMem_RawFree(ptr);
}

static void *pyjs_def_realloc(void *opaque, void *ptr, size_t size)
{
    return PyMem_RawRealloc(ptr, size);
}

static inline size_t pyjs__malloc_usable_size(const void *ptr)
{
#if defined(__APPLE__)
    return malloc_size(ptr);
#elif defined(_WIN32)
    return _msize((void *)ptr);
#elif defined(__linux__) || defined(__ANDROID__) || defined(__CYGWIN__) || defined(__FreeBSD__) || defined(__GLIBC__)
    return malloc_usable_size((void *)ptr);
#else
    return 0;
#endif
}


static const JSMallocFunctions pydef_malloc_funcs = {
    pyjs_def_calloc,
    pyjs_def_malloc,
    pyjs_def_free,
    pyjs_def_realloc,
    pyjs__malloc_usable_size
};


// Creates an instance of the _quickjs.Context class.
static PyObject *pyjscontext_tp_new(PyTypeObject *type, PyObject *args, PyObject *kwds) {
	mod_state* state = get_mod_state_by_cls(type);
	PyJsContextObject *self = PyObject_GC_New(PyJsContextObject, type);
	if (self != NULL) {
		// We never have different contexts for the same runtime. This way, different
		// _quickjs.Context can be used concurrently.

		/* NOTE: Vizonex Use Python's memory instead of 
		outside memory so that were properly sharing it 
		with python's GCs */
		self->state = state;
		self->runtime = JS_NewRuntime2(&pydef_malloc_funcs, NULL);
		self->context = JS_NewContext(self->runtime);
		/* so that we can grab the module state threadsafe from anything */
		JS_SetContextOpaque(self->context, self);

		JS_NewClass(self->runtime, self->state->js_python_function_class_id,
		            &js_python_function_class);
		JSValue global = JS_GetGlobalObject(self->context);
		JSValue fct_cls = JS_GetPropertyStr(self->context, global, "Function");
		JSValue fct_proto = JS_GetPropertyStr(self->context, fct_cls, "prototype");
		JS_FreeValue(self->context, fct_cls);
		JS_SetClassProto(self->context, self->state->js_python_function_class_id, fct_proto);
		JS_FreeValue(self->context, global);
		self->has_time_limit = 0;
		self->time_limit = 0;
		self->thread_state = NULL;
		self->python_callables = NULL;
		JS_SetRuntimeOpaque(self->runtime, self);
		PyObject_GC_Track(self);
	}
	return (PyObject *)self;
}

// Deallocates an instance of the _quickjs.Context class.
static void pyjscontext_tp_dealloc(PyJsContextObject *self) {
	JS_FreeContext(self->context);
	JS_FreeRuntime(self->runtime);
	PyObject_GC_UnTrack(self);
	PyObject_GC_Del(self);
}

// Evaluates a Python string as JS and returns the result as a Python object. Will return
// _quickjs.Object for complex types (other than e.g. str, int).
static PyObject *pyjscontext_eval_internal(PyJsContextObject *self, PyObject *args, int eval_type) {
	/* TODO: (Vizonex) support bytes-like objects? */
	const char *code;
	if (!PyArg_ParseTuple(args, "s", &code)) {
		return NULL;
	}
	prepare_call_js(self);
	JSValue value;
	value = JS_Eval(self->context, code, strlen(code), "<input>", eval_type);
	end_call_js(self);
	return quickjs_to_python(self, value);
}

// _quickjs.Context.eval
//
// Evaluates a Python string as JS and returns the result as a Python object. Will return
// _quickjs.Object for complex types (other than e.g. str, int).
static PyObject *pyjscontext_eval(PyJsContextObject *self, PyObject *args) {
	return pyjscontext_eval_internal(self, args, JS_EVAL_TYPE_GLOBAL);
}

// _quickjs.Context.module
//
// Evaluates a Python string as JS module. Otherwise identical to eval.
static PyObject *pyjscontext_module(PyJsContextObject *self, PyObject *args) {
	return pyjscontext_eval_internal(self, args, JS_EVAL_TYPE_MODULE);
}

// _quickjs.Context.execute_pending_job
//
// If there are pending jobs, executes one and returns True. Else returns False.
static PyObject *pyjscontext_execute_pending_job(PyJsContextObject *self) {
	prepare_call_js(self);
	JSContext *ctx;
	int ret = JS_ExecutePendingJob(self->runtime, &ctx);
	end_call_js(self);
	if (ret > 0) {
		Py_RETURN_TRUE;
	} else if (ret == 0) {
		Py_RETURN_FALSE;
	} else {
		quickjs_exception_to_python(ctx);
		return NULL;
	}
}

// _quickjs.Context.parse_json
//
// Evaluates a Python string as JSON and returns the result as a Python object. Will
// return _quickjs.Object for complex types (other than e.g. str, int).
static PyObject *pyjscontext_parse_json(PyJsContextObject *self, PyObject *args) {
	const char *data;
	if (!PyArg_ParseTuple(args, "s", &data)) {
		return NULL;
	}
	JSValue value;
	Py_BEGIN_ALLOW_THREADS;
	value = JS_ParseJSON(self->context, data, strlen(data), "pyjscontext_parse_json.json");
	Py_END_ALLOW_THREADS;
	return quickjs_to_python(self, value);
}

// _quickjs.Context.get
//
// Retrieves a global variable from the JS context.
static PyObject *pyjscontext_get(PyJsContextObject *self, PyObject *args) {
	const char *name;
	if (!PyArg_ParseTuple(args, "s", &name)) {
		return NULL;
	}
	JSValue global = JS_GetGlobalObject(self->context);
	JSValue value = JS_GetPropertyStr(self->context, global, name);
	JS_FreeValue(self->context, global);
	return quickjs_to_python(self, value);
}

// _quickjs.Context.set
//
// Sets a global variable to the JS context.
static PyObject *pyjscontext_set(PyJsContextObject *self, PyObject *args) {
	const char *name;
	PyObject *item;
	if (!PyArg_ParseTuple(args, "sO", &name, &item)) {
		return NULL;
	}
	JSValue global = JS_GetGlobalObject(self->context);
	int ret = 0;
	if (python_to_quickjs_possible(self, item)) {
		ret = JS_SetPropertyStr(self->context, global, name, python_to_quickjs(self, item));
		if (ret != 1) {
			PyErr_SetString(PyExc_TypeError, "Failed setting the variable.");
		}
	}
	JS_FreeValue(self->context, global);
	if (ret == 1) {
		Py_RETURN_NONE;
	} else {
		return NULL;
	}
}

// _quickjs.Context.set_memory_limit
//
// Sets the memory limit of the context.
static PyObject *pyjscontext_set_memory_limit(PyJsContextObject *self, PyObject *args) {
	Py_ssize_t limit;
	if (!PyArg_ParseTuple(args, "n", &limit)) {
		return NULL;
	}
	JS_SetMemoryLimit(self->runtime, limit);
	Py_RETURN_NONE;
}

// _quickjs.Context.set_time_limit
//
// Sets the CPU time limit of the context. This will be used in an interrupt handler.
static PyObject *pyjscontext_set_time_limit(PyJsContextObject *self, PyObject *arg) {
	if (!PyFloat_Check(arg)){
		PyErr_SetString(PyExc_TypeError, "excepted a float got %s", Py_TYPE(arg)->tp_name);
		return NULL;
	}
	double limit = PyFloat_AS_DOUBLE(arg);
	
	if (limit < 0) {
		self->has_time_limit = 0;
	} else {
		self->has_time_limit = 1;
		self->time_limit = (clock_t)(limit * CLOCKS_PER_SEC);
	}
	Py_RETURN_NONE;
}

// _quickjs.Context.set_max_stack_size
//
// Sets the max stack size in bytes.

static PyObject *pyjscontext_set_max_stack_size(PyJsContextObject *self, PyObject *arg) {
	if (!PyLong_Check(arg)){
		PyErr_SetString(PyExc_TypeError, "excepted an integer got %s", Py_TYPE(arg)->tp_name);
		return NULL;
	}
	Py_ssize_t limit = PyLong_AsSsize_t(arg);
	JS_SetMaxStackSize(self->runtime, limit);
	Py_RETURN_NONE;
}

// _quickjs.Context.memory
//
// Sets the CPU time limit of the context. This will be used in an interrupt handler.
static PyObject *pyjscontext_memory(PyJsContextObject *self) {
	PyObject *dict = PyDict_New();
	if (dict == NULL) {
		return NULL;
	}
	JSMemoryUsage usage;
	JS_ComputeMemoryUsage(self->runtime, &usage);
#define MEM_USAGE_ADD_TO_DICT(key)                          \
	{                                                       \
		PyObject *value = PyLong_FromLongLong(usage.key);   \
		if (PyDict_SetItemString(dict, #key, value) != 0) { \
			return NULL;                                    \
		}                                                   \
		Py_DECREF(value);                                   \
	}
	MEM_USAGE_ADD_TO_DICT(malloc_size);
	MEM_USAGE_ADD_TO_DICT(malloc_limit);
	MEM_USAGE_ADD_TO_DICT(memory_used_size);
	MEM_USAGE_ADD_TO_DICT(malloc_count);
	MEM_USAGE_ADD_TO_DICT(memory_used_count);
	MEM_USAGE_ADD_TO_DICT(atom_count);
	MEM_USAGE_ADD_TO_DICT(atom_size);
	MEM_USAGE_ADD_TO_DICT(str_count);
	MEM_USAGE_ADD_TO_DICT(str_size);
	MEM_USAGE_ADD_TO_DICT(obj_count);
	MEM_USAGE_ADD_TO_DICT(obj_size);
	MEM_USAGE_ADD_TO_DICT(prop_count);
	MEM_USAGE_ADD_TO_DICT(prop_size);
	MEM_USAGE_ADD_TO_DICT(shape_count);
	MEM_USAGE_ADD_TO_DICT(shape_size);
	MEM_USAGE_ADD_TO_DICT(js_func_count);
	MEM_USAGE_ADD_TO_DICT(js_func_size);
	MEM_USAGE_ADD_TO_DICT(js_func_code_size);
	MEM_USAGE_ADD_TO_DICT(js_func_pc2line_count);
	MEM_USAGE_ADD_TO_DICT(js_func_pc2line_size);
	MEM_USAGE_ADD_TO_DICT(c_func_count);
	MEM_USAGE_ADD_TO_DICT(array_count);
	MEM_USAGE_ADD_TO_DICT(fast_array_count);
	MEM_USAGE_ADD_TO_DICT(fast_array_elements);
	MEM_USAGE_ADD_TO_DICT(binary_object_count);
	MEM_USAGE_ADD_TO_DICT(binary_object_size);
	return dict;
}

// _quickjs.Context.gc
//
// Runs garbage collection.
static PyObject *pyjscontext_gc(PyJsContextObject *self) {
	JS_RunGC(self->runtime);
	Py_RETURN_NONE;
}

/* TODO: Fast-call this function */
static PyObject *pyjscontext_add_callable(PyJsContextObject *self, PyObject *args) {
	const char *name;
	PyObject *callable;
	if (!PyArg_ParseTuple(args, "sO", &name, &callable)) {
		return NULL;
	}
	if (!PyCallable_Check(callable)) {
		PyErr_SetString(PyExc_TypeError, "Argument must be callable.");
		return NULL;
	}

	JSValue function = JS_NewObjectClass(self->context, self->state->js_python_function_class_id);
	if (JS_IsException(function)) {
		quickjs_exception_to_python(self->context);
		return NULL;
	}
	// TODO: Should we allow setting the .length of the function to something other than 0?
	JS_DefinePropertyValueStr(self->context, function, "name", JS_NewString(self->context, name), JS_PROP_CONFIGURABLE);
	PythonCallableNode *node = PyMem_Malloc(sizeof(PythonCallableNode));
	if (!node) {
		JS_FreeValue(self->context, function);
		return NULL;
	}
	Py_INCREF(callable);
	node->obj = callable;
	node->prev = NULL;
	node->next = self->python_callables;
	if (self->python_callables) {
		self->python_callables->prev = node;
	}
	self->python_callables = node;
	JS_SetOpaque(function, node);

	JSValue global = JS_GetGlobalObject(self->context);
	if (JS_IsException(global)) {
		JS_FreeValue(self->context, function);
		quickjs_exception_to_python(self->context);
		return NULL;
	}
	// If this fails we don't notify the caller of this function.
	int ret = JS_SetPropertyStr(self->context, global, name, function);
	JS_FreeValue(self->context, global);
	if (ret != 1) {
		PyErr_SetString(PyExc_TypeError, "Failed adding the callable.");
		return NULL;
	} else {
		Py_RETURN_NONE;
	}
}


// _quickjs.Context.globalThis
//
// Global object of the JS context.
static PyObject *pyjscontext_global_this(PyJsContextObject *self, void *closure) {
	return quickjs_to_python(self, JS_GetGlobalObject(self->context));
}


// All methods of the _quickjs.Context class.
static PyMethodDef pyjscontext_methods[] = {
    {"eval", (PyCFunction)pyjscontext_eval, METH_VARARGS, "Evaluates a Javascript string."},
    {"module",
     (PyCFunction)pyjscontext_module,
     METH_VARARGS,
     "Evaluates a Javascript string as a module."},
    {"execute_pending_job", (PyCFunction)pyjscontext_execute_pending_job, METH_NOARGS, "Executes a pending job."},
    {"parse_json", (PyCFunction)pyjscontext_parse_json, METH_VARARGS, "Parses a JSON string."},
    {"get", (PyCFunction)pyjscontext_get, METH_VARARGS, "Gets a Javascript global variable."},
    {"set", (PyCFunction)pyjscontext_set, METH_VARARGS, "Sets a Javascript global variable."},
    {"set_memory_limit",
     (PyCFunction)pyjscontext_set_memory_limit,
     METH_VARARGS,
     "Sets the memory limit in bytes."},
    {"set_time_limit",
     (PyCFunction)pyjscontext_set_time_limit,
     METH_O,
     "Sets the CPU time limit in seconds (C function clock() is used)."},
    {"set_max_stack_size",
     (PyCFunction)pyjscontext_set_max_stack_size,
     METH_O,
     "Sets the maximum stack size in bytes. Default is 256kB."},
    {"memory", (PyCFunction)pyjscontext_memory, METH_NOARGS, "Returns the memory usage as a dict."},
    {"gc", (PyCFunction)pyjscontext_gc, METH_NOARGS, "Runs garbage collection."},
    {"add_callable", (PyCFunction)pyjscontext_add_callable, METH_VARARGS, "Wraps a Python callable."},
    {NULL} /* Sentinel */
};

// All getsetters (properties) of the _quickjs.Context class.
static PyGetSetDef pyjscontext_getsetters[] = {
    {"globalThis", (getter)pyjscontext_global_this, NULL, "Global object of the context.", NULL},
    {NULL} /* Sentinel */
};

// Define the _quickjs.Context type.
static PyTypeObject PyJsContextObjectType = {
		PyVarObject_HEAD_INIT(NULL, 0)
		/* TODO: rename to something like quickjs2 or quickjs3 ... */
		.tp_name = "_quickjs.Context",
        .tp_doc = "Quickjs context",
        .tp_basicsize = sizeof(PyJsContextObject),
        .tp_itemsize = 0,
        .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
        .tp_traverse = (traverseproc)pyjscontext_tp_traverse,
        .tp_clear = (inquiry)pyjscontext_tp_clear,
        .tp_new = pyjscontext_tp_new,
        .tp_dealloc = (destructor)pyjscontext_tp_dealloc,
        .tp_methods = pyjscontext_methods,
        .tp_getset = pyjscontext_getsetters
};

// All global methods in _quickjs.
static PyMethodDef myextension_methods[] = {
	{"_test_cmodule", (PyCFunction)test_cmodule, METH_NOARGS, NULL},
    {NULL, NULL}
};


/* ========== MODULE ======== */

static int 
module_exec(PyObject* mod){
	mod_state* state = get_mod_state(mod);

	state->QuickJSContextType = &PyJsContextObjectType;
	state->QuickJSObjectType = &PyJSObjectType;
	if (PyModule_AddType(mod, state->QuickJSContextType) < 0){
		return -1;
	}
	if (PyModule_AddType(mod, state->QuickJSObjectType) < 0) {
		return -1;	
	}
	state->JSException = PyErr_NewExceptionWithDoc("JSException", "an exception that has been thrown by quickjs", NULL, NULL);
	if (state->JSException == NULL){
		return -1;
	}
	state->StackOverflowException = PyErr_NewExceptionWithDoc("StackOverflow", "an excpetion thrown when quickjs overflows", NULL, NULL);
	if (state->StackOverflowException == NULL){
		return -1;
	}
	// remain at zero until found...
	state->js_python_function_class_id = 0;
	return 0;
}

/* Give the module some breathing room */

static int
module_traverse(PyObject *mod, visitproc visit, void *arg)
{
    mod_state *state = get_mod_state(mod);
	Py_VISIT(state->QuickJSContextType);
	Py_VISIT(state->QuickJSObjectType);
	return 0;
}

static int
module_clear(PyObject *mod)
{
    mod_state *state = get_mod_state(mod);
	Py_CLEAR(state->QuickJSContextType);
	Py_CLEAR(state->QuickJSObjectType);
	return 0;
}

static void
module_free(void *mod)
{
    (void)module_clear((PyObject *)mod);
}


static struct PyModuleDef_Slot module_slots[] = {
    {Py_mod_exec, module_exec},
#if PY_VERSION_HEX >= 0x030c00f0
    {Py_mod_multiple_interpreters, Py_MOD_PER_INTERPRETER_GIL_SUPPORTED},
#endif
#if PY_VERSION_HEX >= 0x030d00f0
    {Py_mod_gil, Py_MOD_GIL_NOT_USED},
#endif
    {0, NULL},
};

// Define the _quickjs module.
static struct PyModuleDef quickjs_module = {
	.m_base = PyModuleDef_HEAD_INIT,
	.m_name = "_quickjs",
    .m_size = sizeof(mod_state),
    .m_methods = myextension_methods,
    .m_slots = module_slots,
    .m_traverse = module_traverse,
    .m_clear = module_clear,
    .m_free = (freefunc)module_free,
};

PyMODINIT_FUNC PyInit__quickjs(void) {
	return  PyModuleDef_Init(&quickjs_module);
}