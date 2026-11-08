#!/usr/bin/env python3
"""Generate compile_flags.txt for clangd / clang-tidy (MSVC toolchain).

clangd (and clang-tidy) parse the code with clang, so a single project-root
`compile_flags.txt` — one flag per line, applied to every translation unit —
is all that's needed here: every native TU (the core, the C ABI and the doctest
suite) shares the same include paths and defines. clangd discovers
`compile_flags.txt` automatically by walking up from each source file, so there
is no per-developer editor config to maintain.

The paths are absolute, so this file is generated locally rather than committed.
Re-run it after a fresh clone, inside each worktree (a worktree otherwise resolves
headers from the main checkout), or when include dirs / defines change:

    py eng/gen_compile_flags.py

The output (compile_flags.txt) is gitignored. clang-tidy picks it up the same way
(no -p needed):  clang-tidy src/core/src/frameprocessor.cpp
"""

from __future__ import annotations

from pathlib import Path

REPO = Path(__file__).resolve().parent.parent

# The native projects' include dirs (AdditionalIncludeDirectories in src/core/tmdq_core.vcxproj and
# tests/core/tmdq_core_tests.vcxproj), repo-relative.
PROJECT_INCLUDES = [
    "src/core/include",
    "src/core/include/dto",
    "src/core/include/model",
    "third_party/irig106/include",
    "third_party/doctest",
    "src/core/abi",
]

# Their preprocessor defines. The ABI's export macro (TMDQ_ABI_BUILD in the DLL, TMDQ_ABI_STATIC in the tests) is
# left out: either one parses the same.
DEFINES = [
    "UNICODE", "_UNICODE", "WIN32", "WIN64", "_ENABLE_EXTENDED_ALIGNED_STORAGE",
    "DOCTEST_CONFIG_USE_STD_HEADERS",
]


def main() -> int:
    flags: list[str] = [
        "-std=c++20",
        # clangd parses with clang; target MSVC ABI and enable MS compatibility so
        # the MSVC headers and __declspec/etc. resolve the same way cl sees them.
        "--target=x86_64-pc-windows-msvc",
        "-fms-compatibility",
        "-fms-extensions",
    ]

    # Include paths, absolute so the file is location-independent.
    for rel in PROJECT_INCLUDES:
        flags.append(f"-I{(REPO / rel).resolve()}")

    for d in DEFINES:
        flags.append(f"-D{d}")

    out = REPO / "compile_flags.txt"
    out.write_text("\n".join(flags) + "\n", encoding="utf-8")
    print(f"Wrote {out} ({len(flags)} flags)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
