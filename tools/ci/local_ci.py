# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Aurora Jahan
# Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.
"""Run every local gate, cheapest first; the first failed step fails the run.

Usage (from the repository root)::

    python3 -m tools.ci.local_ci                run every step
    python3 -m tools.ci.local_ci --list         print the step names and run nothing
    python3 -m tools.ci.local_ci --only pytest  run one step (repeatable)
    python3 -m tools.ci.local_ci --only presets run the five preset steps

Each step prints one verdict line: ``PASS <name>  (<seconds>s)`` or
``FAIL <name>  (<seconds>s)``. A failed step is followed by the last 60 lines of its output,
indented by four spaces, and the run stops there. When every selected step passes, the last
line reads ``local CI passed (<n> steps, <seconds>s)``.

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
SOURCE_DIRS = ("src", "tests", "app")
CLANG_FORMAT_SUFFIXES = (".cpp", ".hpp", ".h")

type Command = tuple[str, ...]
type Outcome = tuple[bool, str]


@dataclass(frozen=True)
class Step:
    """One gate: the name shown and accepted by ``--only``, and the callable that runs it."""

    name: str
    run: Callable[[], Outcome]


@dataclass(frozen=True)
class StepResult:
    """The outcome of one step, as printed by the runner."""

    name: str
    passed: bool
    seconds: float
    output: str


def run_command(argv: Command) -> Outcome:
    """Run ``argv`` from the repository root; the output starts with the echoed command."""
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
    """Run the commands in order, stopping at the first failure."""
    chunks: list[str] = []
    for argv in commands:
        passed, output = run_command(argv)
        chunks.append(output)
        if not passed:
            return False, "".join(chunks)
    return True, "".join(chunks)


def in_order(*commands: Command) -> Callable[[], Outcome]:
    """A step body that runs ``commands`` in order."""
    return partial(run_in_order, commands)


def preset_commands(preset: str) -> tuple[Command, ...]:
    """Configure, build and test one CMake preset."""
    return (
        ("cmake", "--preset", preset),
        ("cmake", "--build", "--preset", preset),
        ("ctest", "--preset", preset),
    )


def preset_step_name(preset: str) -> str:
    """The name of the step that runs one preset."""
    return f"preset {preset}"


def _in_source_dirs(path: str, suffixes: tuple[str, ...]) -> bool:
    pure = PurePosixPath(path)
    return pure.suffix in suffixes and len(pure.parts) > 1 and pure.parts[0] in SOURCE_DIRS


def tidy_sources(entries: Sequence[Mapping[str, str]], repo_root: Path) -> list[str]:
    """The repository-relative engine ``.cpp`` files named by a compile database."""
    root = repo_root.resolve()
    found: set[str] = set()
    for entry in entries:
        source = (Path(entry["directory"]) / entry["file"]).resolve()
        if not source.is_relative_to(root):
            continue
        relative = source.relative_to(root).as_posix()
        if _in_source_dirs(relative, (".cpp",)):
            found.add(relative)
    return sorted(found)


def _tidy_one(source: str) -> Outcome:
    return run_command(("clang-tidy", "-p", TIDY_BUILD_DIR, "--quiet", source))


def clang_format_check() -> Outcome:
    """``clang-format --dry-run -Werror`` over every engine source and header."""
    sources = repository_files(REPO_ROOT)
    files = [path for path in sources if _in_source_dirs(path, CLANG_FORMAT_SUFFIXES)]
    if not files:
        return True, ""
    return run_command(("clang-format", "--dry-run", "-Werror", *files))


def clang_tidy_check() -> Outcome:
    """``clang-tidy`` over every engine ``.cpp`` in the dev-clang compile database."""
    database = REPO_ROOT / TIDY_BUILD_DIR / "compile_commands.json"
    if not database.is_file():
        hint = "run the dev-clang preset first (cmake --preset dev-clang)"
        return False, f"{TIDY_BUILD_DIR}/compile_commands.json is missing; {hint}"
    entries = cast(list[dict[str, str]], json.loads(database.read_text(encoding="utf-8")))
    sources = tidy_sources(entries, REPO_ROOT)
    if not sources:
        return True, ""
    with ThreadPoolExecutor(max_workers=os.cpu_count()) as pool:
        results = list(pool.map(_tidy_one, sources))
    failures = [output.rstrip("\n") + "\n" for passed, output in results if not passed]
    return not failures, "".join(failures)


STEPS: list[Step] = [
    Step("toolchain", in_order(("python3", "-m", "tools.toolchain.check_toolchain"))),
    Step("license-headers", in_order(("python3", "-m", "tools.ci.license_headers"))),
    Step("clang-format", clang_format_check),
    *(Step(preset_step_name(preset), in_order(*preset_commands(preset))) for preset in PRESETS),
    Step("clang-tidy", clang_tidy_check),
    Step("ruff check", in_order(("ruff", "check"))),
    Step("ruff format", in_order(("ruff", "format", "--check"))),
    Step("basedpyright", in_order(("basedpyright",))),
    Step("pytest", in_order(("python3", "-m", "pytest"))),
    Step("dbc", in_order(("python3", "Planning/DBC/check_dbc.py"))),
]


def run_step(step: Step) -> StepResult:
    """Run one step; an exception fails that step instead of crashing the runner."""
    started = time.perf_counter()
    try:
        passed, output = step.run()
    except Exception as exc:
        passed, output = False, f"{type(exc).__name__}: {exc}"
    return StepResult(step.name, passed, time.perf_counter() - started, output)


def select_steps(names: list[str] | None) -> list[Step]:
    """The named steps in STEPS order; ``presets`` names the five preset steps."""
    if names is None:
        return list(STEPS)
    wanted = {name for name in names if name != PRESETS_SHORTHAND}
    if PRESETS_SHORTHAND in names:
        wanted.update(preset_step_name(preset) for preset in PRESETS)
    return [step for step in STEPS if step.name in wanted]


def run_steps(steps: Sequence[Step]) -> int:
    """Run the steps in order, print a verdict for each and stop at the first failure."""
    started = time.perf_counter()
    for step in steps:
        result = run_step(step)
        verdict = "PASS" if result.passed else "FAIL"
        print(f"{verdict} {result.name}  ({result.seconds:.1f}s)", flush=True)
        if not result.passed:
            for line in result.output.splitlines()[-FAILURE_TAIL_LINES:]:
                print(f"    {line}")
            return EXIT_FAILED
    total = time.perf_counter() - started
    print(f"local CI passed ({len(steps)} steps, {total:.1f}s)")
    return EXIT_OK


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="python3 -m tools.ci.local_ci",
        description="Run the local gates cheapest first; the first failed step fails the run.",
        epilog="exit codes: 0 every selected step passed; 1 a step failed; 2 usage error",
    )
    parser.add_argument(
        "--list",
        action="store_true",
        help="print the selected step names and run nothing",
    )
    parser.add_argument(
        "--only",
        action="append",
        choices=[*(step.name for step in STEPS), PRESETS_SHORTHAND],
        metavar="STEP",
        help="run only this step (repeatable); 'presets' selects the five preset steps",
    )
    args = parser.parse_args(argv)
    list_only: bool = args.list
    names: list[str] | None = args.only
    steps = select_steps(names)
    if list_only:
        for step in steps:
            print(step.name)
        return EXIT_OK
    return run_steps(steps)


if __name__ == "__main__":
    sys.exit(main())
