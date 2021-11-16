/* connection.c - the connection type
 *
 * Copyright (C) 2004-2010 Gerhard Häring <gh@ghaering.de>
 *
 * This file is part of pysqlite.
 *
 * This software is provided 'as-is', without any express or implied
 * warranty.  In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 */

#include "module.h"
#include "structmember.h"         // PyMemberDef
#include "connection.h"
#include "statement.h"
#include "cursor.h"
#include "prepare_protocol.h"
#include "util.h"

#if SQLITE_VERSION_NUMBER >= 3014000
#define HAVE_TRACE_V2
#endif

static int
clinic_fsconverter(PyObject *pathlike, const char **result)
{
    PyObject *bytes = NULL;
    Py_ssize_t len;
    char *str;

    if (!PyUnicode_FSConverter(pathlike, &bytes)) {
        goto error;
    }
    if (PyBytes_AsStringAndSize(bytes, &str, &len) < 0) {
        goto error;
    }
    if ((*result = (const char *)PyMem_Malloc(len+1)) == NULL) {
        goto error;
    }

    memcpy((void *)(*result), str, len+1);
    Py_DECREF(bytes);
    return 1;

error:
    Py_XDECREF(bytes);
    return 0;
}

#define clinic_state() (pysqlite_get_state_by_type(Py_TYPE(self)))
#include "clinic/connection.c.h"
#undef clinic_state

/*[clinic input]
module _sqlite3
class _sqlite3.Connection "pysqlite_Connection *" "clinic_state()->ConnectionType"
[clinic start generated code]*/
/*[clinic end generated code: output=da39a3ee5e6b4b0d input=67369db2faf80891]*/

_Py_IDENTIFIER(cursor);

static const char * const begin_statements[] = {
    "BEGIN ",
    "BEGIN DEFERRED",
    "BEGIN IMMEDIATE",
    "BEGIN EXCLUSIVE",
    NULL
};

static void _pysqlite_drop_unused_cursor_references(pysqlite_Connection* self);
static void free_callback_context(callback_context *ctx);
static void set_callback_context(callback_context **ctx_pp,
                                 callback_context *ctx);

static PyObject *
new_statement_cache(pysqlite_Connection *self, int maxsize)
{
    PyObject *args[] = { NULL, PyLong_FromLong(maxsize), };
    if (args[1] == NULL) {
        return NULL;
    }
    PyObject *lru_cache = self->state->lru_cache;
    size_t nargsf = 1 | PY_VECTORCALL_ARGUMENTS_OFFSET;
    PyObject *inner = PyObject_Vectorcall(lru_cache, args + 1, nargsf, NULL);
    Py_DECREF(args[1]);
    if (inner == NULL) {
        return NULL;
    }

    args[1] = (PyObject *)self;  // Borrowed ref.
    nargsf = 1 | PY_VECTORCALL_ARGUMENTS_OFFSET;
    PyObject *res = PyObject_Vectorcall(inner, args + 1, nargsf, NULL);
    Py_DECREF(inner);
    return res;
}

static inline const char *
begin_stmt_to_isolation_level(const char *begin_stmt)
{
    assert(begin_stmt != NULL);

    // All begin statements start with "BEGIN "; add strlen("BEGIN ") to get
    // the isolation level.
    return begin_stmt + 6;
}

static const char *
get_begin_statement(const char *level)
{
    assert(level != NULL);
    for (int i = 0; begin_statements[i] != NULL; i++) {
        const char *stmt = begin_statements[i];
        const char *candidate = begin_stmt_to_isolation_level(stmt);
        if (sqlite3_stricmp(level, candidate) == 0) {
            return begin_statements[i];
        }
    }
    PyErr_SetString(PyExc_ValueError,
                    "isolation_level string must be '', 'DEFERRED', "
                    "'IMMEDIATE', or 'EXCLUSIVE'");
    return NULL;
}

/*[python input]
class FSConverter_converter(CConverter):
    type = "const char *"
    converter = "clinic_fsconverter"
    def converter_init(self):
        self.c_default = "NULL"
    def cleanup(self):
        return f"PyMem_Free((void *){self.name});\n"
[python start generated code]*/
/*[python end generated code: output=da39a3ee5e6b4b0d input=7b3be538bc4058c0]*/

/*[clinic input]
_sqlite3.Connection.__init__ as pysqlite_connection_init

    database: FSConverter
    timeout: double = 5.0
    detect_types: int = 0
    isolation_level: str(accept={str, NoneType}) = ""
    check_same_thread: bool(accept={int}) = True
    factory: object(c_default='(PyObject*)clinic_state()->ConnectionType') = ConnectionType
    cached_statements: int = 128
    uri: bool = False
[clinic start generated code]*/

static int
pysqlite_connection_init_impl(pysqlite_Connection *self,
                              const char *database, double timeout,
                              int detect_types, const char *isolation_level,
                              int check_same_thread, PyObject *factory,
                              int cached_statements, int uri)
/*[clinic end generated code: output=d8c37afc46d318b0 input=adfb29ac461f9e61]*/
{
    int rc;

    if (PySys_Audit("sqlite3.connect", "s", database) < 0) {
        return -1;
    }

    pysqlite_state *state = pysqlite_get_state_by_type(Py_TYPE(self));
    self->state = state;

    Py_CLEAR(self->statement_cache);
    Py_CLEAR(self->cursors);

    Py_INCREF(Py_None);
    Py_XSETREF(self->row_factory, Py_None);

    Py_INCREF(&PyUnicode_Type);
    Py_XSETREF(self->text_factory, (PyObject*)&PyUnicode_Type);

    Py_BEGIN_ALLOW_THREADS
    rc = sqlite3_open_v2(database, &self->db,
                         SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                         (uri ? SQLITE_OPEN_URI : 0), NULL);
    Py_END_ALLOW_THREADS

    if (self->db == NULL && rc == SQLITE_NOMEM) {
        PyErr_NoMemory();
        return -1;
    }
    if (rc != SQLITE_OK) {
        _pysqlite_seterror(state, self->db);
        return -1;
    }

    if (isolation_level) {
        const char *stmt = get_begin_statement(isolation_level);
        if (stmt == NULL) {
            return -1;
        }
        self->begin_statement = stmt;
    }
    else {
        self->begin_statement = NULL;
    }

    self->statement_cache = new_statement_cache(self, cached_statements);
    if (self->statement_cache == NULL) {
        return -1;
    }
    if (PyErr_Occurred()) {
        return -1;
    }

    self->created_cursors = 0;

    /* Create list of weak references to cursors */
    self->cursors = PyList_New(0);
    if (self->cursors == NULL) {
        return -1;
    }

    self->detect_types = detect_types;
    (void)sqlite3_busy_timeout(self->db, (int)(timeout*1000));
    self->thread_ident = PyThread_get_thread_ident();
    self->check_same_thread = check_same_thread;

    set_callback_context(&self->trace_ctx, NULL);
    set_callback_context(&self->progress_ctx, NULL);
    set_callback_context(&self->authorizer_ctx, NULL);

    self->Warning               = state->Warning;
    self->Error                 = state->Error;
    self->InterfaceError        = state->InterfaceError;
    self->DatabaseError         = state->DatabaseError;
    self->DataError             = state->DataError;
    self->OperationalError      = state->OperationalError;
    self->IntegrityError        = state->IntegrityError;
    self->InternalError         = state->InternalError;
    self->ProgrammingError      = state->ProgrammingError;
    self->NotSupportedError     = state->NotSupportedError;

    if (PySys_Audit("sqlite3.connect/handle", "O", self) < 0) {
        return -1;
    }

    self->initialized = 1;

    return 0;
}

