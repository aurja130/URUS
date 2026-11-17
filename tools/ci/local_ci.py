# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Aurora Jahan
# Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.
"""Run every local gate, cheapest first; the first failed step fails the run.

Usage (from the repository root)::

    python3 -m tools.ci.local_ci                run every step
    python3 -m tools.ci.local_ci --list         print the step names and run nothing
    python3 -m tools.ci.local_ci --only pytest  run one step (repeatable)
    python3 -m tools.ci.local_ci --only presets run the five preset steps

clang-tidy runs last because it needs the dev-clang compile database that the preset steps
create.

Each step prints one verdict line, ``PASS <name>  (<seconds>s)`` or ``FAIL <name>  (<seconds>s)``.
A failed step is followed by the last 60 lines of its output, indented by four spaces, and the
run stops there. A step left out by ``--only`` prints ``SKIP <name>`` before the run. When every
step ran and passed, the last line reads ``local CI passed (<n> steps, <seconds>s)``. When
``--only`` left steps out, it reads ``selected steps passed (<k> of <n>; <m> not run)`` instead,
so a partial run never claims a full pass.

Exit codes: 0 every selected step passed; 1 a step failed; 2 usage error.
"""

from __future__ import annotations

import argparse
import json
import os
import shlex
import subprocess
import sys
import time
from collections.abc import Callable, Mapping, Sequence
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from functools import partial
from pathlib import Path, PurePosixPath
from typing import cast

from tools.ci import REPO_ROOT, repository_files

EXIT_OK = 0
EXIT_FAILED = 1
FAILURE_TAIL_LINES = 60
PRESETS = ("dev-gcc", "dev-clang", "asan-ubsan-clang", "tsan-gcc", "release-gcc")
PRESETS_SHORTHAND = "presets"
TIDY_BUILD_DIR = "build/dev-clang"
TIDY_HINT = "rerun cmake --preset dev-clang"
TIDY_UNLISTED_HINT = f"add each to a CMake target, or {TIDY_HINT}"
SOURCE_DIRS = ("src", "tests", "app")
CLANG_FORMAT_SUFFIXES = (".cpp", ".hpp", ".h")
TIDY_SUFFIXES = (".cpp",)

type Command = tuple[str, ...]
type Outcome = tuple[bool, str]


@dataclass(frozen=True)
class Step:
    name: str
    run: Callable[[], Outcome]


@dataclass(frozen=True)
class StepResult:
    name: str
    passed: bool
    seconds: float
    output: str


def run_command(argv: Command) -> Outcome:
    proc = subprocess.run(
        argv,
        cwd=REPO_ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        errors="replace",
        check=False,
    )
    return proc.returncode == 0, f"$ {shlex.join(argv)}\n{proc.stdout}"


def run_in_order(commands: Sequence[Command]) -> Outcome:
    """Run ``commands`` one after another; stop at the first failure."""
    transcript: list[str] = []
    for argv in commands:
        passed, output = run_command(argv)
        transcript.append(output)
        if not passed:
            return False, "".join(transcript)
    return True, "".join(transcript)


def in_order(*commands: Command) -> Callable[[], Outcome]:
    return partial(run_in_order, commands)


def preset_commands(preset: str) -> tuple[Command, Command, Command]:
    return (
        ("cmake", "--preset", preset),
        ("cmake", "--build", "--preset", preset),
        ("ctest", "--preset", preset),
    )


def preset_step_name(preset: str) -> str:
    return f"preset {preset}"


def _in_source_dirs(path: str, suffixes: tuple[str, ...]) -> bool:
    pure = PurePosixPath(path)
    return pure.suffix in suffixes and len(pure.parts) > 1 and pure.parts[0] in SOURCE_DIRS


def _engine_files(suffixes: tuple[str, ...]) -> list[str]:
    """Files of ours under the engine source directories with one of ``suffixes``."""
    return [path for path in repository_files(REPO_ROOT) if _in_source_dirs(path, suffixes)]


def tidy_sources(entries: Sequence[Mapping[str, str]], repo_root: Path) -> list[str]:
    """Engine translation units listed in a compile database, relative to ``repo_root``."""
    root = repo_root.resolve()
    found: set[str] = set()
    for entry in entries:
        source = Path(entry["directory"], entry["file"]).resolve()
        if not source.is_relative_to(root):
            continue
        relative = source.relative_to(root).as_posix()
        if _in_source_dirs(relative, TIDY_SUFFIXES):
            found.add(relative)
    return sorted(found)


def _tidy_one(source: str) -> Outcome:
    return run_command(("clang-tidy", "-p", TIDY_BUILD_DIR, "--quiet", source))


