# pysqlite2/test/dbapi.py: tests for DB-API compliance
#
# Copyright (C) 2004-2010 Gerhard Häring <gh@ghaering.de>
#
# This file is part of pysqlite.
#
# This software is provided 'as-is', without any express or implied
# warranty.  In no event will the authors be held liable for any damages
# arising from the use of this software.
#
# Permission is granted to anyone to use this software for any purpose,
# including commercial applications, and to alter it and redistribute it
# freely, subject to the following restrictions:
#
# 1. The origin of this software must not be misrepresented; you must not
#    claim that you wrote the original software. If you use this software
#    in a product, an acknowledgment in the product documentation would be
#    appreciated but is not required.
# 2. Altered source versions must be plainly marked as such, and must not be
#    misrepresented as being the original software.
# 3. This notice may not be removed or altered from any source distribution.

import contextlib
import sqlite3 as sqlite
import subprocess
import sys
import threading
import unittest

from test.support import (
    SHORT_TIMEOUT,
    check_disallow_instantiation,
    threading_helper,
)
from test.support.os_helper import TESTFN, unlink, temp_dir


# Helper for tests using TESTFN
@contextlib.contextmanager
def managed_connect(*args, in_mem=False, **kwargs):
    cx = sqlite.connect(*args, **kwargs)
    try:
        yield cx
    finally:
        cx.close()
        if not in_mem:
            unlink(TESTFN)


# Helper for temporary memory databases
def memory_database(*args, **kwargs):
    cx = sqlite.connect(":memory:", *args, **kwargs)
    return contextlib.closing(cx)


# Temporarily limit a database connection parameter
@contextlib.contextmanager
def cx_limit(cx, category=sqlite.SQLITE_LIMIT_SQL_LENGTH, limit=128):
    try:
        _prev = cx.setlimit(category, limit)
        yield limit
    finally:
        cx.setlimit(category, _prev)


