#!/usr/bin/env python
"""Create a WASM asset bundle directory structure.

The WASM asset bundles are pre-loaded by the final WASM build. The bundle
contains:

- a stripped down, pyc-only stdlib zip file, e.g. {PREFIX}/lib/python311.zip
- os.py as marker module {PREFIX}/lib/python3.11/os.py
- empty lib-dynload directory, to make sure it is copied into the bundle {PREFIX}/lib/python3.11/lib-dynload/.empty
"""

import argparse
import pathlib
import shutil
import sys
import zipfile

# source directory
SRCDIR = pathlib.Path(__file__).parent.parent.parent.absolute()
SRCDIR_LIB = SRCDIR / "Lib"

# sysconfig data relative to build dir.
SYSCONFIGDATA = pathlib.PurePath(
    "build",
    f"lib.emscripten-wasm32-{sys.version_info.major}.{sys.version_info.minor}",
    "_sysconfigdata__emscripten_wasm32-emscripten.py",
)

# Library directory relative to $(prefix).
WASM_LIB = pathlib.PurePath("lib")
WASM_STDLIB_ZIP = (
    WASM_LIB / f"python{sys.version_info.major}{sys.version_info.minor}.zip"
)
WASM_STDLIB = (
    WASM_LIB / f"python{sys.version_info.major}.{sys.version_info.minor}"
)
WASM_DYNLOAD = WASM_STDLIB / "lib-dynload"


# Don't ship large files / packages that are not particularly useful at
# the moment.
OMIT_FILES = (
    # regression tests
    "test/",
    # package management
    "ensurepip/",
    "venv/",
    # build system
    "distutils/",
    "lib2to3/",
    # deprecated
    "asyncore.py",
    "asynchat.py",
    "uu.py",
    "xdrlib.py",
    # other platforms
    "_aix_support.py",
    "_bootsubprocess.py",
    "_osx_support.py",
    # webbrowser
    "antigravity.py",
    "webbrowser.py",
    # Pure Python implementations of C extensions
    "_pydecimal.py",
    "_pyio.py",
    # Misc unused or large files
    "pydoc_data/",
    "msilib/",
)

# Synchronous network I/O and protocols are not supported; for example,
# socket.create_connection() raises an exception:
# "BlockingIOError: [Errno 26] Operation in progress".
OMIT_NETWORKING_FILES = (
    "cgi.py",
    "cgitb.py",
    "email/",
    "ftplib.py",
    "http/",
    "imaplib.py",
    "mailbox.py",
    "mailcap.py",
    "nntplib.py",
    "poplib.py",
    "smtpd.py",
    "smtplib.py",
    "socketserver.py",
    "telnetlib.py",
    # keep urllib.parse for pydoc
    "urllib/error.py",
    "urllib/request.py",
    "urllib/response.py",
    "urllib/robotparser.py",
    "wsgiref/",
)

OMIT_MODULE_FILES = {
    "_asyncio": ["asyncio/"],
    "audioop": ["aifc.py", "sunau.py", "wave.py"],
    "_crypt": ["crypt.py"],
    "_curses": ["curses/"],
    "_ctypes": ["ctypes/"],
    "_decimal": ["decimal.py"],
    "_dbm": ["dbm/ndbm.py"],
    "_gdbm": ["dbm/gnu.py"],
    "_json": ["json/"],
    "_multiprocessing": ["concurrent/", "multiprocessing/"],
    "pyexpat": ["xml/", "xmlrpc/"],
    "readline": ["rlcompleter.py"],
    "_sqlite3": ["sqlite3/"],
    "_ssl": ["ssl.py"],
    "_tkinter": ["idlelib/", "tkinter/", "turtle.py", "turtledemo/"],

    "_zoneinfo": ["zoneinfo/"],
}

# regression test sub directories
OMIT_SUBDIRS = (
    "ctypes/test/",
    "tkinter/test/",
    "unittest/test/",
)


