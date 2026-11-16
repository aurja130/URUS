# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Aurora Jahan
# Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.
"""Check the pinned toolchain and keep ``manifests/toolchain.txt`` in step with it.

Usage (from the repository root)::

    python3 -m tools.toolchain.check_toolchain                   # check only
    python3 -m tools.toolchain.check_toolchain --write-manifest  # check, then rewrite manifest

The check prints one line per tool (status, name, version, path, detail): minimum versions
for the C++ build, lint and test tools, the Python tooling, Universal Ctags, HDF5 with its
C++ API (through ``h5c++``) and OpenMC (nodal data generation). It reports the glibc version
and compares the regenerated manifest with the committed one.

Exit codes: 0 every check passed; 1 at least one check failed (nothing is written then);
2 command-line usage error (argparse).
"""

from __future__ import annotations

import argparse
import difflib
import os
import re
import shutil
import subprocess
import sys
from collections.abc import Sequence
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from enum import StrEnum
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_MANIFEST = REPO_ROOT / "manifests" / "toolchain.txt"

EXIT_OK = 0
EXIT_FAILED = 1

_TIMEOUT_S = 120
_NUMERIC_VERSION = r"(\d+(?:\.\d+)+)"
_MAX_DIFF_LINES = 12
_MAX_DIFF_WIDTH = 160


class Status(StrEnum):
    OK = "OK"
    FAIL = "FAIL"
    INFO = "INFO"
    SKIP = "SKIP"


@dataclass(frozen=True)
class ToolSpec:
    """A tool on PATH whose version output must report at least ``minimum``."""

    name: str
    command: str
    version_re: re.Pattern[str]
    minimum: tuple[int, ...]
    install_hint: str
    version_args: tuple[str, ...] = ("--version",)


@dataclass(frozen=True)
class CheckResult:
    status: Status
    name: str
    version: str
    path: str
    detail: str

    def line(self) -> str:
        return f"{self.status:<5} {self.name:<13} {self.version:<9} {self.path}  {self.detail}"


def _spec(name: str, pattern: str, minimum: tuple[int, ...], hint: str) -> ToolSpec:
    return ToolSpec(name, name, re.compile(pattern, re.MULTILINE), minimum, hint)


TOOL_SPECS: tuple[ToolSpec, ...] = (
    _spec("gcc", rf"^gcc \(GCC\) {_NUMERIC_VERSION}", (16,), "install GCC (dnf install gcc-c++)"),
    _spec(
        "clang",
        rf"^clang version {_NUMERIC_VERSION}",
        (22,),
        "install Clang (dnf install clang)",
    ),
    _spec("cmake", rf"^cmake version {_NUMERIC_VERSION}", (4, 0), "install CMake"),
    _spec("ninja", rf"^{_NUMERIC_VERSION}$", (1, 11), "install Ninja (dnf install ninja-build)"),
    # The interpreter running this check is the one the Python tooling uses.
    ToolSpec(
        "python",
        sys.executable,
        re.compile(rf"^Python {_NUMERIC_VERSION}", re.MULTILINE),
        (3, 13),
        "run the Python tooling with Python >= 3.13 (the pyenv environment in .python-version)",
    ),
    _spec("ruff", rf"^ruff {_NUMERIC_VERSION}", (0, 16), "pip install --user 'ruff>=0.16'"),
    _spec(
        "basedpyright",
        rf"^basedpyright {_NUMERIC_VERSION}",
        (1, 39),
        "pip install --user 'basedpyright>=1.39'",
    ),
    _spec("pytest", rf"^pytest {_NUMERIC_VERSION}", (9, 1), "pip install --user 'pytest>=9.1'"),
    _spec(
        "clang-tidy",
        rf"LLVM version {_NUMERIC_VERSION}",
        (22,),
        "install clang-tools-extra (dnf install clang-tools-extra)",
    ),
    _spec(
        "clang-format",
        rf"^clang-format version {_NUMERIC_VERSION}",
        (22,),
        "install clang-tools-extra (dnf install clang-tools-extra)",
    ),
    _spec(
        "ctags",
        rf"^Universal Ctags {_NUMERIC_VERSION}",
        (6, 0),
        "install Universal Ctags (dnf install ctags); Exuberant Ctags is not supported",
    ),
    # h5c++ ships with the HDF5 development files, so finding it also proves the C++ API.
    ToolSpec(
        "hdf5",
        "h5c++",
        re.compile(rf"HDF5 Version: {_NUMERIC_VERSION}", re.MULTILINE),
        (1, 14),
        "install HDF5 with its C++ API (dnf install hdf5 hdf5-devel)",
        ("-showconfig",),
    ),
    _spec(
        "openmc",
        rf"^OpenMC version {_NUMERIC_VERSION}",
        (0, 15),
        "install OpenMC (https://docs.openmc.org/en/latest/quickinstall.html)",
    ),
)


