#include <Python.h>

static PyObject *test(PyObject *self, PyObject *args) {
	return Py_BuildValue("i", 42);
}

struct module_state {};

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
	return PyModule_Create(&moduledef);
}
