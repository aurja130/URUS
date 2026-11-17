# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Aurora Jahan
# Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.
"""Tests for tools.ci.local_ci. No real gate runs here; the pytest gate runs these tests."""

from __future__ import annotations

import json
import sys
from pathlib import Path

import pytest

from tools.ci import local_ci as lc

EXPECTED_STEPS = [
    "toolchain",
    "license-headers",
    "clang-format",
    "preset dev-gcc",
    "preset dev-clang",
    "preset asan-ubsan-clang",
    "preset tsan-gcc",
    "preset release-gcc",
    "clang-tidy",
    "ruff check",
    "ruff format",
    "basedpyright",
    "pytest",
    "dbc",
]
PRESET_STEPS = EXPECTED_STEPS[3:8]


def _names(steps: list[lc.Step]) -> list[str]:
    return [step.name for step in steps]


def test_steps_follow_the_plan_order() -> None:
    assert _names(lc.STEPS) == EXPECTED_STEPS


def test_each_preset_is_configured_built_and_tested() -> None:
    assert lc.preset_commands("dev-gcc") == (
        ("cmake", "--preset", "dev-gcc"),
        ("cmake", "--build", "--preset", "dev-gcc"),
        ("ctest", "--preset", "dev-gcc"),
    )


def test_select_steps_keeps_the_step_order() -> None:
    assert _names(lc.select_steps(["pytest", "toolchain"])) == ["toolchain", "pytest"]


def test_select_steps_expands_presets() -> None:
    assert _names(lc.select_steps(["presets"])) == PRESET_STEPS


def test_select_steps_combines_presets_with_other_names() -> None:
    assert _names(lc.select_steps(["dbc", "presets"])) == [*PRESET_STEPS, "dbc"]


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


def test_clang_tidy_passes_when_no_engine_source_is_listed(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    database = tmp_path / "build" / "dev-clang" / "compile_commands.json"
    database.parent.mkdir(parents=True)
    entries = [{"directory": str(tmp_path), "file": "tools/x.cpp"}]
    database.write_text(json.dumps(entries), encoding="utf-8")
    monkeypatch.setattr(lc, "REPO_ROOT", tmp_path)
    assert lc.clang_tidy_check() == (True, "")


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