def parse_version(text: str) -> tuple[int, ...]:
    """``"16.2.1"`` -> ``(16, 2, 1)``."""
    if not re.fullmatch(r"\d+(?:\.\d+)*", text):
        raise ValueError(f"not a numeric version: {text!r}")
    return tuple(int(part) for part in text.split("."))


def version_at_least(version: tuple[int, ...], minimum: tuple[int, ...]) -> bool:
    """Compare versions, padding the shorter one with zeros."""
    width = max(len(version), len(minimum))
    return version + (0,) * (width - len(version)) >= minimum + (0,) * (width - len(minimum))


def format_version(version: tuple[int, ...]) -> str:
    return ".".join(str(part) for part in version)


def display_path(path: Path | str) -> str:
    """Path as recorded: repository-relative inside the repository, else ``~`` for home."""
    candidate = Path(path)
    if candidate.is_absolute() and candidate.is_relative_to(REPO_ROOT):
        return candidate.relative_to(REPO_ROOT).as_posix()
    return display_path_text(str(path))


def display_path_text(text: str) -> str:
    """Write the home directory as ``~`` wherever it occurs in ``text``."""
    home = str(Path.home())
    if text == home:
        return "~"
    return text.replace(home + os.sep, "~" + os.sep)


def _c_locale_env() -> dict[str, str]:
    env = dict(os.environ)
    env["LC_ALL"] = "C"
    return env