static void
pysqlite_do_all_statements(pysqlite_Connection *self)
{
    // Reset all statements
    sqlite3_stmt *stmt = NULL;
    while ((stmt = sqlite3_next_stmt(self->db, stmt))) {
        if (sqlite3_stmt_busy(stmt)) {
            (void)sqlite3_reset(stmt);
        }
    }

    // Reset all cursors
    for (int i = 0; i < PyList_Size(self->cursors); i++) {
        PyObject *weakref = PyList_GetItem(self->cursors, i);
        PyObject *object = PyWeakref_GetObject(weakref);
        if (object != Py_None) {
            pysqlite_Cursor *cursor = (pysqlite_Cursor *)object;
            cursor->reset = 1;
        }
    }
}

#define VISIT_CALLBACK_CONTEXT(ctx) \
do {                                \
    if (ctx) {                      \
        Py_VISIT(ctx->callable);    \
        Py_VISIT(ctx->module);      \
    }                               \
} while (0)

static int
connection_traverse(pysqlite_Connection *self, visitproc visit, void *arg)
{
    Py_VISIT(Py_TYPE(self));
    Py_VISIT(self->statement_cache);
    Py_VISIT(self->cursors);
    Py_VISIT(self->row_factory);
    Py_VISIT(self->text_factory);
    VISIT_CALLBACK_CONTEXT(self->trace_ctx);
    VISIT_CALLBACK_CONTEXT(self->progress_ctx);
    VISIT_CALLBACK_CONTEXT(self->authorizer_ctx);
#undef VISIT_CALLBACK_CONTEXT
    return 0;
}

static inline void
clear_callback_context(callback_context *ctx)
{
    if (ctx != NULL) {
        Py_CLEAR(ctx->callable);
        Py_CLEAR(ctx->module);
    }
}

static int
connection_clear(pysqlite_Connection *self)
{
    Py_CLEAR(self->statement_cache);
    Py_CLEAR(self->cursors);
    Py_CLEAR(self->row_factory);
    Py_CLEAR(self->text_factory);
    clear_callback_context(self->trace_ctx);
    clear_callback_context(self->progress_ctx);
    clear_callback_context(self->authorizer_ctx);
    return 0;
}

static void
connection_close(pysqlite_Connection *self)
{
    if (self->db) {
        int rc = sqlite3_close_v2(self->db);
        assert(rc == SQLITE_OK), (void)rc;
        self->db = NULL;
    }
}

static void
free_callback_contexts(pysqlite_Connection *self)
{
    set_callback_context(&self->trace_ctx, NULL);
    set_callback_context(&self->progress_ctx, NULL);
    set_callback_context(&self->authorizer_ctx, NULL);
}

static void
connection_dealloc(pysqlite_Connection *self)
{
    PyTypeObject *tp = Py_TYPE(self);
    PyObject_GC_UnTrack(self);
    tp->tp_clear((PyObject *)self);

    /* Clean up if user has not called .close() explicitly. */
    connection_close(self);
    free_callback_contexts(self);

    tp->tp_free(self);
    Py_DECREF(tp);
}

/*
 * Registers a cursor with the connection.
 *
 * 0 => error; 1 => ok
 */
int pysqlite_connection_register_cursor(pysqlite_Connection* connection, PyObject* cursor)
{
    PyObject* weakref;

    weakref = PyWeakref_NewRef((PyObject*)cursor, NULL);
    if (!weakref) {
        goto error;
    }

    if (PyList_Append(connection->cursors, weakref) != 0) {
        Py_CLEAR(weakref);
        goto error;
    }

    Py_DECREF(weakref);

    return 1;
error:
    return 0;
}

/*[clinic input]
_sqlite3.Connection.cursor as pysqlite_connection_cursor

    factory: object = NULL

Return a cursor for the connection.
[clinic start generated code]*/

static PyObject *
pysqlite_connection_cursor_impl(pysqlite_Connection *self, PyObject *factory)
/*[clinic end generated code: output=562432a9e6af2aa1 input=4127345aa091b650]*/
{
    PyObject* cursor;

    if (!pysqlite_check_thread(self) || !pysqlite_check_connection(self)) {
        return NULL;
    }

    if (factory == NULL) {
        factory = (PyObject *)self->state->CursorType;
    }

    cursor = PyObject_CallOneArg(factory, (PyObject *)self);
    if (cursor == NULL)
        return NULL;
    if (!PyObject_TypeCheck(cursor, self->state->CursorType)) {
        PyErr_Format(PyExc_TypeError,
                     "factory must return a cursor, not %.100s",
                     Py_TYPE(cursor)->tp_name);
        Py_DECREF(cursor);
        return NULL;
    }

    _pysqlite_drop_unused_cursor_references(self);

    if (cursor && self->row_factory != Py_None) {
        Py_INCREF(self->row_factory);
        Py_XSETREF(((pysqlite_Cursor *)cursor)->row_factory, self->row_factory);
    }

    return cursor;
}

/*[clinic input]
_sqlite3.Connection.close as pysqlite_connection_close

Closes the connection.
[clinic start generated code]*/

static PyObject *
pysqlite_connection_close_impl(pysqlite_Connection *self)
/*[clinic end generated code: output=a546a0da212c9b97 input=3d58064bbffaa3d3]*/
{
    if (!pysqlite_check_thread(self)) {
        return NULL;
    }

    if (!self->initialized) {
        PyTypeObject *tp = Py_TYPE(self);
        pysqlite_state *state = pysqlite_get_state_by_type(tp);
        PyErr_SetString(state->ProgrammingError,
                        "Base Connection.__init__ not called.");
        return NULL;
    }

    Py_CLEAR(self->statement_cache);
    connection_close(self);

    Py_RETURN_NONE;
}

/*
 * Checks if a connection object is usable (i. e. not closed).
 *
 * 0 => error; 1 => ok
 */
int pysqlite_check_connection(pysqlite_Connection* con)
{
    if (!con->initialized) {
        pysqlite_state *state = pysqlite_get_state_by_type(Py_TYPE(con));
        PyErr_SetString(state->ProgrammingError,
                        "Base Connection.__init__ not called.");
        return 0;
    }

    if (!con->db) {
        PyErr_SetString(con->state->ProgrammingError,
                        "Cannot operate on a closed database.");
        return 0;
    } else {
        return 1;
    }
}

/*[clinic input]
_sqlite3.Connection.commit as pysqlite_connection_commit

Commit the current transaction.
[clinic start generated code]*/

static PyObject *
pysqlite_connection_commit_impl(pysqlite_Connection *self)
/*[clinic end generated code: output=3da45579e89407f2 input=39c12c04dda276a8]*/
{
    if (!pysqlite_check_thread(self) || !pysqlite_check_connection(self)) {
        return NULL;
    }

    if (!sqlite3_get_autocommit(self->db)) {
        int rc;

        Py_BEGIN_ALLOW_THREADS
        sqlite3_stmt *statement;
        rc = sqlite3_prepare_v2(self->db, "COMMIT", 7, &statement, NULL);
        if (rc == SQLITE_OK) {
            (void)sqlite3_step(statement);
            rc = sqlite3_finalize(statement);
        }
        Py_END_ALLOW_THREADS

        if (rc != SQLITE_OK) {
            (void)_pysqlite_seterror(self->state, self->db);
            return NULL;
        }
    }

    Py_RETURN_NONE;
}

/*[clinic input]
_sqlite3.Connection.rollback as pysqlite_connection_rollback

Roll back the current transaction.
[clinic start generated code]*/

