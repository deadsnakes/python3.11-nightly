
#include "Python.h"
#include "frameobject.h"
#include "pycore_frame.h"
#include "pycore_object.h"        // _PyObject_GC_UNTRACK()

int
_PyFrame_Traverse(InterpreterFrame *frame, visitproc visit, void *arg)
{
    Py_VISIT(frame->frame_obj);
    Py_VISIT(frame->f_globals);
    Py_VISIT(frame->f_builtins);
    Py_VISIT(frame->f_locals);
    Py_VISIT(frame->f_code);
   /* locals */
    PyObject **locals = _PyFrame_GetLocalsArray(frame);
    int i = 0;
    /* locals and stack */
    for (; i <frame->stacktop; i++) {
        Py_VISIT(locals[i]);
    }
    return 0;
}

PyFrameObject *
_PyFrame_MakeAndSetFrameObject(InterpreterFrame *frame)
{
    assert(frame->frame_obj == NULL);
    PyObject *error_type, *error_value, *error_traceback;
    PyErr_Fetch(&error_type, &error_value, &error_traceback);
    PyFrameObject *f = _PyFrame_New_NoTrack(frame, 0);
    if (f == NULL) {
        Py_XDECREF(error_type);
        Py_XDECREF(error_value);
        Py_XDECREF(error_traceback);
    }
    else {
        PyErr_Restore(error_type, error_value, error_traceback);
    }
    frame->frame_obj = f;
    return f;
}


static InterpreterFrame *
copy_frame_to_heap(InterpreterFrame *frame)
{
    assert(frame->stacktop >= frame->f_code->co_nlocalsplus);
    Py_ssize_t size = ((char*)&frame->localsplus[frame->stacktop]) - (char *)frame;
    InterpreterFrame *copy = PyMem_Malloc(size);
    if (copy == NULL) {
        PyErr_NoMemory();
        return NULL;
    }
    memcpy(copy, frame, size);
    return copy;
}

static inline void
clear_specials(InterpreterFrame *frame)
{
    frame->generator = NULL;
    Py_XDECREF(frame->frame_obj);
    Py_XDECREF(frame->f_locals);
    Py_DECREF(frame->f_globals);
    Py_DECREF(frame->f_builtins);
    Py_DECREF(frame->f_code);
}

static void
take_ownership(PyFrameObject *f, InterpreterFrame *frame)
{
    assert(f->f_own_locals_memory == 0);
    assert(frame->frame_obj == NULL);

    f->f_own_locals_memory = 1;
    f->f_frame = frame;
    assert(f->f_back == NULL);
    if (frame->previous != NULL) {
        /* Link PyFrameObjects.f_back and remove link through InterpreterFrame.previous */
        PyFrameObject *back = _PyFrame_GetFrameObject(frame->previous);
        if (back == NULL) {
            /* Memory error here. */
            assert(PyErr_ExceptionMatches(PyExc_MemoryError));
            /* Nothing we can do about it */
            PyErr_Clear();
            _PyErr_WriteUnraisableMsg("Out of memory lazily allocating frame->f_back", NULL);
        }
        else {
            f->f_back = (PyFrameObject *)Py_NewRef(back);
        }
        frame->previous = NULL;
    }
    if (!_PyObject_GC_IS_TRACKED((PyObject *)f)) {
        _PyObject_GC_TRACK((PyObject *)f);
    }
}

int
_PyFrame_Clear(InterpreterFrame * frame, int take)
{
    if (frame->frame_obj) {
        PyFrameObject *f = frame->frame_obj;
        frame->frame_obj = NULL;
        if (Py_REFCNT(f) > 1) {
            if (!take) {
                frame = copy_frame_to_heap(frame);
                if (frame == NULL) {
                    return -1;
                }
            }
            take_ownership(f, frame);
            Py_DECREF(f);
            return 0;
        }
        Py_DECREF(f);
    }
    assert(_PyFrame_GetStackPointer(frame) >= _PyFrame_Stackbase(frame));
    for (int i = 0; i < frame->stacktop; i++) {
        Py_XDECREF(frame->localsplus[i]);
    }
    clear_specials(frame);
    if (take) {
        PyMem_Free(frame);
    }
    return 0;
}
