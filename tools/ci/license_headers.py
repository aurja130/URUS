# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Aurora Jahan
# Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.
"""Check that every tracked or untracked, not ignored, code file of ours starts with the
project license header.

Usage (from the repository root)::

    python3 -m tools.ci.license_headers

A candidate file is a Python, C++ or CMake source (``.py``, ``.cpp``, ``.hpp``, ``.h``,
``.cmake``) or a ``CMakeLists.txt``. After an optional leading ``#!`` line it must open with
three header lines, each starting with the comment marker of its language (``# `` for Python
and CMake, ``// `` for C++)::

    <marker>SPDX-License-Identifier: GPL-3.0-or-later
    <marker>Copyright (C) <YYYY> Aurora Jahan
    <marker>Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

The year must be between 2026, the year of the repository's first commit, and the current year.
Files under ``external/``, ``Resources/`` and ``build/``, and the DBC checker
``Planning/DBC/check_dbc.py`` (read-only planning material), are not checked.

Exit codes: 0 all headers correct; 1 a header is wrong or missing, or no file was checked;
2 usage error.
"""

from __future__ import annotations

import argparse
import re
import sys
from collections.abc import Iterable, Sequence
from datetime import date
from pathlib import Path, PurePosixPath

from tools.ci import REPO_ROOT, repository_files

EXIT_OK = 0
EXIT_FAILED = 1
FIRST_YEAR = 2026
HEADER_LINES = 3
EXCLUDED_TOP_DIRS = ("external", "Resources", "build")
EXCLUDED_FILES = ("Planning/DBC/check_dbc.py",)
CODE_SUFFIXES = (".py", ".cpp", ".hpp", ".h", ".cmake")
HASH_SUFFIXES = (".py", ".cmake")
CMAKE_NAME = "CMakeLists.txt"
SPDX_LINE = "SPDX-License-Identifier: GPL-3.0-or-later"
COPYRIGHT_TEXT = r"Copyright \(C\) ([0-9]{4}) Aurora Jahan"
LICENSED_LINE = "Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE."


def comment_prefix(path: str) -> str:
    """The comment marker that opens each header line of ``path``."""
    pure = PurePosixPath(path)
    if pure.name == CMAKE_NAME or pure.suffix in HASH_SUFFIXES:
        return "# "
    return "// "


def header_problem(path: str, text: str, current_year: int) -> str | None:
    """Why ``text`` does not open with the license header of ``path``; None when it does."""
    prefix = comment_prefix(path)
    lines = text.splitlines()
    if lines and lines[0].startswith("#!"):
        lines = lines[1:]
    if len(lines) < HEADER_LINES:
        return f"license header missing: expected three lines starting with {prefix!r}"
    spdx_line, copyright_line, licensed_line = lines[:HEADER_LINES]
    expected = prefix + SPDX_LINE
    if spdx_line != expected:
        return f"license header line 1 must be {expected!r}, found {spdx_line!r}"
    copyright_match = re.fullmatch(re.escape(prefix) + COPYRIGHT_TEXT, copyright_line)
    if copyright_match is None:
        expected = prefix + "Copyright (C) <YYYY> Aurora Jahan"
        return f"license header line 2 must read {expected!r}, found {copyright_line!r}"
    year_text: str = copyright_match.group(1)
    year = int(year_text)
    if not FIRST_YEAR <= year <= current_year:
        return f"copyright year {year} is outside {FIRST_YEAR}..{current_year}"
    expected = prefix + LICENSED_LINE
    if licensed_line != expected:
        return f"license header line 3 must be {expected!r}, found {licensed_line!r}"
    return None


def _is_candidate(path: str) -> bool:
    if not path or path in EXCLUDED_FILES:
        return False
    pure = PurePosixPath(path)
    if pure.parts[0] in EXCLUDED_TOP_DIRS:
        return False
    return pure.suffix in CODE_SUFFIXES or pure.name == CMAKE_NAME


def candidate_files(paths: Iterable[str]) -> list[str]:
    """The paths, in order, whose license header is checked."""
    return [path for path in paths if _is_candidate(path)]


def _read_problem(root: Path, path: str, current_year: int) -> str | None:
    try:
        text = (root / path).read_text(encoding="utf-8")
    except UnicodeDecodeError:
        return "not UTF-8 text"
    return header_problem(path, text, current_year)


def check_files(root: Path, paths: Iterable[str], current_year: int) -> list[tuple[str, str]]:
    """The ``(path, reason)`` pairs of the candidate files under ``root`` with a bad header."""
    problems: list[tuple[str, str]] = []
    for path in candidate_files(paths):
        reason = _read_problem(root, path, current_year)
        if reason is not None:
            problems.append((path, reason))
    return problems


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="python3 -m tools.ci.license_headers",
        description=(
            "Check the license header of every tracked or untracked, not ignored, "
            "code file of ours."
        ),
        epilog=(
            "exit codes: 0 all headers correct, 1 a header is wrong or no file was checked, "
            "2 usage error"
        ),
    )
    parser.parse_args(argv)
    paths = repository_files(REPO_ROOT)
    checked = len(candidate_files(paths))
    problems = check_files(REPO_ROOT, paths, date.today().year)
    for path, reason in problems:
        print(f"{path}: {reason}")
    if problems:
        print(f"license headers FAILED: {len(problems)} of {checked} files")
        return EXIT_FAILED
    if checked == 0:
        print("license headers FAILED: no code file of ours found; nothing was checked")
        return EXIT_FAILED
    print(f"license headers passed ({checked} files)")
    return EXIT_OK


if __name__ == "__main__":
    sys.exit(main())