static PyObject *
pysqlite_connection_rollback_impl(pysqlite_Connection *self)
/*[clinic end generated code: output=b66fa0d43e7ef305 input=12d4e8d068942830]*/
{
    if (!pysqlite_check_thread(self) || !pysqlite_check_connection(self)) {
        return NULL;
    }

    if (!sqlite3_get_autocommit(self->db)) {
        pysqlite_do_all_statements(self);

        int rc;

        Py_BEGIN_ALLOW_THREADS
        sqlite3_stmt *statement;
        rc = sqlite3_prepare_v2(self->db, "ROLLBACK", 9, &statement, NULL);
        if (rc == SQLITE_OK) {
            (void)sqlite3_step(statement);
            rc = sqlite3_finalize(statement);
        }
        Py_END_ALLOW_THREADS

        if (rc != SQLITE_OK) {
            (void)_pysqlite_seterror(self->state, self->db);
            return NULL;
        }

    }

    Py_RETURN_NONE;
}

static int
_pysqlite_set_result(sqlite3_context* context, PyObject* py_val)
{
    if (py_val == Py_None) {
        sqlite3_result_null(context);
    } else if (PyLong_Check(py_val)) {
        sqlite_int64 value = _pysqlite_long_as_int64(py_val);
        if (value == -1 && PyErr_Occurred())
            return -1;
        sqlite3_result_int64(context, value);
    } else if (PyFloat_Check(py_val)) {
        sqlite3_result_double(context, PyFloat_AsDouble(py_val));
    } else if (PyUnicode_Check(py_val)) {
        Py_ssize_t sz;
        const char *str = PyUnicode_AsUTF8AndSize(py_val, &sz);
        if (str == NULL) {
            return -1;
        }
        if (sz > INT_MAX) {
            PyErr_SetString(PyExc_OverflowError,
                            "string is longer than INT_MAX bytes");
            return -1;
        }
        sqlite3_result_text(context, str, (int)sz, SQLITE_TRANSIENT);
    } else if (PyObject_CheckBuffer(py_val)) {
        Py_buffer view;
        if (PyObject_GetBuffer(py_val, &view, PyBUF_SIMPLE) != 0) {
            PyErr_SetString(PyExc_ValueError,
                            "could not convert BLOB to buffer");
            return -1;
        }
        if (view.len > INT_MAX) {
            PyErr_SetString(PyExc_OverflowError,
                            "BLOB longer than INT_MAX bytes");
            PyBuffer_Release(&view);
            return -1;
        }
        sqlite3_result_blob(context, view.buf, (int)view.len, SQLITE_TRANSIENT);
        PyBuffer_Release(&view);
    } else {
        return -1;
    }
    return 0;
}

static PyObject *
_pysqlite_build_py_params(sqlite3_context *context, int argc,
                          sqlite3_value **argv)
{
    PyObject* args;
    int i;
    sqlite3_value* cur_value;
    PyObject* cur_py_value;

    args = PyTuple_New(argc);
    if (!args) {
        return NULL;
    }

    for (i = 0; i < argc; i++) {
        cur_value = argv[i];
        switch (sqlite3_value_type(argv[i])) {
            case SQLITE_INTEGER:
                cur_py_value = PyLong_FromLongLong(sqlite3_value_int64(cur_value));
                break;
            case SQLITE_FLOAT:
                cur_py_value = PyFloat_FromDouble(sqlite3_value_double(cur_value));
                break;
            case SQLITE_TEXT: {
                sqlite3 *db = sqlite3_context_db_handle(context);
                const char *text = (const char *)sqlite3_value_text(cur_value);

                if (text == NULL && sqlite3_errcode(db) == SQLITE_NOMEM) {
                    PyErr_NoMemory();
                    goto error;
                }

                Py_ssize_t size = sqlite3_value_bytes(cur_value);
                cur_py_value = PyUnicode_FromStringAndSize(text, size);
                break;
            }
            case SQLITE_BLOB: {
                sqlite3 *db = sqlite3_context_db_handle(context);
                const void *blob = sqlite3_value_blob(cur_value);

                if (blob == NULL && sqlite3_errcode(db) == SQLITE_NOMEM) {
                    PyErr_NoMemory();
                    goto error;
                }

                Py_ssize_t size = sqlite3_value_bytes(cur_value);
                cur_py_value = PyBytes_FromStringAndSize(blob, size);
                break;
            }
            case SQLITE_NULL:
            default:
                cur_py_value = Py_NewRef(Py_None);
        }

        if (!cur_py_value) {
            goto error;
        }

        PyTuple_SET_ITEM(args, i, cur_py_value);
    }

    return args;

error:
    Py_DECREF(args);
    return NULL;
}

static void
print_or_clear_traceback(callback_context *ctx)
{
    assert(ctx != NULL);
    assert(ctx->state != NULL);
    if (ctx->state->enable_callback_tracebacks) {
        PyErr_Print();
    }
    else {
        PyErr_Clear();
    }
}

// Checks the Python exception and sets the appropriate SQLite error code.
static void
set_sqlite_error(sqlite3_context *context, const char *msg)
{
    assert(PyErr_Occurred());
    if (PyErr_ExceptionMatches(PyExc_MemoryError)) {
        sqlite3_result_error_nomem(context);
    }
    else if (PyErr_ExceptionMatches(PyExc_OverflowError)) {
        sqlite3_result_error_toobig(context);
    }
    else {
        sqlite3_result_error(context, msg, -1);
    }
    callback_context *ctx = (callback_context *)sqlite3_user_data(context);
    print_or_clear_traceback(ctx);
}

static void
func_callback(sqlite3_context *context, int argc, sqlite3_value **argv)
{
    PyGILState_STATE threadstate = PyGILState_Ensure();

    PyObject* args;
    PyObject* py_retval = NULL;
    int ok;

    args = _pysqlite_build_py_params(context, argc, argv);
    if (args) {
        callback_context *ctx = (callback_context *)sqlite3_user_data(context);
        assert(ctx != NULL);
        py_retval = PyObject_CallObject(ctx->callable, args);
        Py_DECREF(args);
    }

    ok = 0;
    if (py_retval) {
        ok = _pysqlite_set_result(context, py_retval) == 0;
        Py_DECREF(py_retval);
    }
    if (!ok) {
        set_sqlite_error(context, "user-defined function raised exception");
    }

    PyGILState_Release(threadstate);
}

static void
step_callback(sqlite3_context *context, int argc, sqlite3_value **params)
{
    PyGILState_STATE threadstate = PyGILState_Ensure();

    PyObject* args;
    PyObject* function_result = NULL;
    PyObject** aggregate_instance;
    PyObject* stepmethod = NULL;

    aggregate_instance = (PyObject**)sqlite3_aggregate_context(context, sizeof(PyObject*));

    if (*aggregate_instance == NULL) {
        callback_context *ctx = (callback_context *)sqlite3_user_data(context);
        assert(ctx != NULL);
        *aggregate_instance = PyObject_CallNoArgs(ctx->callable);
        if (!*aggregate_instance) {
            set_sqlite_error(context,
                    "user-defined aggregate's '__init__' method raised error");
            goto error;
        }
    }

    stepmethod = PyObject_GetAttrString(*aggregate_instance, "step");
    if (!stepmethod) {
        goto error;
    }

    args = _pysqlite_build_py_params(context, argc, params);
    if (!args) {
        goto error;
    }

    function_result = PyObject_CallObject(stepmethod, args);
    Py_DECREF(args);

    if (!function_result) {
        set_sqlite_error(context,
                "user-defined aggregate's 'step' method raised error");
    }

error:
    Py_XDECREF(stepmethod);
    Py_XDECREF(function_result);

    PyGILState_Release(threadstate);
}