class ModuleTests(unittest.TestCase):
    def test_api_level(self):
        self.assertEqual(sqlite.apilevel, "2.0",
                         "apilevel is %s, should be 2.0" % sqlite.apilevel)

    def test_thread_safety(self):
        self.assertIn(sqlite.threadsafety, {0, 1, 3},
                      "threadsafety is %d, should be 0, 1 or 3" %
                      sqlite.threadsafety)

    def test_param_style(self):
        self.assertEqual(sqlite.paramstyle, "qmark",
                         "paramstyle is '%s', should be 'qmark'" %
                         sqlite.paramstyle)

    def test_warning(self):
        self.assertTrue(issubclass(sqlite.Warning, Exception),
                     "Warning is not a subclass of Exception")

    def test_error(self):
        self.assertTrue(issubclass(sqlite.Error, Exception),
                        "Error is not a subclass of Exception")

    def test_interface_error(self):
        self.assertTrue(issubclass(sqlite.InterfaceError, sqlite.Error),
                        "InterfaceError is not a subclass of Error")

    def test_database_error(self):
        self.assertTrue(issubclass(sqlite.DatabaseError, sqlite.Error),
                        "DatabaseError is not a subclass of Error")

    def test_data_error(self):
        self.assertTrue(issubclass(sqlite.DataError, sqlite.DatabaseError),
                        "DataError is not a subclass of DatabaseError")

    def test_operational_error(self):
        self.assertTrue(issubclass(sqlite.OperationalError, sqlite.DatabaseError),
                        "OperationalError is not a subclass of DatabaseError")

    def test_integrity_error(self):
        self.assertTrue(issubclass(sqlite.IntegrityError, sqlite.DatabaseError),
                        "IntegrityError is not a subclass of DatabaseError")

    def test_internal_error(self):
        self.assertTrue(issubclass(sqlite.InternalError, sqlite.DatabaseError),
                        "InternalError is not a subclass of DatabaseError")

    def test_programming_error(self):
        self.assertTrue(issubclass(sqlite.ProgrammingError, sqlite.DatabaseError),
                        "ProgrammingError is not a subclass of DatabaseError")

    def test_not_supported_error(self):
        self.assertTrue(issubclass(sqlite.NotSupportedError,
                                   sqlite.DatabaseError),
                        "NotSupportedError is not a subclass of DatabaseError")

    def test_module_constants(self):
        consts = [
            "SQLITE_ABORT",
            "SQLITE_ALTER_TABLE",
            "SQLITE_ANALYZE",
            "SQLITE_ATTACH",
            "SQLITE_AUTH",
            "SQLITE_BUSY",
            "SQLITE_CANTOPEN",
            "SQLITE_CONSTRAINT",
            "SQLITE_CORRUPT",
            "SQLITE_CREATE_INDEX",
            "SQLITE_CREATE_TABLE",
            "SQLITE_CREATE_TEMP_INDEX",
            "SQLITE_CREATE_TEMP_TABLE",
            "SQLITE_CREATE_TEMP_TRIGGER",
            "SQLITE_CREATE_TEMP_VIEW",
            "SQLITE_CREATE_TRIGGER",
            "SQLITE_CREATE_VIEW",
            "SQLITE_CREATE_VTABLE",
            "SQLITE_DELETE",
            "SQLITE_DENY",
            "SQLITE_DETACH",
            "SQLITE_DONE",
            "SQLITE_DROP_INDEX",
            "SQLITE_DROP_TABLE",
            "SQLITE_DROP_TEMP_INDEX",
            "SQLITE_DROP_TEMP_TABLE",
            "SQLITE_DROP_TEMP_TRIGGER",
            "SQLITE_DROP_TEMP_VIEW",
            "SQLITE_DROP_TRIGGER",
            "SQLITE_DROP_VIEW",
            "SQLITE_DROP_VTABLE",
            "SQLITE_EMPTY",
            "SQLITE_ERROR",
            "SQLITE_FORMAT",
            "SQLITE_FULL",
            "SQLITE_FUNCTION",
            "SQLITE_IGNORE",
            "SQLITE_INSERT",
            "SQLITE_INTERNAL",
            "SQLITE_INTERRUPT",
            "SQLITE_IOERR",
            "SQLITE_LOCKED",
            "SQLITE_MISMATCH",
            "SQLITE_MISUSE",
            "SQLITE_NOLFS",
            "SQLITE_NOMEM",
            "SQLITE_NOTADB",
            "SQLITE_NOTFOUND",
            "SQLITE_OK",
            "SQLITE_PERM",
            "SQLITE_PRAGMA",
            "SQLITE_PROTOCOL",
            "SQLITE_RANGE",
            "SQLITE_READ",
            "SQLITE_READONLY",
            "SQLITE_REINDEX",
            "SQLITE_ROW",
            "SQLITE_SAVEPOINT",
            "SQLITE_SCHEMA",
            "SQLITE_SELECT",
            "SQLITE_TOOBIG",
            "SQLITE_TRANSACTION",
            "SQLITE_UPDATE",
            # Run-time limit categories
            "SQLITE_LIMIT_LENGTH",
            "SQLITE_LIMIT_SQL_LENGTH",
            "SQLITE_LIMIT_COLUMN",
            "SQLITE_LIMIT_EXPR_DEPTH",
            "SQLITE_LIMIT_COMPOUND_SELECT",
            "SQLITE_LIMIT_VDBE_OP",
            "SQLITE_LIMIT_FUNCTION_ARG",
            "SQLITE_LIMIT_ATTACHED",
            "SQLITE_LIMIT_LIKE_PATTERN_LENGTH",
            "SQLITE_LIMIT_VARIABLE_NUMBER",
            "SQLITE_LIMIT_TRIGGER_DEPTH",
        ]
        if sqlite.sqlite_version_info >= (3, 7, 17):
            consts += ["SQLITE_NOTICE", "SQLITE_WARNING"]
        if sqlite.sqlite_version_info >= (3, 8, 3):
            consts.append("SQLITE_RECURSIVE")
        if sqlite.sqlite_version_info >= (3, 8, 7):
            consts.append("SQLITE_LIMIT_WORKER_THREADS")
        consts += ["PARSE_DECLTYPES", "PARSE_COLNAMES"]
        # Extended result codes
        consts += [
            "SQLITE_ABORT_ROLLBACK",
            "SQLITE_BUSY_RECOVERY",
            "SQLITE_CANTOPEN_FULLPATH",
            "SQLITE_CANTOPEN_ISDIR",
            "SQLITE_CANTOPEN_NOTEMPDIR",
            "SQLITE_CORRUPT_VTAB",
            "SQLITE_IOERR_ACCESS",
            "SQLITE_IOERR_BLOCKED",
            "SQLITE_IOERR_CHECKRESERVEDLOCK",
            "SQLITE_IOERR_CLOSE",
            "SQLITE_IOERR_DELETE",
            "SQLITE_IOERR_DELETE_NOENT",
            "SQLITE_IOERR_DIR_CLOSE",
            "SQLITE_IOERR_DIR_FSYNC",
            "SQLITE_IOERR_FSTAT",
            "SQLITE_IOERR_FSYNC",
            "SQLITE_IOERR_LOCK",
            "SQLITE_IOERR_NOMEM",
            "SQLITE_IOERR_RDLOCK",
            "SQLITE_IOERR_READ",
            "SQLITE_IOERR_SEEK",
            "SQLITE_IOERR_SHMLOCK",
            "SQLITE_IOERR_SHMMAP",
            "SQLITE_IOERR_SHMOPEN",
            "SQLITE_IOERR_SHMSIZE",
            "SQLITE_IOERR_SHORT_READ",
            "SQLITE_IOERR_TRUNCATE",
            "SQLITE_IOERR_UNLOCK",
            "SQLITE_IOERR_WRITE",
            "SQLITE_LOCKED_SHAREDCACHE",
            "SQLITE_READONLY_CANTLOCK",
            "SQLITE_READONLY_RECOVERY",
        ]
        if sqlite.version_info >= (3, 7, 16):
            consts += [
                "SQLITE_CONSTRAINT_CHECK",
                "SQLITE_CONSTRAINT_COMMITHOOK",
                "SQLITE_CONSTRAINT_FOREIGNKEY",
                "SQLITE_CONSTRAINT_FUNCTION",
                "SQLITE_CONSTRAINT_NOTNULL",
                "SQLITE_CONSTRAINT_PRIMARYKEY",
                "SQLITE_CONSTRAINT_TRIGGER",
                "SQLITE_CONSTRAINT_UNIQUE",
                "SQLITE_CONSTRAINT_VTAB",
                "SQLITE_READONLY_ROLLBACK",
            ]
        if sqlite.version_info >= (3, 7, 17):
            consts += [
                "SQLITE_IOERR_MMAP",
                "SQLITE_NOTICE_RECOVER_ROLLBACK",
                "SQLITE_NOTICE_RECOVER_WAL",
            ]
        if sqlite.version_info >= (3, 8, 0):
            consts += [
                "SQLITE_BUSY_SNAPSHOT",
                "SQLITE_IOERR_GETTEMPPATH",
                "SQLITE_WARNING_AUTOINDEX",
            ]
        if sqlite.version_info >= (3, 8, 1):
            consts += ["SQLITE_CANTOPEN_CONVPATH", "SQLITE_IOERR_CONVPATH"]
        if sqlite.version_info >= (3, 8, 2):
            consts.append("SQLITE_CONSTRAINT_ROWID")
        if sqlite.version_info >= (3, 8, 3):
            consts.append("SQLITE_READONLY_DBMOVED")
        if sqlite.version_info >= (3, 8, 7):
            consts.append("SQLITE_AUTH_USER")
        if sqlite.version_info >= (3, 9, 0):
            consts.append("SQLITE_IOERR_VNODE")
        if sqlite.version_info >= (3, 10, 0):
            consts.append("SQLITE_IOERR_AUTH")
        if sqlite.version_info >= (3, 14, 1):
            consts.append("SQLITE_OK_LOAD_PERMANENTLY")
        if sqlite.version_info >= (3, 21, 0):
            consts += [
                "SQLITE_IOERR_BEGIN_ATOMIC",
                "SQLITE_IOERR_COMMIT_ATOMIC",
                "SQLITE_IOERR_ROLLBACK_ATOMIC",
            ]
        if sqlite.version_info >= (3, 22, 0):
            consts += [
                "SQLITE_ERROR_MISSING_COLLSEQ",
                "SQLITE_ERROR_RETRY",
                "SQLITE_READONLY_CANTINIT",
                "SQLITE_READONLY_DIRECTORY",
            ]
        if sqlite.version_info >= (3, 24, 0):
            consts += ["SQLITE_CORRUPT_SEQUENCE", "SQLITE_LOCKED_VTAB"]
        if sqlite.version_info >= (3, 25, 0):
            consts += ["SQLITE_CANTOPEN_DIRTYWAL", "SQLITE_ERROR_SNAPSHOT"]
        if sqlite.version_info >= (3, 31, 0):
            consts += [
                "SQLITE_CANTOPEN_SYMLINK",
                "SQLITE_CONSTRAINT_PINNED",
                "SQLITE_OK_SYMLINK",
            ]
        if sqlite.version_info >= (3, 32, 0):
            consts += [
                "SQLITE_BUSY_TIMEOUT",
                "SQLITE_CORRUPT_INDEX",
                "SQLITE_IOERR_DATA",
            ]
        if sqlite.version_info >= (3, 34, 0):
            const.append("SQLITE_IOERR_CORRUPTFS")
        for const in consts:
            with self.subTest(const=const):
                self.assertTrue(hasattr(sqlite, const))

    def test_error_code_on_exception(self):
        err_msg = "unable to open database file"
        if sys.platform.startswith("win"):
            err_code = sqlite.SQLITE_CANTOPEN_ISDIR
        else:
            err_code = sqlite.SQLITE_CANTOPEN

        with temp_dir() as db:
            with self.assertRaisesRegex(sqlite.Error, err_msg) as cm:
                sqlite.connect(db)
            e = cm.exception
            self.assertEqual(e.sqlite_errorcode, err_code)
            self.assertTrue(e.sqlite_errorname.startswith("SQLITE_CANTOPEN"))

    @unittest.skipIf(sqlite.sqlite_version_info <= (3, 7, 16),
                     "Requires SQLite 3.7.16 or newer")
    def test_extended_error_code_on_exception(self):
        with managed_connect(":memory:", in_mem=True) as con:
            with con:
                con.execute("create table t(t integer check(t > 0))")
            errmsg = "constraint failed"
            with self.assertRaisesRegex(sqlite.IntegrityError, errmsg) as cm:
                con.execute("insert into t values(-1)")
            exc = cm.exception
            self.assertEqual(exc.sqlite_errorcode,
                             sqlite.SQLITE_CONSTRAINT_CHECK)
            self.assertEqual(exc.sqlite_errorname, "SQLITE_CONSTRAINT_CHECK")

    # sqlite3_enable_shared_cache() is deprecated on macOS and calling it may raise
    # OperationalError on some buildbots.
    @unittest.skipIf(sys.platform == "darwin", "shared cache is deprecated on macOS")
    def test_shared_cache_deprecated(self):
        for enable in (True, False):
            with self.assertWarns(DeprecationWarning) as cm:
                sqlite.enable_shared_cache(enable)
            self.assertIn("dbapi.py", cm.filename)

    def test_disallow_instantiation(self):
        cx = sqlite.connect(":memory:")
        check_disallow_instantiation(self, type(cx("select 1")))

    def test_complete_statement(self):
        self.assertFalse(sqlite.complete_statement("select t"))
        self.assertTrue(sqlite.complete_statement("create table t(t);"))