def create_stdlib_zip(
    args: argparse.Namespace,
    *,
    optimize: int = 0,
) -> None:
    def filterfunc(name: str) -> bool:
        return not name.startswith(args.omit_subdirs_absolute)

    with zipfile.PyZipFile(
        args.wasm_stdlib_zip, mode="w", compression=args.compression, optimize=optimize
    ) as pzf:
        if args.compresslevel is not None:
            pzf.compresslevel = args.compresslevel
        pzf.writepy(args.sysconfig_data)
        for entry in sorted(args.srcdir_lib.iterdir()):
            if entry.name == "__pycache__":
                continue
            if entry in args.omit_files_absolute:
                continue
            if entry.name.endswith(".py") or entry.is_dir():
                # writepy() writes .pyc files (bytecode).
                pzf.writepy(entry, filterfunc=filterfunc)


def detect_extension_modules(args: argparse.Namespace):
    modules = {}

    # disabled by Modules/Setup.local ?
    with open(args.builddir / "Makefile") as f:
        for line in f:
            if line.startswith("MODDISABLED_NAMES="):
                disabled = line.split("=", 1)[1].strip().split()
                for modname in disabled:
                    modules[modname] = False
                break

    # disabled by configure?
    with open(args.sysconfig_data) as f:
        data = f.read()
    loc = {}
    exec(data, globals(), loc)

    for name, value in loc["build_time_vars"].items():
        if value not in {"yes", "missing", "disabled", "n/a"}:
            continue
        if not name.startswith("MODULE_"):
            continue
        if name.endswith(("_CFLAGS", "_DEPS", "_LDFLAGS")):
            continue
        modname = name.removeprefix("MODULE_").lower()
        if modname not in modules:
            modules[modname] = value == "yes"
    return modules


def path(val: str) -> pathlib.Path:
    return pathlib.Path(val).absolute()


parser = argparse.ArgumentParser()
parser.add_argument(
    "--builddir",
    help="absolute build directory",
    default=pathlib.Path(".").absolute(),
    type=path,
)
parser.add_argument(
    "--prefix",
    help="install prefix",
    default=pathlib.Path("/usr/local"),
    type=path,
)


def main():
    args = parser.parse_args()

    relative_prefix = args.prefix.relative_to(pathlib.Path("/"))
    args.srcdir = SRCDIR
    args.srcdir_lib = SRCDIR_LIB
    args.wasm_root = args.builddir / relative_prefix
    args.wasm_stdlib_zip = args.wasm_root / WASM_STDLIB_ZIP
    args.wasm_stdlib = args.wasm_root / WASM_STDLIB
    args.wasm_dynload = args.wasm_root / WASM_DYNLOAD

    # bpo-17004: zipimport supports only zlib compression.
    # Emscripten ZIP_STORED + -sLZ4=1 linker flags results in larger file.
    args.compression = zipfile.ZIP_DEFLATED
    args.compresslevel = 9

    args.sysconfig_data = args.builddir / SYSCONFIGDATA
    if not args.sysconfig_data.is_file():
        raise ValueError(f"sysconfigdata file {SYSCONFIGDATA} missing.")

    extmods = detect_extension_modules(args)
    omit_files = list(OMIT_FILES)
    omit_files.extend(OMIT_NETWORKING_FILES)
    for modname, modfiles in OMIT_MODULE_FILES.items():
        if not extmods.get(modname):
            omit_files.extend(modfiles)

    args.omit_files_absolute = {args.srcdir_lib / name for name in omit_files}
    args.omit_subdirs_absolute = tuple(
        str(args.srcdir_lib / name) for name in OMIT_SUBDIRS
    )

    # Empty, unused directory for dynamic libs, but required for site initialization.
    args.wasm_dynload.mkdir(parents=True, exist_ok=True)
    marker = args.wasm_dynload / ".empty"
    marker.touch()
    # os.py is a marker for finding the correct lib directory.
    shutil.copy(args.srcdir_lib / "os.py", args.wasm_stdlib)
    # The rest of stdlib that's useful in a WASM context.
    create_stdlib_zip(args)
    size = round(args.wasm_stdlib_zip.stat().st_size / 1024**2, 2)
    parser.exit(0, f"Created {args.wasm_stdlib_zip} ({size} MiB)\n")


if __name__ == "__main__":
    main()
