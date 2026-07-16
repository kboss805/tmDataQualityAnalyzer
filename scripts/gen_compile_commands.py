#!/usr/bin/env python3
"""Generate .vscode/compile_commands.json for clangd from the qmake build.

qmake does not emit a compilation database, so this scrapes one out of a
`mingw32-make` dry run (`-nB` prints every compile recipe without building).
It covers BOTH targets — the app (built in build/) and the test suite (built
in tests/) — so clangd has accurate per-file flags for every translation unit,
including the by-layer src/<layer>/ folders.

The output is gitignored (a local IDE artifact). Re-run after adding/moving
source files or changing build flags:

    py scripts/gen_compile_commands.py

clangd still applies the Remove/Add rules in .clangd on top of these entries,
so MinGW-only flags it can't parse are stripped there, not here.

NOTE (toolchain): this scraper targets the **MinGW** build — it drives
`mingw32-make` and parses `g++ -c` recipe lines, and clangd's IntelliSense is
GCC-flavored. Since the project's default build toolchain moved to MSVC, run
this with the MinGW toolchain selected:

    $env:TMDQ_TOOLCHAIN = 'mingw'; . .\scripts\env.ps1; py scripts/gen_compile_commands.py

Regenerating this database from the MSVC build (parsing `cl` recipes from an
nmake dry run, clangd `--driver-mode=cl`) is a separate follow-up; the clangd
IntelliSense setup here is still MinGW-based and independent of how the app,
tests, and CI are actually built.
"""

from __future__ import annotations

import json
import os
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent


def _default_qtdir() -> str:
    """Fall back to the QT_VERSION default in scripts/env.ps1 (the single
    source of truth) when QTDIR isn't already set in the environment."""
    env_ps1 = REPO / "scripts" / "env.ps1"
    match = re.search(r"QT_VERSION\s*=\s*'([^']+)'", env_ps1.read_text())
    if not match:
        raise RuntimeError(f"could not find QT_VERSION default in {env_ps1}")
    return f"C:/Qt/{match.group(1)}/mingw_64"


# Toolchain locations. Match tasks.json / .clangd; override via env if needed.
QTDIR = Path(os.environ.get("QTDIR") or _default_qtdir())
MINGW_DIR = Path(os.environ.get("MINGW_DIR", r"C:/Qt/Tools/mingw1310_64"))
QMAKE = QTDIR / "bin" / "qmake.exe"
MAKE = MINGW_DIR / "bin" / "mingw32-make.exe"

# (build directory, .pro file) pairs. The app builds in build/ against the
# top-level .pro; the tests build in-source in tests/ against tests.pro.
TARGETS = [
    (REPO / "build", REPO / "tmDataQualityAnalyzer.pro"),
    (REPO / "tests", REPO / "tests" / "tests.pro"),
]

SOURCE_SUFFIXES = (".cpp", ".cxx", ".cc", ".c")


def tool_env() -> dict[str, str]:
    """Environment with Qt + MinGW on PATH so qmake/make and their sub-tools run."""
    env = dict(os.environ)
    env["PATH"] = os.pathsep.join(
        [str(QTDIR / "bin"), str(MINGW_DIR / "bin"), env.get("PATH", "")]
    )
    return env


def pick_makefile(build_dir: Path) -> str:
    """Prefer the Release makefile; fall back to Debug, then plain Makefile."""
    for name in ("Makefile.Release", "Makefile.Debug", "Makefile"):
        if (build_dir / name).exists():
            return name
    return "Makefile"


def source_token(line: str) -> str | None:
    """Return the source file argument from a `g++ -c ... src.cpp` recipe line."""
    if " -c " not in line:
        return None
    # The compile input is the lone token with a source suffix (the -o target is .o).
    for tok in reversed(line.split()):
        cleaned = tok.strip('"')
        if cleaned.lower().endswith(SOURCE_SUFFIXES):
            return cleaned
    return None


def collect(build_dir: Path, pro: Path) -> list[dict]:
    if not build_dir.exists():
        build_dir.mkdir(parents=True)

    env = tool_env()
    # Regenerate the makefile so flags/file lists track the current .pro.
    subprocess.run([str(QMAKE), str(pro)], cwd=build_dir, env=env, check=True)

    makefile = pick_makefile(build_dir)
    # -n: dry run (print, don't execute); -B: treat everything out of date.
    proc = subprocess.run(
        [str(MAKE), "-nB", "-f", makefile],
        cwd=build_dir,
        env=env,
        capture_output=True,
        text=True,
    )

    entries: list[dict] = []
    seen: set[str] = set()
    for line in proc.stdout.splitlines():
        line = line.strip()
        src = source_token(line)
        if src is None:
            continue
        abs_src = (build_dir / src).resolve()
        # Skip generated TUs that don't exist yet (moc_*/qrc_*) and anything
        # outside the repo; clangd only needs real, on-disk project sources.
        if not abs_src.exists():
            continue
        key = str(abs_src)
        if key in seen:
            continue
        seen.add(key)
        entries.append(
            {
                "directory": str(build_dir),
                "command": line,
                "file": str(abs_src),
            }
        )
    return entries


def main() -> int:
    if not QMAKE.exists() or not MAKE.exists():
        sys.stderr.write(
            f"qmake/make not found.\n  QMAKE={QMAKE}\n  MAKE={MAKE}\n"
            "Set QTDIR / MINGW_DIR env vars to your install.\n"
        )
        return 1

    all_entries: list[dict] = []
    for build_dir, pro in TARGETS:
        if not pro.exists():
            sys.stderr.write(f"skip: {pro} not found\n")
            continue
        all_entries.extend(collect(build_dir, pro))

    out = REPO / ".vscode" / "compile_commands.json"
    out.parent.mkdir(exist_ok=True)
    out.write_text(json.dumps(all_entries, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote {len(all_entries)} entries to {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