def _run(argv: Sequence[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        list(argv),
        capture_output=True,
        text=True,
        check=False,
        timeout=_TIMEOUT_S,
        env=_c_locale_env(),
    )


def evaluate_version_output(spec: ToolSpec, path: str, output: str) -> CheckResult:
    """Judge a tool's version output against its spec."""
    minimum = format_version(spec.minimum)
    match = spec.version_re.search(output)
    if match is None:
        first = output.strip().splitlines()[0] if output.strip() else "<no output>"
        return CheckResult(
            Status.FAIL,
            spec.name,
            "?",
            path,
            f"unrecognised version output {first!r}; {spec.install_hint}",
        )
    found = match.group(1)
    if not version_at_least(parse_version(found), spec.minimum):
        return CheckResult(
            Status.FAIL,
            spec.name,
            found,
            path,
            f"version {found} is older than the required >= {minimum}; {spec.install_hint}",
        )
    return CheckResult(Status.OK, spec.name, found, path, f">= {minimum}")


def check_tool(spec: ToolSpec) -> CheckResult:
    exe = shutil.which(spec.command)
    if exe is None:
        return CheckResult(
            Status.FAIL,
            spec.name,
            "-",
            "-",
            f"`{spec.command}` not found on PATH; {spec.install_hint}",
        )
    try:
        proc = _run([exe, *spec.version_args])
    except (OSError, subprocess.TimeoutExpired) as exc:
        return CheckResult(Status.FAIL, spec.name, "-", exe, f"cannot run: {exc}")
    return evaluate_version_output(spec, display_path(exe), proc.stdout + proc.stderr)


def glibc_version() -> str:
    """The glibc version of this process, e.g. ``"2.43"``."""
    value = os.confstr("CS_GNU_LIBC_VERSION")
    if not value or not value.startswith("glibc "):
        raise RuntimeError(f"not a glibc system (CS_GNU_LIBC_VERSION={value!r})")
    return value.removeprefix("glibc ")


def render_manifest(tools: Sequence[CheckResult], glibc: str) -> str:
    """The deterministic content of ``manifests/toolchain.txt``."""
    table = [f"{t.name:<13} {t.version:<9} {t.path}" for t in tools]
    lines = [
        "# Reactor simulator: pinned toolchain manifest.",
        "#",
        "# Records the versions of the build, lint, test and data-generation tools and the",
        "# glibc version.",
        "#",
        "# Generated by `python3 -m tools.toolchain.check_toolchain --write-manifest`;",
        "# do not edit by hand.",
        "# `python3 -m tools.toolchain.check_toolchain` regenerates this content from the live",
        "# toolchain and fails if it differs from this file. The home directory is written as ~.",
        "",
        "[tools]",
        f"{'name':<13} {'version':<9} path",
        *table,
        f"{'glibc':<13} {glibc}",
    ]
    return "\n".join(lines) + "\n"


def compare_manifest(manifest: Path, expected: str) -> tuple[CheckResult, list[str]]:
    """Compare the committed manifest with the live content; also returns the differing lines."""
    path = display_path(manifest)
    hint = "rerun with --write-manifest"
    if not manifest.is_file():
        return CheckResult(Status.FAIL, "manifest", "-", path, f"missing; {hint}"), []
    current = manifest.read_text(encoding="utf-8")
    if current == expected:
        ok = CheckResult(Status.OK, "manifest", "-", path, "up to date with the live toolchain")
        return ok, []
    diff = difflib.unified_diff(
        current.splitlines(), expected.splitlines(), "committed", "live", lineterm="", n=0
    )
    changed = [ln for ln in diff if ln[:1] in "+-" and not ln.startswith(("+++", "---"))]
    detail = f"out of date with the live toolchain ({len(changed)} differing lines); {hint}"
    return CheckResult(Status.FAIL, "manifest", "-", path, detail), changed


def run_checks(manifest: Path, write: bool, specs: Sequence[ToolSpec] = TOOL_SPECS) -> int:
    results: list[CheckResult] = []

    def emit(result: CheckResult) -> None:
        results.append(result)
        print(result.line(), flush=True)

    with ThreadPoolExecutor(max_workers=len(specs)) as pool:
        tool_results = list(pool.map(check_tool, specs))
    for result in tool_results:
        emit(result)

    glibc = glibc_version()
    emit(CheckResult(Status.INFO, "glibc", glibc, "-", "C library of this Python process"))

    failed = [r.name for r in results if r.status is Status.FAIL]
    if failed:
        emit(
            CheckResult(
                Status.SKIP,
                "manifest",
                "-",
                display_path(manifest),
                "not compared/written because other checks failed",
            )
        )
    else:
        content = render_manifest(tool_results, glibc)
        if write:
            manifest.parent.mkdir(parents=True, exist_ok=True)
            manifest.write_text(content, encoding="utf-8")
            print(f"wrote {display_path(manifest)}")
        comparison, changed = compare_manifest(manifest, content)
        emit(comparison)
        for line in changed[:_MAX_DIFF_LINES]:
            print(f"      {line[:_MAX_DIFF_WIDTH]}")
        if len(changed) > _MAX_DIFF_LINES:
            print(f"      ... {len(changed) - _MAX_DIFF_LINES} more differing lines")
        failed = [r.name for r in results if r.status is Status.FAIL]

    if failed:
        print(f"toolchain check FAILED: {', '.join(failed)}")
        return EXIT_FAILED
    print(f"toolchain check passed ({len(results)} checks)")
    return EXIT_OK


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="python3 -m tools.toolchain.check_toolchain",
        description="Check the pinned toolchain and the committed toolchain manifest.",
        epilog="exit codes: 0 all checks passed; 1 a check failed; 2 usage error",
    )
    parser.add_argument(
        "--write-manifest",
        action="store_true",
        help="(re)write the manifest from the live toolchain when every check passes",
    )
    parser.add_argument(
        "--manifest",
        type=Path,
        default=DEFAULT_MANIFEST,
        help="manifest path (default: <repo>/manifests/toolchain.txt)",
    )
    args = parser.parse_args(argv)
    manifest: Path = args.manifest
    write: bool = args.write_manifest
    return run_checks(manifest, write)


if __name__ == "__main__":
    sys.exit(main())
