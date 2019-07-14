#include <Python.h>

#include "third-party/quickjs.h"

typedef struct {
	PyObject_HEAD JSRuntime *runtime;
	JSContext *context;
} ContextData;

typedef struct {
	PyObject_HEAD;
	ContextData *context;
	JSValue object;
} ObjectData;

static PyObject *JSException = NULL;
static PyObject *quickjs_to_python(ContextData *context_obj, JSValue value);

//
// Object type
//
static PyObject *object_new(PyTypeObject *type, PyObject *args, PyObject *kwds) {
	ObjectData *self;
	self = (ObjectData *)type->tp_alloc(type, 0);
	if (self != NULL) {
		self->context = NULL;
	}
	return (PyObject *)self;
}

static void object_dealloc(ObjectData *self) {
	if (self->context) {
		JS_FreeValue(self->context->context, self->object);
		Py_DECREF(self->context);
	}
	Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyObject *object_call(ObjectData *self, PyObject *args, PyObject *kwds);

static PyObject *object_json(ObjectData *self) {
	JSContext *context = self->context->context;
	JSValue global = JS_GetGlobalObject(context);
	JSValue JSON = JS_GetPropertyStr(context, global, "JSON");
	JSValue stringify = JS_GetPropertyStr(context, JSON, "stringify");
	JSValueConst args[1] = {self->object};
	JSValue json_string = JS_Call(context, stringify, JSON, 1, args);
	JS_FreeValue(context, global);
	JS_FreeValue(context, JSON);
	JS_FreeValue(context, stringify);
	return quickjs_to_python(self->context, json_string);
}

static PyMethodDef object_methods[] = {
    {"json", (PyCFunction)object_json, METH_NOARGS, "Converts to a JSON string."},
    {NULL} /* Sentinel */
};

static PyTypeObject Object = {PyVarObject_HEAD_INIT(NULL, 0).tp_name = "_quickjs.Object",
                              .tp_doc = "Quickjs object",
                              .tp_basicsize = sizeof(ObjectData),
                              .tp_itemsize = 0,
                              .tp_flags = Py_TPFLAGS_DEFAULT,
                              .tp_new = object_new,
                              .tp_dealloc = (destructor)object_dealloc,
                              .tp_call = object_call,
                              .tp_methods = object_methods};

static PyObject *object_call(ObjectData *self, PyObject *args, PyObject *kwds) {
	if (self->context == NULL) {
		Py_RETURN_NONE;
	}
	const int nargs = PyTuple_Size(args);
	for (int i = 0; i < nargs; ++i) {
		PyObject *item = PyTuple_GetItem(args, i);
		if (PyLong_Check(item)) {
		} else if (PyUnicode_Check(item)) {
		} else if (PyObject_IsInstance(item, (PyObject *)&Object)) {
		} else {
			PyErr_Format(PyExc_ValueError, "Unsupported type when calling quickjs object");
			return NULL;
		}
	}
	JSValueConst *jsargs = malloc(nargs * sizeof(JSValueConst));
	for (int i = 0; i < nargs; ++i) {
		PyObject *item = PyTuple_GetItem(args, i);
		if (PyLong_Check(item)) {
			jsargs[i] = JS_MKVAL(JS_TAG_INT, PyLong_AsLong(item));
		} else if (PyUnicode_Check(item)) {
			jsargs[i] = JS_NewString(self->context->context, PyUnicode_AsUTF8(item));
		} else if (PyObject_IsInstance(item, (PyObject *)&Object)) {
			jsargs[i] = JS_DupValue(self->context->context, ((ObjectData *)item)->object);
		}
	}
	JSValue value = JS_Call(self->context->context, self->object, JS_NULL, nargs, jsargs);
	for (int i = 0; i < nargs; ++i) {
		JS_FreeValue(self->context->context, jsargs[i]);
	}
	free(jsargs);
	return quickjs_to_python(self->context, value);
}

static PyObject *quickjs_to_python(ContextData *context_obj, JSValue value) {
	JSContext *context = context_obj->context;
	int tag = JS_VALUE_GET_TAG(value);
	PyObject *return_value = NULL;

	if (tag == JS_TAG_INT) {
		return_value = Py_BuildValue("i", JS_VALUE_GET_INT(value));
	} else if (tag == JS_TAG_BOOL) {
		return_value = Py_BuildValue("O", JS_VALUE_GET_BOOL(value) ? Py_True : Py_False);
	} else if (tag == JS_TAG_NULL) {
		return_value = Py_None;
	} else if (tag == JS_TAG_UNDEFINED) {
		return_value = Py_None;
	} else if (tag == JS_TAG_EXCEPTION) {
		JSValue exception = JS_GetException(context);
		JSValue error_string = JS_ToString(context, exception);
		const char *cstring = JS_ToCString(context, error_string);
		PyErr_Format(JSException, "%s", cstring);
		JS_FreeCString(context, cstring);
		JS_FreeValue(context, error_string);
		JS_FreeValue(context, exception);
	} else if (tag == JS_TAG_FLOAT64) {
		return_value = Py_BuildValue("d", JS_VALUE_GET_FLOAT64(value));
	} else if (tag == JS_TAG_STRING) {
		const char *cstring = JS_ToCString(context, value);
		return_value = Py_BuildValue("s", cstring);
		JS_FreeCString(context, cstring);
	} else if (tag == JS_TAG_OBJECT) {
		return_value = PyObject_CallObject((PyObject *)&Object, NULL);
		ObjectData *object = (ObjectData *)return_value;
		Py_INCREF(context_obj);
		object->context = context_obj;
		object->object = JS_DupValue(context, value);
	} else {
		PyErr_Format(PyExc_ValueError, "Unknown quickjs tag: %d", tag);
	}

	JS_FreeValue(context, value);
	if (return_value == Py_None) {
		Py_RETURN_NONE;
	}
	return return_value;
}

static PyObject *test(PyObject *self, PyObject *args) {
	return Py_BuildValue("i", 42);
}

struct module_state {};

//
// Context type
//
static PyObject *context_new(PyTypeObject *type, PyObject *args, PyObject *kwds) {
	ContextData *self;
	self = (ContextData *)type->tp_alloc(type, 0);
	if (self != NULL) {
		self->runtime = JS_NewRuntime();
		self->context = JS_NewContext(self->runtime);
	}
	return (PyObject *)self;
}

static void context_dealloc(ContextData *self) {
	JS_FreeContext(self->context);
	JS_FreeRuntime(self->runtime);
	Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyObject *context_eval(ContextData *self, PyObject *args) {
	const char *code;
	if (!PyArg_ParseTuple(args, "s", &code)) {
		return NULL;
	}
	JSValue value = JS_Eval(self->context, code, strlen(code), "<input>", JS_EVAL_TYPE_GLOBAL);
	return quickjs_to_python(self, value);
}

static PyObject *context_get(ContextData *self, PyObject *args) {
	const char *name;
	if (!PyArg_ParseTuple(args, "s", &name)) {
		return NULL;
	}
	JSValue global = JS_GetGlobalObject(self->context);
	JSValue value = JS_GetPropertyStr(self->context, global, name);
	JS_FreeValue(self->context, global);
	return quickjs_to_python(self, value);
}

static PyMethodDef context_methods[] = {
    {"eval", (PyCFunction)context_eval, METH_VARARGS, "Evaluates a Javascript string."},
    {"get", (PyCFunction)context_get, METH_VARARGS, "Gets a Javascript global variable."},
    {NULL} /* Sentinel */
};

static PyTypeObject Context = {PyVarObject_HEAD_INIT(NULL, 0).tp_name = "_quickjs.Context",
                               .tp_doc = "Quickjs context",
                               .tp_basicsize = sizeof(ContextData),
                               .tp_itemsize = 0,
                               .tp_flags = Py_TPFLAGS_DEFAULT,
                               .tp_new = context_new,
                               .tp_dealloc = (destructor)context_dealloc,
                               .tp_methods = context_methods};

static PyMethodDef myextension_methods[] = {{"test", (PyCFunction)test, METH_NOARGS, NULL},
                                            {NULL, NULL}};

static struct PyModuleDef moduledef = {PyModuleDef_HEAD_INIT,
                                       "quickjs",
                                       NULL,
                                       sizeof(struct module_state),
                                       myextension_methods,
                                       NULL,
                                       NULL,
                                       NULL,
                                       NULL};

PyMODINIT_FUNC PyInit__quickjs(void) {
	if (PyType_Ready(&Context) < 0) {
		return NULL;
	}
	if (PyType_Ready(&Object) < 0) {
		return NULL;
	}

	PyObject *module = PyModule_Create(&moduledef);
	if (module == NULL) {
		return NULL;
	}

	JSException = PyErr_NewException("_quickjs.JSException", NULL, NULL);
	if (JSException == NULL) {
		return NULL;
	}

	Py_INCREF(&Context);
	PyModule_AddObject(module, "Context", (PyObject *)&Context);
	Py_INCREF(&Object);
	PyModule_AddObject(module, "Object", (PyObject *)&Object);
	PyModule_AddObject(module, "JSException", JSException);
	return module;
}