class ConnectionTests(unittest.TestCase):

    def setUp(self):
        self.cx = sqlite.connect(":memory:")
        cu = self.cx.cursor()
        cu.execute("create table test(id integer primary key, name text)")
        cu.execute("insert into test(name) values (?)", ("foo",))

    def tearDown(self):
        self.cx.close()

    def test_commit(self):
        self.cx.commit()

    def test_commit_after_no_changes(self):
        """
        A commit should also work when no changes were made to the database.
        """
        self.cx.commit()
        self.cx.commit()

    def test_rollback(self):
        self.cx.rollback()

    def test_rollback_after_no_changes(self):
        """
        A rollback should also work when no changes were made to the database.
        """
        self.cx.rollback()
        self.cx.rollback()

    def test_cursor(self):
        cu = self.cx.cursor()

    def test_failed_open(self):
        YOU_CANNOT_OPEN_THIS = "/foo/bar/bla/23534/mydb.db"
        with self.assertRaises(sqlite.OperationalError):
            con = sqlite.connect(YOU_CANNOT_OPEN_THIS)

    def test_close(self):
        self.cx.close()

    def test_use_after_close(self):
        sql = "select 1"
        cu = self.cx.cursor()
        res = cu.execute(sql)
        self.cx.close()
        self.assertRaises(sqlite.ProgrammingError, res.fetchall)
        self.assertRaises(sqlite.ProgrammingError, cu.execute, sql)
        self.assertRaises(sqlite.ProgrammingError, cu.executemany, sql, [])
        self.assertRaises(sqlite.ProgrammingError, cu.executescript, sql)
        self.assertRaises(sqlite.ProgrammingError, self.cx.execute, sql)
        self.assertRaises(sqlite.ProgrammingError,
                          self.cx.executemany, sql, [])
        self.assertRaises(sqlite.ProgrammingError, self.cx.executescript, sql)
        self.assertRaises(sqlite.ProgrammingError,
                          self.cx.create_function, "t", 1, lambda x: x)
        self.assertRaises(sqlite.ProgrammingError, self.cx.cursor)
        with self.assertRaises(sqlite.ProgrammingError):
            with self.cx:
                pass

    def test_exceptions(self):
        # Optional DB-API extension.
        self.assertEqual(self.cx.Warning, sqlite.Warning)
        self.assertEqual(self.cx.Error, sqlite.Error)
        self.assertEqual(self.cx.InterfaceError, sqlite.InterfaceError)
        self.assertEqual(self.cx.DatabaseError, sqlite.DatabaseError)
        self.assertEqual(self.cx.DataError, sqlite.DataError)
        self.assertEqual(self.cx.OperationalError, sqlite.OperationalError)
        self.assertEqual(self.cx.IntegrityError, sqlite.IntegrityError)
        self.assertEqual(self.cx.InternalError, sqlite.InternalError)
        self.assertEqual(self.cx.ProgrammingError, sqlite.ProgrammingError)
        self.assertEqual(self.cx.NotSupportedError, sqlite.NotSupportedError)

    def test_in_transaction(self):
        # Can't use db from setUp because we want to test initial state.
        cx = sqlite.connect(":memory:")
        cu = cx.cursor()
        self.assertEqual(cx.in_transaction, False)
        cu.execute("create table transactiontest(id integer primary key, name text)")
        self.assertEqual(cx.in_transaction, False)
        cu.execute("insert into transactiontest(name) values (?)", ("foo",))
        self.assertEqual(cx.in_transaction, True)
        cu.execute("select name from transactiontest where name=?", ["foo"])
        row = cu.fetchone()
        self.assertEqual(cx.in_transaction, True)
        cx.commit()
        self.assertEqual(cx.in_transaction, False)
        cu.execute("select name from transactiontest where name=?", ["foo"])
        row = cu.fetchone()
        self.assertEqual(cx.in_transaction, False)

    def test_in_transaction_ro(self):
        with self.assertRaises(AttributeError):
            self.cx.in_transaction = True

    def test_connection_exceptions(self):
        exceptions = [
            "DataError",
            "DatabaseError",
            "Error",
            "IntegrityError",
            "InterfaceError",
            "NotSupportedError",
            "OperationalError",
            "ProgrammingError",
            "Warning",
        ]
        for exc in exceptions:
            with self.subTest(exc=exc):
                self.assertTrue(hasattr(self.cx, exc))
                self.assertIs(getattr(sqlite, exc), getattr(self.cx, exc))

    def test_interrupt_on_closed_db(self):
        cx = sqlite.connect(":memory:")
        cx.close()
        with self.assertRaises(sqlite.ProgrammingError):
            cx.interrupt()

    def test_interrupt(self):
        self.assertIsNone(self.cx.interrupt())

    def test_drop_unused_refs(self):
        for n in range(500):
            cu = self.cx.execute(f"select {n}")
            self.assertEqual(cu.fetchone()[0], n)

    def test_connection_limits(self):
        category = sqlite.SQLITE_LIMIT_SQL_LENGTH
        saved_limit = self.cx.getlimit(category)
        try:
            new_limit = 10
            prev_limit = self.cx.setlimit(category, new_limit)
            self.assertEqual(saved_limit, prev_limit)
            self.assertEqual(self.cx.getlimit(category), new_limit)
            msg = "query string is too large"
            self.assertRaisesRegex(sqlite.DataError, msg,
                                   self.cx.execute, "select 1 as '16'")
        finally:  # restore saved limit
            self.cx.setlimit(category, saved_limit)

    def test_connection_bad_limit_category(self):
        msg = "'category' is out of bounds"
        cat = 1111
        self.assertRaisesRegex(sqlite.ProgrammingError, msg,
                               self.cx.getlimit, cat)
        self.assertRaisesRegex(sqlite.ProgrammingError, msg,
                               self.cx.setlimit, cat, 0)

    def test_connection_init_bad_isolation_level(self):
        msg = (
            "isolation_level string must be '', 'DEFERRED', 'IMMEDIATE', or "
            "'EXCLUSIVE'"
        )
        with self.assertRaisesRegex(ValueError, msg):
            memory_database(isolation_level="BOGUS")

    def test_connection_init_good_isolation_levels(self):
        for level in ("", "DEFERRED", "IMMEDIATE", "EXCLUSIVE", None):
            with self.subTest(level=level):
                with memory_database(isolation_level=level) as cx:
                    cx.execute("select 'ok'")