def clang_format_check() -> Outcome:
    """``clang-format --dry-run -Werror`` over every engine source and header of ours."""
    files = _engine_files(CLANG_FORMAT_SUFFIXES)
    if not files:
        return False, "no C++ source or header under src/, tests/ or app/; nothing was checked"
    return run_command(("clang-format", "--dry-run", "-Werror", *files))


def clang_tidy_check() -> Outcome:
    """``clang-tidy`` over every engine ``.cpp`` of ours, against the dev-clang database."""
    database = REPO_ROOT / TIDY_BUILD_DIR / "compile_commands.json"
    if not database.is_file():
        return False, f"{TIDY_BUILD_DIR}/compile_commands.json is missing; {TIDY_HINT}"
    entries = cast(list[dict[str, str]], json.loads(database.read_text(encoding="utf-8")))
    listed = set(tidy_sources(entries, REPO_ROOT))
    expected = _engine_files(TIDY_SUFFIXES)
    if not expected:
        return False, "no engine .cpp source under src/, tests/ or app/; nothing was checked"
    missing = [path for path in expected if path not in listed]
    if missing:
        names = "".join(f"    {path}\n" for path in missing)
        return False, (
            f"{TIDY_BUILD_DIR}/compile_commands.json lacks {len(missing)} engine source(s):\n"
            f"{names}{TIDY_UNLISTED_HINT}"
        )
    with ThreadPoolExecutor(max_workers=os.cpu_count()) as pool:
        results = list(pool.map(_tidy_one, expected))
    failures = [output.rstrip("\n") + "\n" for passed, output in results if not passed]
    return not failures, "".join(failures)


STEPS: list[Step] = [
    Step("toolchain", in_order(("python3", "-m", "tools.toolchain.check_toolchain"))),
    Step("license-headers", in_order(("python3", "-m", "tools.ci.license_headers"))),
    Step("clang-format", clang_format_check),
    Step("ruff check", in_order(("ruff", "check"))),
    Step("ruff format", in_order(("ruff", "format", "--check"))),
    Step("basedpyright", in_order(("basedpyright",))),
    Step("pytest", in_order(("python3", "-m", "pytest"))),
    Step("dbc", in_order(("python3", "Planning/DBC/check_dbc.py"))),
    *(Step(preset_step_name(preset), in_order(*preset_commands(preset))) for preset in PRESETS),
    Step("clang-tidy", clang_tidy_check),
]


def run_step(step: Step) -> StepResult:
    started = time.perf_counter()
    try:
        passed, output = step.run()
    except Exception as exc:
        passed, output = False, f"{type(exc).__name__}: {exc}\n"
    return StepResult(step.name, passed, time.perf_counter() - started, output)


def select_steps(names: list[str] | None) -> list[Step]:
    if names is None:
        return list(STEPS)
    wanted: set[str] = set()
    for name in names:
        if name == PRESETS_SHORTHAND:
            wanted.update(preset_step_name(preset) for preset in PRESETS)
        else:
            wanted.add(name)
    return [step for step in STEPS if step.name in wanted]


def run_steps(selected: Sequence[Step], skipped: Sequence[Step]) -> int:
    """Run ``selected`` in order, stopping at the first failure, and report the outcome."""
    for step in skipped:
        print(f"SKIP {step.name}", flush=True)
    started = time.perf_counter()
    for step in selected:
        result = run_step(step)
        verdict = "PASS" if result.passed else "FAIL"
        print(f"{verdict} {result.name}  ({result.seconds:.1f}s)", flush=True)
        if not result.passed:
            for line in result.output.splitlines()[-FAILURE_TAIL_LINES:]:
                print(f"    {line}")
            return EXIT_FAILED
    elapsed = time.perf_counter() - started
    if skipped:
        step_count = len(selected) + len(skipped)
        print(f"selected steps passed ({len(selected)} of {step_count}; {len(skipped)} not run)")
    else:
        print(f"local CI passed ({len(selected)} steps, {elapsed:.1f}s)")
    return EXIT_OK


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="python3 -m tools.ci.local_ci",
        description="Run the local gates cheapest first; the first failed step fails the run.",
        epilog="exit codes: 0 every selected step passed; 1 a step failed; 2 usage error",
    )
    parser.add_argument("--list", action="store_true", help="print the step names and run nothing")
    parser.add_argument(
        "--only",
        action="append",
        choices=[*(step.name for step in STEPS), PRESETS_SHORTHAND],
        metavar="STEP",
        help="run only this step (repeatable); 'presets' selects the five preset steps",
    )
    args = parser.parse_args(argv)
    names: list[str] | None = args.only
    selected = select_steps(names)
    if args.list:
        for step in selected:
            print(step.name)
        return EXIT_OK
    chosen = {step.name for step in selected}
    skipped = [step for step in STEPS if step.name not in chosen]
    return run_steps(selected, skipped)


if __name__ == "__main__":
    sys.exit(main())
