# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Aurora Jahan
# Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.
"""Tests for tools.ci.local_ci. No real gate runs here; the pytest gate runs these tests."""

from __future__ import annotations

import json
import sys
from collections.abc import Callable
from pathlib import Path

import pytest

from tools.ci import local_ci as lc

EXPECTED_STEPS = [
    "toolchain",
    "license-headers",
    "clang-format",
    "ruff check",
    "ruff format",
    "basedpyright",
    "pytest",
    "dbc",
    "preset dev-gcc",
    "preset dev-clang",
    "preset asan-ubsan-clang",
    "preset tsan-gcc",
    "preset release-gcc",
    "clang-tidy",
]
PRESET_STEPS = [name for name in EXPECTED_STEPS if name.startswith("preset ")]


def _names(steps: list[lc.Step]) -> list[str]:
    return [step.name for step in steps]


def _step(name: str) -> lc.Step:
    return next(step for step in lc.STEPS if step.name == name)


def _record_commands(monkeypatch: pytest.MonkeyPatch) -> list[lc.Command]:
    """Replace ``run_command`` with a recorder that passes every command it is given."""
    calls: list[lc.Command] = []

    def fake_run_command(argv: lc.Command) -> lc.Outcome:
        calls.append(argv)
        return True, ""

    monkeypatch.setattr(lc, "run_command", fake_run_command)
    return calls


def _repo_files(paths: list[str]) -> Callable[[Path], list[str]]:
    """A stand-in for ``repository_files`` that lists ``paths`` whatever the root."""

    def listed(_root: Path) -> list[str]:
        return paths

    return listed


def _write_database(root: Path, entries: list[dict[str, str]]) -> None:
    database = root / "build" / "dev-clang" / "compile_commands.json"
    database.parent.mkdir(parents=True)
    database.write_text(json.dumps(entries), encoding="utf-8")


def _tidy_passes(_source: str) -> lc.Outcome:
    return True, ""


def test_steps_run_cheapest_first_with_clang_tidy_last() -> None:
    assert _names(lc.STEPS) == EXPECTED_STEPS


def test_each_preset_is_configured_built_and_tested() -> None:
    assert lc.preset_commands("dev-gcc") == (
        ("cmake", "--preset", "dev-gcc"),
        ("cmake", "--build", "--preset", "dev-gcc"),
        ("ctest", "--preset", "dev-gcc"),
    )


@pytest.mark.parametrize("preset", lc.PRESETS)
def test_each_preset_step_configures_builds_and_tests_its_preset(
    preset: str, monkeypatch: pytest.MonkeyPatch
) -> None:
    calls = _record_commands(monkeypatch)
    assert lc.run_step(_step(f"preset {preset}")).passed
    assert calls == [
        ("cmake", "--preset", preset),
        ("cmake", "--build", "--preset", preset),
        ("ctest", "--preset", preset),
    ]


@pytest.mark.parametrize(
    ("name", "argv"),
    [
        ("toolchain", ("python3", "-m", "tools.toolchain.check_toolchain")),
        ("license-headers", ("python3", "-m", "tools.ci.license_headers")),
        ("ruff check", ("ruff", "check")),
        ("ruff format", ("ruff", "format", "--check")),
        ("basedpyright", ("basedpyright",)),
        ("pytest", ("python3", "-m", "pytest")),
        ("dbc", ("python3", "Planning/DBC/check_dbc.py")),
    ],
)
def test_command_step_runs_exactly_its_command(
    name: str, argv: lc.Command, monkeypatch: pytest.MonkeyPatch
) -> None:
    calls = _record_commands(monkeypatch)
    assert lc.run_step(_step(name)).passed
    assert calls == [argv]


def test_select_steps_keeps_the_step_order() -> None:
    assert _names(lc.select_steps(["pytest", "toolchain"])) == ["toolchain", "pytest"]


def test_select_steps_expands_presets() -> None:
    assert _names(lc.select_steps(["presets"])) == PRESET_STEPS


def test_select_steps_combines_presets_with_other_names() -> None:
    assert _names(lc.select_steps(["dbc", "presets"])) == ["dbc", *PRESET_STEPS]


def test_select_steps_without_names_selects_every_step() -> None:
    assert _names(lc.select_steps(None)) == EXPECTED_STEPS