class UninitialisedConnectionTests(unittest.TestCase):
    def setUp(self):
        self.cx = sqlite.Connection.__new__(sqlite.Connection)

    def test_uninit_operations(self):
        funcs = (
            lambda: self.cx.isolation_level,
            lambda: self.cx.total_changes,
            lambda: self.cx.in_transaction,
            lambda: self.cx.iterdump(),
            lambda: self.cx.cursor(),
            lambda: self.cx.close(),
        )
        for func in funcs:
            with self.subTest(func=func):
                self.assertRaisesRegex(sqlite.ProgrammingError,
                                       "Base Connection.__init__ not called",
                                       func)


class OpenTests(unittest.TestCase):
    _sql = "create table test(id integer)"

    def test_open_with_path_like_object(self):
        """ Checks that we can successfully connect to a database using an object that
            is PathLike, i.e. has __fspath__(). """
        class Path:
            def __fspath__(self):
                return TESTFN
        path = Path()
        with managed_connect(path) as cx:
            cx.execute(self._sql)

    def test_open_uri(self):
        with managed_connect(TESTFN) as cx:
            cx.execute(self._sql)
        with managed_connect(f"file:{TESTFN}", uri=True) as cx:
            cx.execute(self._sql)
        with self.assertRaises(sqlite.OperationalError):
            with managed_connect(f"file:{TESTFN}?mode=ro", uri=True) as cx:
                cx.execute(self._sql)

    def test_database_keyword(self):
        with sqlite.connect(database=":memory:") as cx:
            self.assertEqual(type(cx), sqlite.Connection)