static void
final_callback(sqlite3_context *context)
{
    PyGILState_STATE threadstate = PyGILState_Ensure();

    PyObject* function_result;
    PyObject** aggregate_instance;
    _Py_IDENTIFIER(finalize);
    int ok;
    PyObject *exception, *value, *tb;

    aggregate_instance = (PyObject**)sqlite3_aggregate_context(context, 0);
    if (aggregate_instance == NULL) {
        /* No rows matched the query; the step handler was never called. */
        goto error;
    }
    else if (!*aggregate_instance) {
        /* this branch is executed if there was an exception in the aggregate's
         * __init__ */

        goto error;
    }

    /* Keep the exception (if any) of the last call to step() */
    PyErr_Fetch(&exception, &value, &tb);

    function_result = _PyObject_CallMethodIdNoArgs(*aggregate_instance, &PyId_finalize);

    Py_DECREF(*aggregate_instance);

    ok = 0;
    if (function_result) {
        ok = _pysqlite_set_result(context, function_result) == 0;
        Py_DECREF(function_result);
    }
    if (!ok) {
        set_sqlite_error(context,
                "user-defined aggregate's 'finalize' method raised error");
    }

    /* Restore the exception (if any) of the last call to step(),
       but clear also the current exception if finalize() failed */
    PyErr_Restore(exception, value, tb);

error:
    PyGILState_Release(threadstate);
}

static void _pysqlite_drop_unused_cursor_references(pysqlite_Connection* self)
{
    PyObject* new_list;
    PyObject* weakref;
    int i;

    /* we only need to do this once in a while */
    if (self->created_cursors++ < 200) {
        return;
    }

    self->created_cursors = 0;

    new_list = PyList_New(0);
    if (!new_list) {
        return;
    }

    for (i = 0; i < PyList_Size(self->cursors); i++) {
        weakref = PyList_GetItem(self->cursors, i);
        if (PyWeakref_GetObject(weakref) != Py_None) {
            if (PyList_Append(new_list, weakref) != 0) {
                Py_DECREF(new_list);
                return;
            }
        }
    }

    Py_SETREF(self->cursors, new_list);
}

/* Allocate a UDF/callback context structure. In order to ensure that the state
 * pointer always outlives the callback context, we make sure it owns a
 * reference to the module itself. create_callback_context() is always called
 * from connection methods, so we use the defining class to fetch the module
 * pointer.
 */
static callback_context *
create_callback_context(PyTypeObject *cls, PyObject *callable)
{
    callback_context *ctx = PyMem_Malloc(sizeof(callback_context));
    if (ctx != NULL) {
        PyObject *module = PyType_GetModule(cls);
        ctx->callable = Py_NewRef(callable);
        ctx->module = Py_NewRef(module);
        ctx->state = pysqlite_get_state(module);
    }
    return ctx;
}

static void
free_callback_context(callback_context *ctx)
{
    assert(ctx != NULL);
    Py_XDECREF(ctx->callable);
    Py_XDECREF(ctx->module);
    PyMem_Free(ctx);
}

static void
set_callback_context(callback_context **ctx_pp, callback_context *ctx)
{
    assert(ctx_pp != NULL);
    callback_context *tmp = *ctx_pp;
    *ctx_pp = ctx;
    if (tmp != NULL) {
        free_callback_context(tmp);
    }
}

static void
destructor_callback(void *ctx)
{
    if (ctx != NULL) {
        // This function may be called without the GIL held, so we need to
        // ensure that we destroy 'ctx' with the GIL held.
        PyGILState_STATE gstate = PyGILState_Ensure();
        free_callback_context((callback_context *)ctx);
        PyGILState_Release(gstate);
    }
}

/*[clinic input]
_sqlite3.Connection.create_function as pysqlite_connection_create_function

    cls: defining_class
    /
    name: str
    narg: int
    func: object
    *
    deterministic: bool = False

Creates a new function. Non-standard.
[clinic start generated code]*/

static PyObject *
pysqlite_connection_create_function_impl(pysqlite_Connection *self,
                                         PyTypeObject *cls, const char *name,
                                         int narg, PyObject *func,
                                         int deterministic)
/*[clinic end generated code: output=8a811529287ad240 input=f0f99754bfeafd8d]*/
{
    int rc;
    int flags = SQLITE_UTF8;

    if (!pysqlite_check_thread(self) || !pysqlite_check_connection(self)) {
        return NULL;
    }

    if (deterministic) {
#if SQLITE_VERSION_NUMBER < 3008003
        PyErr_SetString(self->NotSupportedError,
                        "deterministic=True requires SQLite 3.8.3 or higher");
        return NULL;
#else
        if (sqlite3_libversion_number() < 3008003) {
            PyErr_SetString(self->NotSupportedError,
                            "deterministic=True requires SQLite 3.8.3 or higher");
            return NULL;
        }
        flags |= SQLITE_DETERMINISTIC;
#endif
    }
    callback_context *ctx = create_callback_context(cls, func);
    if (ctx == NULL) {
        return NULL;
    }
    rc = sqlite3_create_function_v2(self->db, name, narg, flags, ctx,
                                    func_callback,
                                    NULL,
                                    NULL,
                                    &destructor_callback);  // will decref func

    if (rc != SQLITE_OK) {
        /* Workaround for SQLite bug: no error code or string is available here */
        PyErr_SetString(self->OperationalError, "Error creating function");
        return NULL;
    }
    Py_RETURN_NONE;
}

/*[clinic input]
_sqlite3.Connection.create_aggregate as pysqlite_connection_create_aggregate

    cls: defining_class
    /
    name: str
    n_arg: int
    aggregate_class: object

Creates a new aggregate. Non-standard.
[clinic start generated code]*/

static PyObject *
pysqlite_connection_create_aggregate_impl(pysqlite_Connection *self,
                                          PyTypeObject *cls,
                                          const char *name, int n_arg,
                                          PyObject *aggregate_class)
/*[clinic end generated code: output=1b02d0f0aec7ff96 input=bd527067e6c2e33f]*/
{
    int rc;

    if (!pysqlite_check_thread(self) || !pysqlite_check_connection(self)) {
        return NULL;
    }

    callback_context *ctx = create_callback_context(cls, aggregate_class);
    if (ctx == NULL) {
        return NULL;
    }
    rc = sqlite3_create_function_v2(self->db, name, n_arg, SQLITE_UTF8, ctx,
                                    0,
                                    &step_callback,
                                    &final_callback,
                                    &destructor_callback); // will decref func
    if (rc != SQLITE_OK) {
        /* Workaround for SQLite bug: no error code or string is available here */
        PyErr_SetString(self->OperationalError, "Error creating aggregate");
        return NULL;
    }
    Py_RETURN_NONE;
}

static int
authorizer_callback(void *ctx, int action, const char *arg1,
                    const char *arg2 , const char *dbname,
                    const char *access_attempt_source)
{
    PyGILState_STATE gilstate = PyGILState_Ensure();

    PyObject *ret;
    int rc = SQLITE_DENY;

    assert(ctx != NULL);
    PyObject *callable = ((callback_context *)ctx)->callable;
    ret = PyObject_CallFunction(callable, "issss", action, arg1, arg2, dbname,
                                access_attempt_source);

    if (ret == NULL) {
        print_or_clear_traceback(ctx);
        rc = SQLITE_DENY;
    }
    else {
        if (PyLong_Check(ret)) {
            rc = _PyLong_AsInt(ret);
            if (rc == -1 && PyErr_Occurred()) {
                print_or_clear_traceback(ctx);
                rc = SQLITE_DENY;
            }
        }
        else {
            rc = SQLITE_DENY;
        }
        Py_DECREF(ret);
    }

    PyGILState_Release(gilstate);
    return rc;
}

static int
progress_callback(void *ctx)
{
    PyGILState_STATE gilstate = PyGILState_Ensure();

    int rc;
    PyObject *ret;

    assert(ctx != NULL);
    PyObject *callable = ((callback_context *)ctx)->callable;
    ret = PyObject_CallNoArgs(callable);
    if (!ret) {
        /* abort query if error occurred */
        rc = -1;
    }
    else {
        rc = PyObject_IsTrue(ret);
        Py_DECREF(ret);
    }
    if (rc < 0) {
        print_or_clear_traceback(ctx);
    }

    PyGILState_Release(gilstate);
    return rc;
}