def test_unknown_only_name_is_a_usage_error(capsys: pytest.CaptureFixture[str]) -> None:
    with pytest.raises(SystemExit) as excinfo:
        lc.main(["--only", "nosuch"])
    assert excinfo.value.code == 2
    err = capsys.readouterr().err
    assert "invalid choice" in err
    assert "preset dev-gcc" in err


def test_list_prints_every_step_and_runs_nothing(capsys: pytest.CaptureFixture[str]) -> None:
    assert lc.main(["--list"]) == lc.EXIT_OK
    assert capsys.readouterr().out.splitlines() == EXPECTED_STEPS


def test_only_skips_the_other_steps_and_never_claims_a_full_pass(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    steps = [lc.Step("a", lambda: (True, "a\n")), lc.Step("b", lambda: (True, "b\n"))]
    monkeypatch.setattr(lc, "STEPS", steps)
    assert lc.main(["--only", "a"]) == lc.EXIT_OK
    lines = capsys.readouterr().out.splitlines()
    assert len(lines) == 3
    assert lines[0] == "SKIP b"
    assert lines[1].startswith("PASS a  (")
    assert lines[2] == "selected steps passed (1 of 2; 1 not run)"


def test_only_naming_every_step_is_still_a_full_run(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    steps = [lc.Step("a", lambda: (True, "a\n")), lc.Step("b", lambda: (True, "b\n"))]
    monkeypatch.setattr(lc, "STEPS", steps)
    assert lc.main(["--only", "a", "--only", "b"]) == lc.EXIT_OK
    last_line = capsys.readouterr().out.splitlines()[-1]
    assert last_line.startswith("local CI passed (2 steps,")


def test_a_failing_command_fails_the_step_and_keeps_its_output() -> None:
    command = (sys.executable, "-c", "print('boom'); raise SystemExit(3)")
    result = lc.run_step(lc.Step("boom", lambda: lc.run_command(command)))
    assert not result.passed
    assert result.output.endswith("boom\n")


def test_a_passing_command_passes_the_step() -> None:
    command = (sys.executable, "-c", "print('ok')")
    result = lc.run_step(lc.Step("ok", lambda: lc.run_command(command)))
    assert result.passed
    assert result.output.endswith("ok\n")


def test_a_missing_executable_fails_the_step_instead_of_crashing() -> None:
    missing = ("sim-no-such-command-xyz",)
    result = lc.run_step(lc.Step("gone", lambda: lc.run_command(missing)))
    assert not result.passed
    assert "sim-no-such-command-xyz" in result.output


def test_run_in_order_stops_at_the_first_failing_command(tmp_path: Path) -> None:
    marker = tmp_path / "ran"
    fail = (sys.executable, "-c", "raise SystemExit(1)")
    touch = (sys.executable, "-c", f"open({marker.as_posix()!r}, 'w').close()")
    passed, _ = lc.run_in_order([fail, touch])
    assert not passed
    assert not marker.exists()


def test_clang_format_checks_the_engine_sources_and_headers_only(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    calls = _record_commands(monkeypatch)
    monkeypatch.setattr(
        lc, "repository_files", _repo_files(["src/nts/a.cpp", "src/nts/b.hpp", "tools/x.py"])
    )
    assert lc.clang_format_check() == (True, "")
    assert calls == [("clang-format", "--dry-run", "-Werror", "src/nts/a.cpp", "src/nts/b.hpp")]


def test_clang_format_fails_when_clang_format_reports_a_file(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.setattr(lc, "repository_files", _repo_files(["src/nts/a.cpp"]))

    def fake_run_command(_argv: lc.Command) -> lc.Outcome:
        return False, "src/nts/a.cpp:3:1: error: code should be clang-formatted\n"

    monkeypatch.setattr(lc, "run_command", fake_run_command)
    passed, output = lc.clang_format_check()
    assert not passed
    assert "clang-formatted" in output


def test_clang_format_fails_when_no_engine_source_exists(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    calls = _record_commands(monkeypatch)
    monkeypatch.setattr(lc, "repository_files", _repo_files(["tools/x.py", "README.md"]))
    passed, output = lc.clang_format_check()
    assert not passed
    assert "nothing was checked" in output
    assert calls == []


def test_tidy_sources_keeps_engine_sources_once(tmp_path: Path) -> None:
    entries = [
        {"directory": str(tmp_path), "file": "src/nts/a.cpp"},
        {"directory": str(tmp_path), "file": str(tmp_path / "app" / "main.cpp")},
        {"directory": str(tmp_path / "build" / "dev-clang"), "file": "../../src/nts/a.cpp"},
        {"directory": str(tmp_path), "file": "tests/unit/b_test.cpp"},
        {"directory": str(tmp_path), "file": "src/nts/c.hpp"},
        {"directory": str(tmp_path), "file": "build/_deps/gtest/gtest-all.cpp"},
        {"directory": str(tmp_path), "file": "tools/x.cpp"},
        {"directory": str(tmp_path), "file": "/usr/include/foo.cpp"},
    ]
    assert lc.tidy_sources(entries, tmp_path) == [
        "app/main.cpp",
        "src/nts/a.cpp",
        "tests/unit/b_test.cpp",
    ]


def test_clang_tidy_needs_the_dev_clang_database(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setattr(lc, "REPO_ROOT", tmp_path)
    passed, output = lc.clang_tidy_check()
    assert not passed
    assert "dev-clang" in output


def test_clang_tidy_fails_when_no_engine_source_exists(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    _write_database(tmp_path, [{"directory": str(tmp_path), "file": "tools/x.cpp"}])
    monkeypatch.setattr(lc, "REPO_ROOT", tmp_path)
    monkeypatch.setattr(lc, "repository_files", _repo_files(["tools/x.cpp"]))
    passed, output = lc.clang_tidy_check()
    assert not passed
    assert "nothing was checked" in output


def test_clang_tidy_fails_when_the_database_lacks_an_engine_source(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    _write_database(tmp_path, [{"directory": str(tmp_path), "file": "src/nts/a.cpp"}])
    monkeypatch.setattr(lc, "REPO_ROOT", tmp_path)
    monkeypatch.setattr(lc, "repository_files", _repo_files(["src/nts/a.cpp", "src/nts/b.cpp"]))
    passed, output = lc.clang_tidy_check()
    assert not passed
    assert "src/nts/b.cpp" in output
    assert "src/nts/a.cpp" not in output
    assert "add each to a CMake target" in output
    assert lc.TIDY_HINT in output


def test_clang_tidy_passes_when_every_engine_source_passes(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    sources = ["src/nts/a.cpp", "app/main.cpp"]
    _write_database(tmp_path, [{"directory": str(tmp_path), "file": path} for path in sources])
    monkeypatch.setattr(lc, "REPO_ROOT", tmp_path)
    monkeypatch.setattr(lc, "repository_files", _repo_files(sources))
    monkeypatch.setattr(lc, "_tidy_one", _tidy_passes)
    assert lc.clang_tidy_check() == (True, "")


def test_clang_tidy_returns_the_output_of_each_failing_source(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    sources = ["src/nts/a.cpp", "src/nts/b.cpp"]
    _write_database(tmp_path, [{"directory": str(tmp_path), "file": path} for path in sources])
    monkeypatch.setattr(lc, "REPO_ROOT", tmp_path)
    monkeypatch.setattr(lc, "repository_files", _repo_files(sources))

    def fake_tidy_one(source: str) -> lc.Outcome:
        if source == "src/nts/b.cpp":
            return False, "src/nts/b.cpp:4:1: error: boom [clang-diagnostic-error]\n"
        return True, ""

    monkeypatch.setattr(lc, "_tidy_one", fake_tidy_one)
    passed, output = lc.clang_tidy_check()
    assert not passed
    assert output == "src/nts/b.cpp:4:1: error: boom [clang-diagnostic-error]\n"


def test_main_stops_at_the_first_failure_and_prints_its_tail(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    failing_output = "".join(f"line {index}\n" for index in range(70))
    steps = [
        lc.Step("fine", lambda: (True, "fine\n")),
        lc.Step("broken", lambda: (False, failing_output)),
        lc.Step("never", lambda: pytest.fail("the run must stop at 'broken'")),
    ]
    monkeypatch.setattr(lc, "STEPS", steps)
    assert lc.main([]) == lc.EXIT_FAILED
    out = capsys.readouterr().out
    assert out.startswith("PASS fine  (")
    assert "FAIL broken  (" in out
    assert "    line 10\n" in out
    assert "    line 69\n" in out
    assert "    line 9\n" not in out
    assert "    line 0\n" not in out


def test_main_passes_when_every_step_passes(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    monkeypatch.setattr(lc, "STEPS", [lc.Step("solo", lambda: (True, "ok\n"))])
    assert lc.main([]) == lc.EXIT_OK
    last_line = capsys.readouterr().out.splitlines()[-1]
    assert last_line.startswith("local CI passed (1 steps,")