class CursorTests(unittest.TestCase):
    def setUp(self):
        self.cx = sqlite.connect(":memory:")
        self.cu = self.cx.cursor()
        self.cu.execute(
            "create table test(id integer primary key, name text, "
            "income number, unique_test text unique)"
        )
        self.cu.execute("insert into test(name) values (?)", ("foo",))

    def tearDown(self):
        self.cu.close()
        self.cx.close()

    def test_execute_no_args(self):
        self.cu.execute("delete from test")

    def test_execute_illegal_sql(self):
        with self.assertRaises(sqlite.OperationalError):
            self.cu.execute("select asdf")

    def test_execute_too_much_sql(self):
        with self.assertRaises(sqlite.Warning):
            self.cu.execute("select 5+4; select 4+5")

    def test_execute_too_much_sql2(self):
        self.cu.execute("select 5+4; -- foo bar")

    def test_execute_too_much_sql3(self):
        self.cu.execute("""
            select 5+4;

            /*
            foo
            */
            """)

    def test_execute_wrong_sql_arg(self):
        with self.assertRaises(TypeError):
            self.cu.execute(42)

    def test_execute_arg_int(self):
        self.cu.execute("insert into test(id) values (?)", (42,))

    def test_execute_arg_float(self):
        self.cu.execute("insert into test(income) values (?)", (2500.32,))

    def test_execute_arg_string(self):
        self.cu.execute("insert into test(name) values (?)", ("Hugo",))

    def test_execute_arg_string_with_zero_byte(self):
        self.cu.execute("insert into test(name) values (?)", ("Hu\x00go",))

        self.cu.execute("select name from test where id=?", (self.cu.lastrowid,))
        row = self.cu.fetchone()
        self.assertEqual(row[0], "Hu\x00go")

    def test_execute_non_iterable(self):
        with self.assertRaises(ValueError) as cm:
            self.cu.execute("insert into test(id) values (?)", 42)
        self.assertEqual(str(cm.exception), 'parameters are of unsupported type')

    def test_execute_wrong_no_of_args1(self):
        # too many parameters
        with self.assertRaises(sqlite.ProgrammingError):
            self.cu.execute("insert into test(id) values (?)", (17, "Egon"))

    def test_execute_wrong_no_of_args2(self):
        # too little parameters
        with self.assertRaises(sqlite.ProgrammingError):
            self.cu.execute("insert into test(id) values (?)")

    def test_execute_wrong_no_of_args3(self):
        # no parameters, parameters are needed
        with self.assertRaises(sqlite.ProgrammingError):
            self.cu.execute("insert into test(id) values (?)")

    def test_execute_param_list(self):
        self.cu.execute("insert into test(name) values ('foo')")
        self.cu.execute("select name from test where name=?", ["foo"])
        row = self.cu.fetchone()
        self.assertEqual(row[0], "foo")

    def test_execute_param_sequence(self):
        class L:
            def __len__(self):
                return 1
            def __getitem__(self, x):
                assert x == 0
                return "foo"

        self.cu.execute("insert into test(name) values ('foo')")
        self.cu.execute("select name from test where name=?", L())
        row = self.cu.fetchone()
        self.assertEqual(row[0], "foo")

    def test_execute_param_sequence_bad_len(self):
        # Issue41662: Error in __len__() was overridden with ProgrammingError.
        class L:
            def __len__(self):
                1/0
            def __getitem__(slf, x):
                raise AssertionError

        self.cu.execute("insert into test(name) values ('foo')")
        with self.assertRaises(ZeroDivisionError):
            self.cu.execute("select name from test where name=?", L())

    def test_execute_too_many_params(self):
        category = sqlite.SQLITE_LIMIT_VARIABLE_NUMBER
        msg = "too many SQL variables"
        with cx_limit(self.cx, category=category, limit=1):
            self.cu.execute("select * from test where id=?", (1,))
            with self.assertRaisesRegex(sqlite.OperationalError, msg):
                self.cu.execute("select * from test where id!=? and id!=?",
                                (1, 2))

    def test_execute_dict_mapping(self):
        self.cu.execute("insert into test(name) values ('foo')")
        self.cu.execute("select name from test where name=:name", {"name": "foo"})
        row = self.cu.fetchone()
        self.assertEqual(row[0], "foo")

    def test_execute_dict_mapping_mapping(self):
        class D(dict):
            def __missing__(self, key):
                return "foo"

        self.cu.execute("insert into test(name) values ('foo')")
        self.cu.execute("select name from test where name=:name", D())
        row = self.cu.fetchone()
        self.assertEqual(row[0], "foo")

    def test_execute_dict_mapping_too_little_args(self):
        self.cu.execute("insert into test(name) values ('foo')")
        with self.assertRaises(sqlite.ProgrammingError):
            self.cu.execute("select name from test where name=:name and id=:id", {"name": "foo"})

    def test_execute_dict_mapping_no_args(self):
        self.cu.execute("insert into test(name) values ('foo')")
        with self.assertRaises(sqlite.ProgrammingError):
            self.cu.execute("select name from test where name=:name")

    def test_execute_dict_mapping_unnamed(self):
        self.cu.execute("insert into test(name) values ('foo')")
        with self.assertRaises(sqlite.ProgrammingError):
            self.cu.execute("select name from test where name=?", {"name": "foo"})

    def test_close(self):
        self.cu.close()

    def test_rowcount_execute(self):
        self.cu.execute("delete from test")
        self.cu.execute("insert into test(name) values ('foo')")
        self.cu.execute("insert into test(name) values ('foo')")
        self.cu.execute("update test set name='bar'")
        self.assertEqual(self.cu.rowcount, 2)

    def test_rowcount_select(self):
        """
        pysqlite does not know the rowcount of SELECT statements, because we
        don't fetch all rows after executing the select statement. The rowcount
        has thus to be -1.
        """
        self.cu.execute("select 5 union select 6")
        self.assertEqual(self.cu.rowcount, -1)

    def test_rowcount_executemany(self):
        self.cu.execute("delete from test")
        self.cu.executemany("insert into test(name) values (?)", [(1,), (2,), (3,)])
        self.assertEqual(self.cu.rowcount, 3)

    def test_total_changes(self):
        self.cu.execute("insert into test(name) values ('foo')")
        self.cu.execute("insert into test(name) values ('foo')")
        self.assertLess(2, self.cx.total_changes, msg='total changes reported wrong value')

    # Checks for executemany:
    # Sequences are required by the DB-API, iterators
    # enhancements in pysqlite.

    def test_execute_many_sequence(self):
        self.cu.executemany("insert into test(income) values (?)", [(x,) for x in range(100, 110)])

    def test_execute_many_iterator(self):
        class MyIter:
            def __init__(self):
                self.value = 5

            def __iter__(self):
                return self

            def __next__(self):
                if self.value == 10:
                    raise StopIteration
                else:
                    self.value += 1
                    return (self.value,)

        self.cu.executemany("insert into test(income) values (?)", MyIter())

    def test_execute_many_generator(self):
        def mygen():
            for i in range(5):
                yield (i,)

        self.cu.executemany("insert into test(income) values (?)", mygen())

    def test_execute_many_wrong_sql_arg(self):
        with self.assertRaises(TypeError):
            self.cu.executemany(42, [(3,)])

    def test_execute_many_select(self):
        with self.assertRaises(sqlite.ProgrammingError):
            self.cu.executemany("select ?", [(3,)])

    def test_execute_many_not_iterable(self):
        with self.assertRaises(TypeError):
            self.cu.executemany("insert into test(income) values (?)", 42)

    def test_fetch_iter(self):
        # Optional DB-API extension.
        self.cu.execute("delete from test")
        self.cu.execute("insert into test(id) values (?)", (5,))
        self.cu.execute("insert into test(id) values (?)", (6,))
        self.cu.execute("select id from test order by id")
        lst = []
        for row in self.cu:
            lst.append(row[0])
        self.assertEqual(lst[0], 5)
        self.assertEqual(lst[1], 6)

    def test_fetchone(self):
        self.cu.execute("select name from test")
        row = self.cu.fetchone()
        self.assertEqual(row[0], "foo")
        row = self.cu.fetchone()
        self.assertEqual(row, None)

    def test_fetchone_no_statement(self):
        cur = self.cx.cursor()
        row = cur.fetchone()
        self.assertEqual(row, None)

    def test_array_size(self):
        # must default to 1
        self.assertEqual(self.cu.arraysize, 1)

        # now set to 2
        self.cu.arraysize = 2

        # now make the query return 3 rows
        self.cu.execute("delete from test")
        self.cu.execute("insert into test(name) values ('A')")
        self.cu.execute("insert into test(name) values ('B')")
        self.cu.execute("insert into test(name) values ('C')")
        self.cu.execute("select name from test")
        res = self.cu.fetchmany()

        self.assertEqual(len(res), 2)

    def test_fetchmany(self):
        self.cu.execute("select name from test")
        res = self.cu.fetchmany(100)
        self.assertEqual(len(res), 1)
        res = self.cu.fetchmany(100)
        self.assertEqual(res, [])

    def test_fetchmany_kw_arg(self):
        """Checks if fetchmany works with keyword arguments"""
        self.cu.execute("select name from test")
        res = self.cu.fetchmany(size=100)
        self.assertEqual(len(res), 1)

    def test_fetchall(self):
        self.cu.execute("select name from test")
        res = self.cu.fetchall()
        self.assertEqual(len(res), 1)
        res = self.cu.fetchall()
        self.assertEqual(res, [])

    def test_setinputsizes(self):
        self.cu.setinputsizes([3, 4, 5])

    def test_setoutputsize(self):
        self.cu.setoutputsize(5, 0)

    def test_setoutputsize_no_column(self):
        self.cu.setoutputsize(42)

    def test_cursor_connection(self):
        # Optional DB-API extension.
        self.assertEqual(self.cu.connection, self.cx)

    def test_wrong_cursor_callable(self):
        with self.assertRaises(TypeError):
            def f(): pass
            cur = self.cx.cursor(f)

    def test_cursor_wrong_class(self):
        class Foo: pass
        foo = Foo()
        with self.assertRaises(TypeError):
            cur = sqlite.Cursor(foo)

    def test_last_row_id_on_replace(self):
        """
        INSERT OR REPLACE and REPLACE INTO should produce the same behavior.
        """
        sql = '{} INTO test(id, unique_test) VALUES (?, ?)'
        for statement in ('INSERT OR REPLACE', 'REPLACE'):
            with self.subTest(statement=statement):
                self.cu.execute(sql.format(statement), (1, 'foo'))
                self.assertEqual(self.cu.lastrowid, 1)

    def test_last_row_id_on_ignore(self):
        self.cu.execute(
            "insert or ignore into test(unique_test) values (?)",
            ('test',))
        self.assertEqual(self.cu.lastrowid, 2)
        self.cu.execute(
            "insert or ignore into test(unique_test) values (?)",
            ('test',))
        self.assertEqual(self.cu.lastrowid, 2)

    def test_last_row_id_insert_o_r(self):
        results = []
        for statement in ('FAIL', 'ABORT', 'ROLLBACK'):
            sql = 'INSERT OR {} INTO test(unique_test) VALUES (?)'
            with self.subTest(statement='INSERT OR {}'.format(statement)):
                self.cu.execute(sql.format(statement), (statement,))
                results.append((statement, self.cu.lastrowid))
                with self.assertRaises(sqlite.IntegrityError):
                    self.cu.execute(sql.format(statement), (statement,))
                results.append((statement, self.cu.lastrowid))
        expected = [
            ('FAIL', 2), ('FAIL', 2),
            ('ABORT', 3), ('ABORT', 3),
            ('ROLLBACK', 4), ('ROLLBACK', 4),
        ]
        self.assertEqual(results, expected)

    def test_column_count(self):
        # Check that column count is updated correctly for cached statements
        select = "select * from test"
        res = self.cu.execute(select)
        old_count = len(res.description)
        # Add a new column and execute the cached select query again
        self.cu.execute("alter table test add newcol")
        res = self.cu.execute(select)
        new_count = len(res.description)
        self.assertEqual(new_count - old_count, 1)

    def test_same_query_in_multiple_cursors(self):
        cursors = [self.cx.execute("select 1") for _ in range(3)]
        for cu in cursors:
            self.assertEqual(cu.fetchall(), [(1,)])


