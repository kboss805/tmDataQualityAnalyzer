#!/usr/bin/env python3
"""Generate compile_flags.txt for clangd / clang-tidy (MSVC toolchain).

clangd (and clang-tidy) parse the code with clang, so a single project-root
`compile_flags.txt` — one flag per line, applied to every translation unit —
is all that's needed here: every TU shares the same include paths and defines.
clangd discovers `compile_flags.txt` automatically by walking up from each source
file, so there is no per-developer editor config to maintain.

The Qt include paths are machine-specific (they live under QTDIR), so this file
is regenerated locally rather than committed. Re-run after a Qt version bump or
when include dirs / defines change:

    py scripts/gen_compile_flags.py

The output (compile_flags.txt) is gitignored. clang-tidy picks it up the same way
(no -p needed):  clang-tidy src/model/frameprocessor.cpp
"""

from __future__ import annotations

import os
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent


def qtdir() -> Path:
    """Resolve the Qt msvc kit dir: QTDIR env, else C:/Qt/<QT_VERSION>/msvc2022_64
    (mirrors scripts/env.ps1's default so the version lives in one place)."""
    if os.environ.get("QTDIR"):
        return Path(os.environ["QTDIR"])
    version = os.environ.get("QT_VERSION")
    if not version:
        # Fall back to the version single-sourced in constants.h is overkill here;
        # env.ps1 defaults to 6.11.1, so match that.
        version = "6.11.1"
    root = os.environ.get("QT_ROOT") or f"C:/Qt/{version}"
    return Path(root) / "msvc2022_64"


# Project include dirs (mirror INCLUDEPATH in tmDataQualityAnalyzer.pro), repo-relative.
PROJECT_INCLUDES = [
    "include",
    "include/dto",
    "include/model",
    "include/viewmodel",
    "include/view",
    "lib/irig106/include",
    "lib/qcustomplot",
]

# Qt modules the project links (QT += ... in the .pro, plus testlib for tests/).
QT_MODULES = [
    "QtCore", "QtGui", "QtWidgets", "QtPrintSupport",
    "QtConcurrent", "QtSvg", "QtNetwork", "QtTest",
]

# Preprocessor defines (mirror the qmake-generated DEFINES for the msvc build).
DEFINES = [
    "UNICODE", "_UNICODE", "WIN32", "WIN64", "_ENABLE_EXTENDED_ALIGNED_STORAGE",
    "QT_CORE_LIB", "QT_GUI_LIB", "QT_WIDGETS_LIB", "QT_PRINTSUPPORT_LIB",
    "QT_CONCURRENT_LIB", "QT_SVG_LIB", "QT_NETWORK_LIB", "QT_TESTLIB_LIB",
]


def main() -> int:
    qt = qtdir()
    qt_inc = qt / "include"
    if not qt_inc.is_dir():
        sys.stderr.write(
            f"Qt include dir not found: {qt_inc}\n"
            "Set QTDIR (or QT_VERSION / QT_ROOT) to your Qt msvc kit — see scripts/env.ps1.\n"
        )
        return 1

    flags: list[str] = [
        "-std=c++17",
        # clangd parses with clang; target MSVC ABI and enable MS compatibility so
        # Qt's MSVC headers and __declspec/etc. resolve the same way cl sees them.
        "--target=x86_64-pc-windows-msvc",
        "-fms-compatibility",
        "-fms-extensions",
    ]

    # Include paths, absolute so the file is location-independent.
    for rel in PROJECT_INCLUDES:
        flags.append(f"-I{(REPO / rel).resolve()}")
    flags.append(f"-I{qt_inc.resolve()}")
    for mod in QT_MODULES:
        d = qt_inc / mod
        if d.is_dir():
            flags.append(f"-I{d.resolve()}")

    for d in DEFINES:
        flags.append(f"-D{d}")

    out = REPO / "compile_flags.txt"
    out.write_text("\n".join(flags) + "\n", encoding="utf-8")
    print(f"Wrote {out} ({len(flags)} flags, Qt: {qt})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