#ifdef HAVE_TRACE_V2
/*
 * From https://sqlite.org/c3ref/trace_v2.html:
 * The integer return value from the callback is currently ignored, though this
 * may change in future releases. Callback implementations should return zero
 * to ensure future compatibility.
 */
static int
trace_callback(unsigned int type, void *ctx, void *prepared_statement,
               void *statement_string)
#else
static void
trace_callback(void *ctx, const char *statement_string)
#endif
{
#ifdef HAVE_TRACE_V2
    if (type != SQLITE_TRACE_STMT) {
        return 0;
    }
#endif

    PyGILState_STATE gilstate = PyGILState_Ensure();

    PyObject *py_statement = NULL;
    PyObject *ret = NULL;
    py_statement = PyUnicode_DecodeUTF8(statement_string,
            strlen(statement_string), "replace");
    assert(ctx != NULL);
    if (py_statement) {
        PyObject *callable = ((callback_context *)ctx)->callable;
        ret = PyObject_CallOneArg(callable, py_statement);
        Py_DECREF(py_statement);
    }

    if (ret) {
        Py_DECREF(ret);
    }
    else {
        print_or_clear_traceback(ctx);
    }

    PyGILState_Release(gilstate);
#ifdef HAVE_TRACE_V2
    return 0;
#endif
}

/*[clinic input]
_sqlite3.Connection.set_authorizer as pysqlite_connection_set_authorizer

    cls: defining_class
    /
    authorizer_callback as callable: object

Sets authorizer callback. Non-standard.
[clinic start generated code]*/

static PyObject *
pysqlite_connection_set_authorizer_impl(pysqlite_Connection *self,
                                        PyTypeObject *cls,
                                        PyObject *callable)
/*[clinic end generated code: output=75fa60114fc971c3 input=9f3e90d3d642c4a0]*/
{
    if (!pysqlite_check_thread(self) || !pysqlite_check_connection(self)) {
        return NULL;
    }

    int rc;
    if (callable == Py_None) {
        rc = sqlite3_set_authorizer(self->db, NULL, NULL);
        set_callback_context(&self->authorizer_ctx, NULL);
    }
    else {
        callback_context *ctx = create_callback_context(cls, callable);
        if (ctx == NULL) {
            return NULL;
        }
        rc = sqlite3_set_authorizer(self->db, authorizer_callback, ctx);
        set_callback_context(&self->authorizer_ctx, ctx);
    }
    if (rc != SQLITE_OK) {
        PyErr_SetString(self->OperationalError,
                        "Error setting authorizer callback");
        set_callback_context(&self->authorizer_ctx, NULL);
        return NULL;
    }
    Py_RETURN_NONE;
}

/*[clinic input]
_sqlite3.Connection.set_progress_handler as pysqlite_connection_set_progress_handler

    cls: defining_class
    /
    progress_handler as callable: object
    n: int

Sets progress handler callback. Non-standard.
[clinic start generated code]*/

static PyObject *
pysqlite_connection_set_progress_handler_impl(pysqlite_Connection *self,
                                              PyTypeObject *cls,
                                              PyObject *callable, int n)
/*[clinic end generated code: output=0739957fd8034a50 input=83e8dcbb4ce183f7]*/
{
    if (!pysqlite_check_thread(self) || !pysqlite_check_connection(self)) {
        return NULL;
    }

    if (callable == Py_None) {
        /* None clears the progress handler previously set */
        sqlite3_progress_handler(self->db, 0, 0, (void*)0);
        set_callback_context(&self->progress_ctx, NULL);
    }
    else {
        callback_context *ctx = create_callback_context(cls, callable);
        if (ctx == NULL) {
            return NULL;
        }
        sqlite3_progress_handler(self->db, n, progress_callback, ctx);
        set_callback_context(&self->progress_ctx, ctx);
    }
    Py_RETURN_NONE;
}

/*[clinic input]
_sqlite3.Connection.set_trace_callback as pysqlite_connection_set_trace_callback

    cls: defining_class
    /
    trace_callback as callable: object

Sets a trace callback called for each SQL statement (passed as unicode).

Non-standard.
[clinic start generated code]*/

static PyObject *
pysqlite_connection_set_trace_callback_impl(pysqlite_Connection *self,
                                            PyTypeObject *cls,
                                            PyObject *callable)
/*[clinic end generated code: output=d91048c03bfcee05 input=96f03acec3ec8044]*/
{
    if (!pysqlite_check_thread(self) || !pysqlite_check_connection(self)) {
        return NULL;
    }

    if (callable == Py_None) {
        /*
         * None clears the trace callback previously set
         *
         * Ref.
         * - https://sqlite.org/c3ref/c_trace.html
         * - https://sqlite.org/c3ref/trace_v2.html
         */
#ifdef HAVE_TRACE_V2
        sqlite3_trace_v2(self->db, SQLITE_TRACE_STMT, 0, 0);
#else
        sqlite3_trace(self->db, 0, (void*)0);
#endif
        set_callback_context(&self->trace_ctx, NULL);
    }
    else {
        callback_context *ctx = create_callback_context(cls, callable);
        if (ctx == NULL) {
            return NULL;
        }
#ifdef HAVE_TRACE_V2
        sqlite3_trace_v2(self->db, SQLITE_TRACE_STMT, trace_callback, ctx);
#else
        sqlite3_trace(self->db, trace_callback, ctx);
#endif
        set_callback_context(&self->trace_ctx, ctx);
    }

    Py_RETURN_NONE;
}

#ifdef PY_SQLITE_ENABLE_LOAD_EXTENSION
/*[clinic input]
_sqlite3.Connection.enable_load_extension as pysqlite_connection_enable_load_extension

    enable as onoff: bool(accept={int})
    /

Enable dynamic loading of SQLite extension modules. Non-standard.
[clinic start generated code]*/

static PyObject *
pysqlite_connection_enable_load_extension_impl(pysqlite_Connection *self,
                                               int onoff)
/*[clinic end generated code: output=9cac37190d388baf input=5c0da5b121121cbc]*/
{
    int rc;

    if (PySys_Audit("sqlite3.enable_load_extension",
                    "OO", self, onoff ? Py_True : Py_False) < 0) {
        return NULL;
    }

    if (!pysqlite_check_thread(self) || !pysqlite_check_connection(self)) {
        return NULL;
    }

    rc = sqlite3_enable_load_extension(self->db, onoff);

    if (rc != SQLITE_OK) {
        PyErr_SetString(self->OperationalError,
                        "Error enabling load extension");
        return NULL;
    } else {
        Py_RETURN_NONE;
    }
}

/*[clinic input]
_sqlite3.Connection.load_extension as pysqlite_connection_load_extension

    name as extension_name: str
    /

Load SQLite extension module. Non-standard.
[clinic start generated code]*/

static PyObject *
pysqlite_connection_load_extension_impl(pysqlite_Connection *self,
                                        const char *extension_name)
/*[clinic end generated code: output=47eb1d7312bc97a7 input=0b711574560db9fc]*/
{
    int rc;
    char* errmsg;

    if (PySys_Audit("sqlite3.load_extension", "Os", self, extension_name) < 0) {
        return NULL;
    }

    if (!pysqlite_check_thread(self) || !pysqlite_check_connection(self)) {
        return NULL;
    }

    rc = sqlite3_load_extension(self->db, extension_name, 0, &errmsg);
    if (rc != 0) {
        PyErr_SetString(self->OperationalError, errmsg);
        return NULL;
    } else {
        Py_RETURN_NONE;
    }
}
#endif