class ThreadTests(unittest.TestCase):
    def setUp(self):
        self.con = sqlite.connect(":memory:")
        self.cur = self.con.cursor()
        self.cur.execute("create table test(name text)")

    def tearDown(self):
        self.cur.close()
        self.con.close()

    @threading_helper.reap_threads
    def _run_test(self, fn, *args, **kwds):
        def run(err):
            try:
                fn(*args, **kwds)
                err.append("did not raise ProgrammingError")
            except sqlite.ProgrammingError:
                pass
            except:
                err.append("raised wrong exception")

        err = []
        t = threading.Thread(target=run, kwargs={"err": err})
        t.start()
        t.join()
        if err:
            self.fail("\n".join(err))

    def test_check_connection_thread(self):
        fns = [
            lambda: self.con.cursor(),
            lambda: self.con.commit(),
            lambda: self.con.rollback(),
            lambda: self.con.close(),
            lambda: self.con.set_trace_callback(None),
            lambda: self.con.set_authorizer(None),
            lambda: self.con.create_collation("foo", None),
            lambda: self.con.setlimit(sqlite.SQLITE_LIMIT_LENGTH, -1),
            lambda: self.con.getlimit(sqlite.SQLITE_LIMIT_LENGTH),
        ]
        for fn in fns:
            with self.subTest(fn=fn):
                self._run_test(fn)

    def test_check_cursor_thread(self):
        fns = [
            lambda: self.cur.execute("insert into test(name) values('a')"),
            lambda: self.cur.close(),
            lambda: self.cur.execute("select name from test"),
            lambda: self.cur.fetchone(),
        ]
        for fn in fns:
            with self.subTest(fn=fn):
                self._run_test(fn)


    @threading_helper.reap_threads
    def test_dont_check_same_thread(self):
        def run(con, err):
            try:
                con.execute("select 1")
            except sqlite.Error:
                err.append("multi-threading not allowed")

        con = sqlite.connect(":memory:", check_same_thread=False)
        err = []
        t = threading.Thread(target=run, kwargs={"con": con, "err": err})
        t.start()
        t.join()
        self.assertEqual(len(err), 0, "\n".join(err))


