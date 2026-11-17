# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Aurora Jahan
# Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.
"""Tests for tools.ci.license_headers."""

from __future__ import annotations

from pathlib import Path

import pytest

from tools.ci import license_headers as lh

CURRENT_YEAR = 2026
MISSING_HASH_HEADER = "license header missing: expected three lines starting with '# '"


def _header(prefix: str, year: str = "2026") -> str:
    return (
        f"{prefix}SPDX-License-Identifier: GPL-3.0-or-later\n"
        f"{prefix}Copyright (C) {year} Aurora Jahan\n"
        f"{prefix}Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.\n"
    )


@pytest.mark.parametrize(
    ("path", "prefix"),
    [
        ("src/nts/a.cpp", "// "),
        ("tests/core/b_test.hpp", "// "),
        ("tools/x.py", "# "),
        ("CMakeLists.txt", "# "),
        ("cmake/Deps.cmake", "# "),
    ],
)
def test_correct_header_passes(path: str, prefix: str) -> None:
    assert lh.comment_prefix(path) == prefix
    assert lh.header_problem(path, _header(prefix), CURRENT_YEAR) is None


def test_shebang_line_is_skipped() -> None:
    text = "#!/usr/bin/env python3\n" + _header("# ")
    assert lh.header_problem("tools/x.py", text, CURRENT_YEAR) is None


def test_shebang_without_header_is_missing() -> None:
    text = "#!/usr/bin/env python3\nprint(1)\n"
    assert lh.header_problem("tools/x.py", text, CURRENT_YEAR) == MISSING_HASH_HEADER


def test_two_line_header_is_missing() -> None:
    text = "".join(_header("# ").splitlines(keepends=True)[:2])
    assert lh.header_problem("tools/x.py", text, CURRENT_YEAR) == MISSING_HASH_HEADER


def test_hash_comments_in_a_cpp_file_are_wrong_on_line_one() -> None:
    problem = lh.header_problem("src/nts/a.cpp", _header("# "), CURRENT_YEAR)
    assert problem is not None
    assert problem.startswith("license header line 1 must be '// SPDX")


@pytest.mark.parametrize("year", ["2000", "2026"])
def test_years_in_range_pass(year: str) -> None:
    assert lh.header_problem("tools/x.py", _header("# ", year), CURRENT_YEAR) is None


@pytest.mark.parametrize("year", ["1999", "2027"])
def test_years_out_of_range_are_reported(year: str) -> None:
    problem = lh.header_problem("tools/x.py", _header("# ", year), CURRENT_YEAR)
    assert problem == f"copyright year {year} is outside 2000..{CURRENT_YEAR}"


def test_wrong_license_line_is_reported() -> None:
    lines = _header("# ").splitlines()
    lines[2] = "# Licensed under MIT"
    problem = lh.header_problem("tools/x.py", "\n".join(lines) + "\n", CURRENT_YEAR)
    assert problem is not None
    assert problem.startswith("license header line 3 must be")


def test_candidate_files_select_sources_and_skip_excluded_paths() -> None:
    paths = [
        "",
        "src/nts/a.cpp",
        "src/nts/a.hpp",
        "tools/x.py",
        "CMakeLists.txt",
        "cmake/Deps.cmake",
        "CMakePresets.json",
        "README.md",
        "external/eigen/Eigen/Core.h",
        "Resources/data.cpp",
        "build/_deps/gtest/gtest-all.cpp",
        "Planning/DBC/check_dbc.py",
        "Planning/DBC/other.py",
    ]
    assert lh.candidate_files(paths) == [
        "src/nts/a.cpp",
        "src/nts/a.hpp",
        "tools/x.py",
        "CMakeLists.txt",
        "cmake/Deps.cmake",
        "Planning/DBC/other.py",
    ]


def test_check_files_reports_only_the_bad_file(tmp_path: Path) -> None:
    (tmp_path / "good.py").write_text(_header("# "), encoding="utf-8")
    (tmp_path / "bad.cpp").write_text("int main() {}\n", encoding="utf-8")
    problems = lh.check_files(tmp_path, ["good.py", "bad.cpp", "README.md"], CURRENT_YEAR)
    assert [path for path, _ in problems] == ["bad.cpp"]


def test_non_utf8_file_is_reported(tmp_path: Path) -> None:
    (tmp_path / "blob.py").write_bytes(b"\xff\xfe")
    problems = lh.check_files(tmp_path, ["blob.py"], CURRENT_YEAR)
    assert problems == [("blob.py", "not UTF-8 text")]


def test_main_lists_bad_files_and_fails(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    (tmp_path / "bad.py").write_text("print(1)\n", encoding="utf-8")

    def tracked(_root: Path) -> list[str]:
        return ["bad.py"]

    monkeypatch.setattr(lh, "REPO_ROOT", tmp_path)
    monkeypatch.setattr(lh, "repository_files", tracked)
    assert lh.main([]) == lh.EXIT_FAILED
    out = capsys.readouterr().out
    assert f"bad.py: {MISSING_HASH_HEADER}\n" in out
    assert "license headers FAILED: 1 of 1 files" in out