int pysqlite_check_thread(pysqlite_Connection* self)
{
    if (self->check_same_thread) {
        if (PyThread_get_thread_ident() != self->thread_ident) {
            PyErr_Format(self->ProgrammingError,
                        "SQLite objects created in a thread can only be used in that same thread. "
                        "The object was created in thread id %lu and this is thread id %lu.",
                        self->thread_ident, PyThread_get_thread_ident());
            return 0;
        }

    }
    return 1;
}

static PyObject* pysqlite_connection_get_isolation_level(pysqlite_Connection* self, void* unused)
{
    if (!pysqlite_check_connection(self)) {
        return NULL;
    }
    if (self->begin_statement != NULL) {
        const char *stmt = self->begin_statement;
        const char *iso_level = begin_stmt_to_isolation_level(stmt);
        return PyUnicode_FromString(iso_level);
    }
    Py_RETURN_NONE;
}

static PyObject* pysqlite_connection_get_total_changes(pysqlite_Connection* self, void* unused)
{
    if (!pysqlite_check_connection(self)) {
        return NULL;
    } else {
        return Py_BuildValue("i", sqlite3_total_changes(self->db));
    }
}

static PyObject* pysqlite_connection_get_in_transaction(pysqlite_Connection* self, void* unused)
{
    if (!pysqlite_check_connection(self)) {
        return NULL;
    }
    if (!sqlite3_get_autocommit(self->db)) {
        Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
}

static int
pysqlite_connection_set_isolation_level(pysqlite_Connection* self, PyObject* isolation_level, void *Py_UNUSED(ignored))
{
    if (isolation_level == NULL) {
        PyErr_SetString(PyExc_AttributeError, "cannot delete attribute");
        return -1;
    }
    if (Py_IsNone(isolation_level)) {
        self->begin_statement = NULL;

        // Execute a COMMIT to re-enable autocommit mode
        PyObject *res = pysqlite_connection_commit_impl(self);
        if (res == NULL) {
            return -1;
        }
        Py_DECREF(res);
    }
    else if (PyUnicode_Check(isolation_level)) {
        Py_ssize_t len;
        const char *cstr_level = PyUnicode_AsUTF8AndSize(isolation_level, &len);
        if (cstr_level == NULL) {
            return -1;
        }
        if (strlen(cstr_level) != (size_t)len) {
            PyErr_SetString(PyExc_ValueError, "embedded null character");
            return -1;
        }
        const char *stmt = get_begin_statement(cstr_level);
        if (stmt == NULL) {
            return -1;
        }
        self->begin_statement = stmt;
    }
    else {
        PyErr_SetString(PyExc_TypeError,
                        "isolation_level must be str or None");
        return -1;
    }
    return 0;
}

static PyObject *
pysqlite_connection_call(pysqlite_Connection *self, PyObject *args,
                         PyObject *kwargs)
{
    PyObject* sql;
    pysqlite_Statement* statement;

    if (!pysqlite_check_thread(self) || !pysqlite_check_connection(self)) {
        return NULL;
    }

    if (!_PyArg_NoKeywords(MODULE_NAME ".Connection", kwargs))
        return NULL;

    if (!PyArg_ParseTuple(args, "U", &sql))
        return NULL;

    statement = pysqlite_statement_create(self, sql);
    if (statement == NULL) {
        return NULL;
    }

    return (PyObject*)statement;
}

/*[clinic input]
_sqlite3.Connection.execute as pysqlite_connection_execute

    sql: unicode
    parameters: object = NULL
    /

Executes a SQL statement. Non-standard.
[clinic start generated code]*/

static PyObject *
pysqlite_connection_execute_impl(pysqlite_Connection *self, PyObject *sql,
                                 PyObject *parameters)
/*[clinic end generated code: output=5be05ae01ee17ee4 input=fbd17c75c7140271]*/
{
    _Py_IDENTIFIER(execute);
    PyObject* cursor = 0;
    PyObject* result = 0;

    cursor = _PyObject_CallMethodIdNoArgs((PyObject*)self, &PyId_cursor);
    if (!cursor) {
        goto error;
    }

    result = _PyObject_CallMethodIdObjArgs(cursor, &PyId_execute, sql, parameters, NULL);
    if (!result) {
        Py_CLEAR(cursor);
    }

error:
    Py_XDECREF(result);

    return cursor;
}

/*[clinic input]
_sqlite3.Connection.executemany as pysqlite_connection_executemany

    sql: unicode
    parameters: object
    /

Repeatedly executes a SQL statement. Non-standard.
[clinic start generated code]*/

static PyObject *
pysqlite_connection_executemany_impl(pysqlite_Connection *self,
                                     PyObject *sql, PyObject *parameters)
/*[clinic end generated code: output=776cd2fd20bfe71f input=4feab80659ffc82b]*/
{
    _Py_IDENTIFIER(executemany);
    PyObject* cursor = 0;
    PyObject* result = 0;

    cursor = _PyObject_CallMethodIdNoArgs((PyObject*)self, &PyId_cursor);
    if (!cursor) {
        goto error;
    }

    result = _PyObject_CallMethodIdObjArgs(cursor, &PyId_executemany, sql,
                                           parameters, NULL);
    if (!result) {
        Py_CLEAR(cursor);
    }

error:
    Py_XDECREF(result);

    return cursor;
}

/*[clinic input]
_sqlite3.Connection.executescript as pysqlite_connection_executescript

    sql_script as script_obj: object
    /

Executes multiple SQL statements at once. Non-standard.
[clinic start generated code]*/

static PyObject *
pysqlite_connection_executescript(pysqlite_Connection *self,
                                  PyObject *script_obj)
/*[clinic end generated code: output=4c4f9d77aa0ae37d input=b27ae5c24ffb8b43]*/
{
    _Py_IDENTIFIER(executescript);
    PyObject* cursor = 0;
    PyObject* result = 0;

    cursor = _PyObject_CallMethodIdNoArgs((PyObject*)self, &PyId_cursor);
    if (!cursor) {
        goto error;
    }

    result = _PyObject_CallMethodIdObjArgs(cursor, &PyId_executescript,
                                           script_obj, NULL);
    if (!result) {
        Py_CLEAR(cursor);
    }

error:
    Py_XDECREF(result);

    return cursor;
}

/* ------------------------- COLLATION CODE ------------------------ */

static int
collation_callback(void *context, int text1_length, const void *text1_data,
                   int text2_length, const void *text2_data)
{
    PyGILState_STATE gilstate = PyGILState_Ensure();

    PyObject* string1 = 0;
    PyObject* string2 = 0;
    PyObject* retval = NULL;
    long longval;
    int result = 0;

    /* This callback may be executed multiple times per sqlite3_step(). Bail if
     * the previous call failed */
    if (PyErr_Occurred()) {
        goto finally;
    }

    string1 = PyUnicode_FromStringAndSize((const char*)text1_data, text1_length);
    string2 = PyUnicode_FromStringAndSize((const char*)text2_data, text2_length);

    if (!string1 || !string2) {
        goto finally; /* failed to allocate strings */
    }

    callback_context *ctx = (callback_context *)context;
    assert(ctx != NULL);
    PyObject *args[] = { NULL, string1, string2 };  // Borrowed refs.
    size_t nargsf = 2 | PY_VECTORCALL_ARGUMENTS_OFFSET;
    retval = PyObject_Vectorcall(ctx->callable, args + 1, nargsf, NULL);
    if (retval == NULL) {
        /* execution failed */
        goto finally;
    }

    longval = PyLong_AsLongAndOverflow(retval, &result);
    if (longval == -1 && PyErr_Occurred()) {
        PyErr_Clear();
        result = 0;
    }
    else if (!result) {
        if (longval > 0)
            result = 1;
        else if (longval < 0)
            result = -1;
    }

finally:
    Py_XDECREF(string1);
    Py_XDECREF(string2);
    Py_XDECREF(retval);
    PyGILState_Release(gilstate);
    return result;
}

/*[clinic input]
_sqlite3.Connection.interrupt as pysqlite_connection_interrupt

Abort any pending database operation. Non-standard.
[clinic start generated code]*/

static PyObject *
pysqlite_connection_interrupt_impl(pysqlite_Connection *self)
/*[clinic end generated code: output=f193204bc9e70b47 input=4bd0ad083cf93aa7]*/
{
    PyObject* retval = NULL;

    if (!pysqlite_check_connection(self)) {
        goto finally;
    }

    sqlite3_interrupt(self->db);

    retval = Py_NewRef(Py_None);

finally:
    return retval;
}

/* Function author: Paul Kippes <kippesp@gmail.com>
 * Class method of Connection to call the Python function _iterdump
 * of the sqlite3 module.
 */
/*[clinic input]
_sqlite3.Connection.iterdump as pysqlite_connection_iterdump

Returns iterator to the dump of the database in an SQL text format.

Non-standard.
[clinic start generated code]*/

static PyObject *
pysqlite_connection_iterdump_impl(pysqlite_Connection *self)
/*[clinic end generated code: output=586997aaf9808768 input=53bc907cb5eedb85]*/
{
    _Py_IDENTIFIER(_iterdump);
    PyObject* retval = NULL;
    PyObject* module = NULL;
    PyObject* module_dict;
    PyObject* pyfn_iterdump;

    if (!pysqlite_check_connection(self)) {
        goto finally;
    }

    module = PyImport_ImportModule(MODULE_NAME ".dump");
    if (!module) {
        goto finally;
    }

    module_dict = PyModule_GetDict(module);
    if (!module_dict) {
        goto finally;
    }

    pyfn_iterdump = _PyDict_GetItemIdWithError(module_dict, &PyId__iterdump);
    if (!pyfn_iterdump) {
        if (!PyErr_Occurred()) {
            PyErr_SetString(self->OperationalError,
                            "Failed to obtain _iterdump() reference");
        }
        goto finally;
    }

    retval = PyObject_CallOneArg(pyfn_iterdump, (PyObject *)self);

finally:
    Py_XDECREF(module);
    return retval;
}

/*[clinic input]
_sqlite3.Connection.backup as pysqlite_connection_backup

    target: object(type='pysqlite_Connection *', subclass_of='clinic_state()->ConnectionType')
    *
    pages: int = -1
    progress: object = None
    name: str = "main"
    sleep: double = 0.250

Makes a backup of the database. Non-standard.
[clinic start generated code]*/

static PyObject *
pysqlite_connection_backup_impl(pysqlite_Connection *self,
                                pysqlite_Connection *target, int pages,
                                PyObject *progress, const char *name,
                                double sleep)
/*[clinic end generated code: output=306a3e6a38c36334 input=c759627ab1ad46ff]*/
{
    int rc;
    int sleep_ms = (int)(sleep * 1000.0);
    sqlite3 *bck_conn;
    sqlite3_backup *bck_handle;

    if (!pysqlite_check_thread(self) || !pysqlite_check_connection(self)) {
        return NULL;
    }

    if (!pysqlite_check_connection(target)) {
        return NULL;
    }

    if (target == self) {
        PyErr_SetString(PyExc_ValueError, "target cannot be the same connection instance");
        return NULL;
    }

#if SQLITE_VERSION_NUMBER < 3008008
    /* Since 3.8.8 this is already done, per commit
       https://www.sqlite.org/src/info/169b5505498c0a7e */
    if (!sqlite3_get_autocommit(target->db)) {
        PyErr_SetString(self->OperationalError, "target is in transaction");
        return NULL;
    }
#endif

    if (progress != Py_None && !PyCallable_Check(progress)) {
        PyErr_SetString(PyExc_TypeError, "progress argument must be a callable");
        return NULL;
    }

    if (pages == 0) {
        pages = -1;
    }

    bck_conn = target->db;

    Py_BEGIN_ALLOW_THREADS
    bck_handle = sqlite3_backup_init(bck_conn, "main", self->db, name);
    Py_END_ALLOW_THREADS

    if (bck_handle == NULL) {
        _pysqlite_seterror(self->state, bck_conn);
        return NULL;
    }

    do {
        Py_BEGIN_ALLOW_THREADS
        rc = sqlite3_backup_step(bck_handle, pages);
        Py_END_ALLOW_THREADS

        if (progress != Py_None) {
            int remaining = sqlite3_backup_remaining(bck_handle);
            int pagecount = sqlite3_backup_pagecount(bck_handle);
            PyObject *res = PyObject_CallFunction(progress, "iii", rc,
                                                  remaining, pagecount);
            if (res == NULL) {
                /* Callback failed: abort backup and bail. */
                Py_BEGIN_ALLOW_THREADS
                sqlite3_backup_finish(bck_handle);
                Py_END_ALLOW_THREADS
                return NULL;
            }
            Py_DECREF(res);
        }

        /* Sleep for a while if there are still further pages to copy and
           the engine could not make any progress */
        if (rc == SQLITE_BUSY || rc == SQLITE_LOCKED) {
            Py_BEGIN_ALLOW_THREADS
            sqlite3_sleep(sleep_ms);
            Py_END_ALLOW_THREADS
        }
    } while (rc == SQLITE_OK || rc == SQLITE_BUSY || rc == SQLITE_LOCKED);

    Py_BEGIN_ALLOW_THREADS
    rc = sqlite3_backup_finish(bck_handle);
    Py_END_ALLOW_THREADS

    if (rc != SQLITE_OK) {
        _pysqlite_seterror(self->state, bck_conn);
        return NULL;
    }

    Py_RETURN_NONE;
}

/*[clinic input]
_sqlite3.Connection.create_collation as pysqlite_connection_create_collation

    cls: defining_class
    name: str
    callback as callable: object
    /

Creates a collation function. Non-standard.
[clinic start generated code]*/

static PyObject *
pysqlite_connection_create_collation_impl(pysqlite_Connection *self,
                                          PyTypeObject *cls,
                                          const char *name,
                                          PyObject *callable)
/*[clinic end generated code: output=32d339e97869c378 input=fee2c8e5708602ad]*/
{
    if (!pysqlite_check_thread(self) || !pysqlite_check_connection(self)) {
        return NULL;
    }

    callback_context *ctx = NULL;
    int rc;
    int flags = SQLITE_UTF8;
    if (callable == Py_None) {
        rc = sqlite3_create_collation_v2(self->db, name, flags,
                                         NULL, NULL, NULL);
    }
    else {
        if (!PyCallable_Check(callable)) {
            PyErr_SetString(PyExc_TypeError, "parameter must be callable");
            return NULL;
        }
        ctx = create_callback_context(cls, callable);
        if (ctx == NULL) {
            return NULL;
        }
        rc = sqlite3_create_collation_v2(self->db, name, flags, ctx,
                                         &collation_callback,
                                         &destructor_callback);
    }

    if (rc != SQLITE_OK) {
        /* Unlike other sqlite3_* functions, the destructor callback is _not_
         * called if sqlite3_create_collation_v2() fails, so we have to free
         * the context before returning.
         */
        if (callable != Py_None) {
            free_callback_context(ctx);
        }
        _pysqlite_seterror(self->state, self->db);
        return NULL;
    }

    Py_RETURN_NONE;
}

/*[clinic input]
_sqlite3.Connection.__enter__ as pysqlite_connection_enter

Called when the connection is used as a context manager.

Returns itself as a convenience to the caller.
[clinic start generated code]*/

static PyObject *
pysqlite_connection_enter_impl(pysqlite_Connection *self)
/*[clinic end generated code: output=457b09726d3e9dcd input=127d7a4f17e86d8f]*/
{
    if (!pysqlite_check_connection(self)) {
        return NULL;
    }
    return Py_NewRef((PyObject *)self);
}

/*[clinic input]
_sqlite3.Connection.__exit__ as pysqlite_connection_exit

    type as exc_type: object
    value as exc_value: object
    traceback as exc_tb: object
    /

Called when the connection is used as a context manager.

If there was any exception, a rollback takes place; otherwise we commit.
[clinic start generated code]*/

static PyObject *
pysqlite_connection_exit_impl(pysqlite_Connection *self, PyObject *exc_type,
                              PyObject *exc_value, PyObject *exc_tb)
/*[clinic end generated code: output=0705200e9321202a input=bd66f1532c9c54a7]*/
{
    int commit = 0;
    PyObject* result;

    if (exc_type == Py_None && exc_value == Py_None && exc_tb == Py_None) {
        commit = 1;
        result = pysqlite_connection_commit_impl(self);
    }
    else {
        result = pysqlite_connection_rollback_impl(self);
    }

    if (result == NULL) {
        if (commit) {
            /* Commit failed; try to rollback in order to unlock the database.
             * If rollback also fails, chain the exceptions. */
            PyObject *exc, *val, *tb;
            PyErr_Fetch(&exc, &val, &tb);
            result = pysqlite_connection_rollback_impl(self);
            if (result == NULL) {
                _PyErr_ChainExceptions(exc, val, tb);
            }
            else {
                Py_DECREF(result);
                PyErr_Restore(exc, val, tb);
            }
        }
        return NULL;
    }
    Py_DECREF(result);

    Py_RETURN_FALSE;
}

/*[clinic input]
_sqlite3.Connection.setlimit as setlimit

    category: int
        The limit category to be set.
    limit: int
        The new limit. If the new limit is a negative number, the limit is
        unchanged.
    /

Set connection run-time limits.

Attempts to increase a limit above its hard upper bound are silently truncated
to the hard upper bound. Regardless of whether or not the limit was changed,
the prior value of the limit is returned.
[clinic start generated code]*/

static PyObject *
setlimit_impl(pysqlite_Connection *self, int category, int limit)
/*[clinic end generated code: output=0d208213f8d68ccd input=9bd469537e195635]*/
{
    if (!pysqlite_check_thread(self) || !pysqlite_check_connection(self)) {
        return NULL;
    }

    int old_limit = sqlite3_limit(self->db, category, limit);
    if (old_limit < 0) {
        PyErr_SetString(self->ProgrammingError, "'category' is out of bounds");
        return NULL;
    }
    return PyLong_FromLong(old_limit);
}

/*[clinic input]
_sqlite3.Connection.getlimit as getlimit

    category: int
        The limit category to be queried.
    /

Get connection run-time limits.
[clinic start generated code]*/

static PyObject *
getlimit_impl(pysqlite_Connection *self, int category)
/*[clinic end generated code: output=7c3f5d11f24cecb1 input=61e0849fb4fb058f]*/
{
    return setlimit_impl(self, category, -1);
}


static const char connection_doc[] =
PyDoc_STR("SQLite database connection object.");

static PyGetSetDef connection_getset[] = {
    {"isolation_level",  (getter)pysqlite_connection_get_isolation_level, (setter)pysqlite_connection_set_isolation_level},
    {"total_changes",  (getter)pysqlite_connection_get_total_changes, (setter)0},
    {"in_transaction",  (getter)pysqlite_connection_get_in_transaction, (setter)0},
    {NULL}
};

static PyMethodDef connection_methods[] = {
    PYSQLITE_CONNECTION_BACKUP_METHODDEF
    PYSQLITE_CONNECTION_CLOSE_METHODDEF
    PYSQLITE_CONNECTION_COMMIT_METHODDEF
    PYSQLITE_CONNECTION_CREATE_AGGREGATE_METHODDEF
    PYSQLITE_CONNECTION_CREATE_COLLATION_METHODDEF
    PYSQLITE_CONNECTION_CREATE_FUNCTION_METHODDEF
    PYSQLITE_CONNECTION_CURSOR_METHODDEF
    PYSQLITE_CONNECTION_ENABLE_LOAD_EXTENSION_METHODDEF
    PYSQLITE_CONNECTION_ENTER_METHODDEF
    PYSQLITE_CONNECTION_EXECUTEMANY_METHODDEF
    PYSQLITE_CONNECTION_EXECUTESCRIPT_METHODDEF
    PYSQLITE_CONNECTION_EXECUTE_METHODDEF
    PYSQLITE_CONNECTION_EXIT_METHODDEF
    PYSQLITE_CONNECTION_INTERRUPT_METHODDEF
    PYSQLITE_CONNECTION_ITERDUMP_METHODDEF
    PYSQLITE_CONNECTION_LOAD_EXTENSION_METHODDEF
    PYSQLITE_CONNECTION_ROLLBACK_METHODDEF
    PYSQLITE_CONNECTION_SET_AUTHORIZER_METHODDEF
    PYSQLITE_CONNECTION_SET_PROGRESS_HANDLER_METHODDEF
    PYSQLITE_CONNECTION_SET_TRACE_CALLBACK_METHODDEF
    SETLIMIT_METHODDEF
    GETLIMIT_METHODDEF
    {NULL, NULL}
};

static struct PyMemberDef connection_members[] =
{
    {"Warning", T_OBJECT, offsetof(pysqlite_Connection, Warning), READONLY},
    {"Error", T_OBJECT, offsetof(pysqlite_Connection, Error), READONLY},
    {"InterfaceError", T_OBJECT, offsetof(pysqlite_Connection, InterfaceError), READONLY},
    {"DatabaseError", T_OBJECT, offsetof(pysqlite_Connection, DatabaseError), READONLY},
    {"DataError", T_OBJECT, offsetof(pysqlite_Connection, DataError), READONLY},
    {"OperationalError", T_OBJECT, offsetof(pysqlite_Connection, OperationalError), READONLY},
    {"IntegrityError", T_OBJECT, offsetof(pysqlite_Connection, IntegrityError), READONLY},
    {"InternalError", T_OBJECT, offsetof(pysqlite_Connection, InternalError), READONLY},
    {"ProgrammingError", T_OBJECT, offsetof(pysqlite_Connection, ProgrammingError), READONLY},
    {"NotSupportedError", T_OBJECT, offsetof(pysqlite_Connection, NotSupportedError), READONLY},
    {"row_factory", T_OBJECT, offsetof(pysqlite_Connection, row_factory)},
    {"text_factory", T_OBJECT, offsetof(pysqlite_Connection, text_factory)},
    {NULL}
};

static PyType_Slot connection_slots[] = {
    {Py_tp_dealloc, connection_dealloc},
    {Py_tp_doc, (void *)connection_doc},
    {Py_tp_methods, connection_methods},
    {Py_tp_members, connection_members},
    {Py_tp_getset, connection_getset},
    {Py_tp_init, pysqlite_connection_init},
    {Py_tp_call, pysqlite_connection_call},
    {Py_tp_traverse, connection_traverse},
    {Py_tp_clear, connection_clear},
    {0, NULL},
};

static PyType_Spec connection_spec = {
    .name = MODULE_NAME ".Connection",
    .basicsize = sizeof(pysqlite_Connection),
    .flags = (Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE |
              Py_TPFLAGS_HAVE_GC | Py_TPFLAGS_IMMUTABLETYPE),
    .slots = connection_slots,
};

int
pysqlite_connection_setup_types(PyObject *module)
{
    PyObject *type = PyType_FromModuleAndSpec(module, &connection_spec, NULL);
    if (type == NULL) {
        return -1;
    }
    pysqlite_state *state = pysqlite_get_state(module);
    state->ConnectionType = (PyTypeObject *)type;
    return 0;
}