class ConstructorTests(unittest.TestCase):
    def test_date(self):
        d = sqlite.Date(2004, 10, 28)

    def test_time(self):
        t = sqlite.Time(12, 39, 35)

    def test_timestamp(self):
        ts = sqlite.Timestamp(2004, 10, 28, 12, 39, 35)

    def test_date_from_ticks(self):
        d = sqlite.DateFromTicks(42)

    def test_time_from_ticks(self):
        t = sqlite.TimeFromTicks(42)

    def test_timestamp_from_ticks(self):
        ts = sqlite.TimestampFromTicks(42)

    def test_binary(self):
        b = sqlite.Binary(b"\0'")

class ExtensionTests(unittest.TestCase):
    def test_script_string_sql(self):
        con = sqlite.connect(":memory:")
        cur = con.cursor()
        cur.executescript("""
            -- bla bla
            /* a stupid comment */
            create table a(i);
            insert into a(i) values (5);
            """)
        cur.execute("select i from a")
        res = cur.fetchone()[0]
        self.assertEqual(res, 5)

    def test_script_syntax_error(self):
        con = sqlite.connect(":memory:")
        cur = con.cursor()
        with self.assertRaises(sqlite.OperationalError):
            cur.executescript("create table test(x); asdf; create table test2(x)")

    def test_script_error_normal(self):
        con = sqlite.connect(":memory:")
        cur = con.cursor()
        with self.assertRaises(sqlite.OperationalError):
            cur.executescript("create table test(sadfsadfdsa); select foo from hurz;")

    def test_cursor_executescript_as_bytes(self):
        con = sqlite.connect(":memory:")
        cur = con.cursor()
        with self.assertRaises(TypeError):
            cur.executescript(b"create table test(foo); insert into test(foo) values (5);")

    def test_cursor_executescript_with_null_characters(self):
        con = sqlite.connect(":memory:")
        cur = con.cursor()
        with self.assertRaises(ValueError):
            cur.executescript("""
                create table a(i);\0
                insert into a(i) values (5);
                """)

    def test_cursor_executescript_with_surrogates(self):
        con = sqlite.connect(":memory:")
        cur = con.cursor()
        with self.assertRaises(UnicodeEncodeError):
            cur.executescript("""
                create table a(s);
                insert into a(s) values ('\ud8ff');
                """)

    def test_cursor_executescript_too_large_script(self):
        msg = "query string is too large"
        with memory_database() as cx, cx_limit(cx) as lim:
            cx.executescript("select 'almost too large'".ljust(lim))
            with self.assertRaisesRegex(sqlite.DataError, msg):
                cx.executescript("select 'too large'".ljust(lim+1))

    def test_cursor_executescript_tx_control(self):
        con = sqlite.connect(":memory:")
        con.execute("begin")
        self.assertTrue(con.in_transaction)
        con.executescript("select 1")
        self.assertFalse(con.in_transaction)

    def test_connection_execute(self):
        con = sqlite.connect(":memory:")
        result = con.execute("select 5").fetchone()[0]
        self.assertEqual(result, 5, "Basic test of Connection.execute")

    def test_connection_executemany(self):
        con = sqlite.connect(":memory:")
        con.execute("create table test(foo)")
        con.executemany("insert into test(foo) values (?)", [(3,), (4,)])
        result = con.execute("select foo from test order by foo").fetchall()
        self.assertEqual(result[0][0], 3, "Basic test of Connection.executemany")
        self.assertEqual(result[1][0], 4, "Basic test of Connection.executemany")

    def test_connection_executescript(self):
        con = sqlite.connect(":memory:")
        con.executescript("create table test(foo); insert into test(foo) values (5);")
        result = con.execute("select foo from test").fetchone()[0]
        self.assertEqual(result, 5, "Basic test of Connection.executescript")

class ClosedConTests(unittest.TestCase):
    def test_closed_con_cursor(self):
        con = sqlite.connect(":memory:")
        con.close()
        with self.assertRaises(sqlite.ProgrammingError):
            cur = con.cursor()

    def test_closed_con_commit(self):
        con = sqlite.connect(":memory:")
        con.close()
        with self.assertRaises(sqlite.ProgrammingError):
            con.commit()

    def test_closed_con_rollback(self):
        con = sqlite.connect(":memory:")
        con.close()
        with self.assertRaises(sqlite.ProgrammingError):
            con.rollback()

    def test_closed_cur_execute(self):
        con = sqlite.connect(":memory:")
        cur = con.cursor()
        con.close()
        with self.assertRaises(sqlite.ProgrammingError):
            cur.execute("select 4")

    def test_closed_create_function(self):
        con = sqlite.connect(":memory:")
        con.close()
        def f(x): return 17
        with self.assertRaises(sqlite.ProgrammingError):
            con.create_function("foo", 1, f)

    def test_closed_create_aggregate(self):
        con = sqlite.connect(":memory:")
        con.close()
        class Agg:
            def __init__(self):
                pass
            def step(self, x):
                pass
            def finalize(self):
                return 17
        with self.assertRaises(sqlite.ProgrammingError):
            con.create_aggregate("foo", 1, Agg)

    def test_closed_set_authorizer(self):
        con = sqlite.connect(":memory:")
        con.close()
        def authorizer(*args):
            return sqlite.DENY
        with self.assertRaises(sqlite.ProgrammingError):
            con.set_authorizer(authorizer)

    def test_closed_set_progress_callback(self):
        con = sqlite.connect(":memory:")
        con.close()
        def progress(): pass
        with self.assertRaises(sqlite.ProgrammingError):
            con.set_progress_handler(progress, 100)

    def test_closed_call(self):
        con = sqlite.connect(":memory:")
        con.close()
        with self.assertRaises(sqlite.ProgrammingError):
            con()

class ClosedCurTests(unittest.TestCase):
    def test_closed(self):
        con = sqlite.connect(":memory:")
        cur = con.cursor()
        cur.close()

        for method_name in ("execute", "executemany", "executescript", "fetchall", "fetchmany", "fetchone"):
            if method_name in ("execute", "executescript"):
                params = ("select 4 union select 5",)
            elif method_name == "executemany":
                params = ("insert into foo(bar) values (?)", [(3,), (4,)])
            else:
                params = []

            with self.assertRaises(sqlite.ProgrammingError):
                method = getattr(cur, method_name)
                method(*params)


class SqliteOnConflictTests(unittest.TestCase):
    """
    Tests for SQLite's "insert on conflict" feature.

    See https://www.sqlite.org/lang_conflict.html for details.
    """

    def setUp(self):
        self.cx = sqlite.connect(":memory:")
        self.cu = self.cx.cursor()
        self.cu.execute("""
          CREATE TABLE test(
            id INTEGER PRIMARY KEY, name TEXT, unique_name TEXT UNIQUE
          );
        """)

    def tearDown(self):
        self.cu.close()
        self.cx.close()

    def test_on_conflict_rollback_with_explicit_transaction(self):
        self.cx.isolation_level = None  # autocommit mode
        self.cu = self.cx.cursor()
        # Start an explicit transaction.
        self.cu.execute("BEGIN")
        self.cu.execute("INSERT INTO test(name) VALUES ('abort_test')")
        self.cu.execute("INSERT OR ROLLBACK INTO test(unique_name) VALUES ('foo')")
        with self.assertRaises(sqlite.IntegrityError):
            self.cu.execute("INSERT OR ROLLBACK INTO test(unique_name) VALUES ('foo')")
        # Use connection to commit.
        self.cx.commit()
        self.cu.execute("SELECT name, unique_name from test")
        # Transaction should have rolled back and nothing should be in table.
        self.assertEqual(self.cu.fetchall(), [])

    def test_on_conflict_abort_raises_with_explicit_transactions(self):
        # Abort cancels the current sql statement but doesn't change anything
        # about the current transaction.
        self.cx.isolation_level = None  # autocommit mode
        self.cu = self.cx.cursor()
        # Start an explicit transaction.
        self.cu.execute("BEGIN")
        self.cu.execute("INSERT INTO test(name) VALUES ('abort_test')")
        self.cu.execute("INSERT OR ABORT INTO test(unique_name) VALUES ('foo')")
        with self.assertRaises(sqlite.IntegrityError):
            self.cu.execute("INSERT OR ABORT INTO test(unique_name) VALUES ('foo')")
        self.cx.commit()
        self.cu.execute("SELECT name, unique_name FROM test")
        # Expect the first two inserts to work, third to do nothing.
        self.assertEqual(self.cu.fetchall(), [('abort_test', None), (None, 'foo',)])

    def test_on_conflict_rollback_without_transaction(self):
        # Start of implicit transaction
        self.cu.execute("INSERT INTO test(name) VALUES ('abort_test')")
        self.cu.execute("INSERT OR ROLLBACK INTO test(unique_name) VALUES ('foo')")
        with self.assertRaises(sqlite.IntegrityError):
            self.cu.execute("INSERT OR ROLLBACK INTO test(unique_name) VALUES ('foo')")
        self.cu.execute("SELECT name, unique_name FROM test")
        # Implicit transaction is rolled back on error.
        self.assertEqual(self.cu.fetchall(), [])

    def test_on_conflict_abort_raises_without_transactions(self):
        # Abort cancels the current sql statement but doesn't change anything
        # about the current transaction.
        self.cu.execute("INSERT INTO test(name) VALUES ('abort_test')")
        self.cu.execute("INSERT OR ABORT INTO test(unique_name) VALUES ('foo')")
        with self.assertRaises(sqlite.IntegrityError):
            self.cu.execute("INSERT OR ABORT INTO test(unique_name) VALUES ('foo')")
        # Make sure all other values were inserted.
        self.cu.execute("SELECT name, unique_name FROM test")
        self.assertEqual(self.cu.fetchall(), [('abort_test', None), (None, 'foo',)])

    def test_on_conflict_fail(self):
        self.cu.execute("INSERT OR FAIL INTO test(unique_name) VALUES ('foo')")
        with self.assertRaises(sqlite.IntegrityError):
            self.cu.execute("INSERT OR FAIL INTO test(unique_name) VALUES ('foo')")
        self.assertEqual(self.cu.fetchall(), [])

    def test_on_conflict_ignore(self):
        self.cu.execute("INSERT OR IGNORE INTO test(unique_name) VALUES ('foo')")
        # Nothing should happen.
        self.cu.execute("INSERT OR IGNORE INTO test(unique_name) VALUES ('foo')")
        self.cu.execute("SELECT unique_name FROM test")
        self.assertEqual(self.cu.fetchall(), [('foo',)])

    def test_on_conflict_replace(self):
        self.cu.execute("INSERT OR REPLACE INTO test(name, unique_name) VALUES ('Data!', 'foo')")
        # There shouldn't be an IntegrityError exception.
        self.cu.execute("INSERT OR REPLACE INTO test(name, unique_name) VALUES ('Very different data!', 'foo')")
        self.cu.execute("SELECT name, unique_name FROM test")
        self.assertEqual(self.cu.fetchall(), [('Very different data!', 'foo')])


class MultiprocessTests(unittest.TestCase):
    CONNECTION_TIMEOUT = SHORT_TIMEOUT / 1000.  # Defaults to 30 ms

    def tearDown(self):
        unlink(TESTFN)

    def test_ctx_mgr_rollback_if_commit_failed(self):
        # bpo-27334: ctx manager does not rollback if commit fails
        SCRIPT = f"""if 1:
            import sqlite3
            def wait():
                print("started")
                assert "database is locked" in input()

            cx = sqlite3.connect("{TESTFN}", timeout={self.CONNECTION_TIMEOUT})
            cx.create_function("wait", 0, wait)
            with cx:
                cx.execute("create table t(t)")
            try:
                # execute two transactions; both will try to lock the db
                cx.executescript('''
                    -- start a transaction and wait for parent
                    begin transaction;
                    select * from t;
                    select wait();
                    rollback;

                    -- start a new transaction; would fail if parent holds lock
                    begin transaction;
                    select * from t;
                    rollback;
                ''')
            finally:
                cx.close()
        """

        # spawn child process
        proc = subprocess.Popen(
            [sys.executable, "-c", SCRIPT],
            encoding="utf-8",
            bufsize=0,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
        )
        self.addCleanup(proc.communicate)

        # wait for child process to start
        self.assertEqual("started", proc.stdout.readline().strip())

        cx = sqlite.connect(TESTFN, timeout=self.CONNECTION_TIMEOUT)
        try:  # context manager should correctly release the db lock
            with cx:
                cx.execute("insert into t values('test')")
        except sqlite.OperationalError as exc:
            proc.stdin.write(str(exc))
        else:
            proc.stdin.write("no error")
        finally:
            cx.close()

        # terminate child process
        self.assertIsNone(proc.returncode)
        try:
            proc.communicate(input="end", timeout=SHORT_TIMEOUT)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.communicate()
            raise
        self.assertEqual(proc.returncode, 0)


if __name__ == "__main__":
    unittest.main()
