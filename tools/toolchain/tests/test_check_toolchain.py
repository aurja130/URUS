# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Aurora Jahan
# Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.
"""Tests for tools.toolchain.check_toolchain."""

from __future__ import annotations

import subprocess
from collections.abc import Sequence
from pathlib import Path

import pytest

from tools.toolchain import check_toolchain as ct

# Version output as printed by the tools on the reference machine (h5c++: excerpt).
SAMPLE_OUTPUTS = {
    "gcc": ("gcc (GCC) 16.2.1 20261029 (Red Hat 16.2.1-3)\nCopyright (C) 2026 FSF\n", "16.2.1"),
    "clang": ("clang version 22.1.8 (Fedora 22.1.8-4.fc44)\nTarget: x86_64\n", "22.1.8"),
    "cmake": ("cmake version 4.3.0\n\nCMake suite maintained by Kitware\n", "4.3.0"),
    "ninja": ("1.13.2\n", "1.13.2"),
    "python": ("Python 3.13.12\n", "3.13.12"),
    "ruff": ("ruff 0.16.6\n", "0.16.6"),
    "basedpyright": ("basedpyright 1.39.10\nbased on pyright 1.1.412\n", "1.39.10"),
    "pytest": ("pytest 9.1.1\n", "9.1.1"),
    "clang-tidy": (
        "LLVM (http://llvm.org/):\n  LLVM version 22.1.8\n  Optimized build.\n",
        "22.1.8",
    ),
    "clang-format": ("clang-format version 22.1.8 (Fedora 22.1.8-4.fc44)\n", "22.1.8"),
    "ctags": ("Universal Ctags 6.2.1, Copyright (C) 2015-2025 Universal Ctags Team\n", "6.2.1"),
    "hdf5": (
        "\t    SUMMARY OF THE HDF5 CONFIGURATION\n"
        "\t    =================================\n\n"
        "General Information:\n-------------------\n"
        "                   HDF5 Version: 1.14.6\n",
        "1.14.6",
    ),
    "openmc": (
        "OpenMC version 0.15.4-dev94\nCommit hash: 417343c9203c9337547d1eeeefb8a9fd0ec536fd\n",
        "0.15.4",
    ),
    "gtest": ("1.17.0\n", "1.17.0"),
    "eigen": ("5.0.1\n", "5.0.1"),
}


def _spec(name: str) -> ct.ToolSpec:
    return next(spec for spec in ct.TOOL_SPECS if spec.name == name)


def test_parse_version() -> None:
    assert ct.parse_version("16.2.1") == (16, 2, 1)
    assert ct.parse_version("4") == (4,)
    with pytest.raises(ValueError, match="not a numeric version"):
        ct.parse_version("1.10.1rc1")


@pytest.mark.parametrize(
    ("version", "minimum", "expected"),
    [
        ((16, 2, 1), (16,), True),
        ((15, 9, 9), (16,), False),
        ((4, 0), (4, 0), True),
        ((4,), (4, 0), True),
        ((1, 38, 9), (1, 39), False),
        ((1, 39, 10), (1, 39), True),
        ((0, 16, 6), (0, 16), True),
        ((0, 9), (0, 16), False),
    ],
)
def test_version_at_least(
    version: tuple[int, ...], minimum: tuple[int, ...], expected: bool
) -> None:
    assert ct.version_at_least(version, minimum) is expected


def test_every_tool_has_a_sample() -> None:
    assert {spec.name for spec in ct.TOOL_SPECS} == set(SAMPLE_OUTPUTS)


@pytest.mark.parametrize("name", sorted(SAMPLE_OUTPUTS))
def test_version_output_parsed(name: str) -> None:
    output, version = SAMPLE_OUTPUTS[name]
    result = ct.evaluate_version_output(_spec(name), "/usr/bin/x", output)
    assert result.status is ct.Status.OK
    assert result.version == version


def test_too_old_version_fails_with_hint() -> None:
    result = ct.evaluate_version_output(_spec("cmake"), "/usr/bin/cmake", "cmake version 3.31.6\n")
    assert result.status is ct.Status.FAIL
    assert "3.31.6 is older than the required >= 4.0" in result.detail
    assert "install CMake" in result.detail


def test_exuberant_ctags_is_rejected() -> None:
    output = "Exuberant Ctags 5.8, Copyright (C) 1996-2009 Darren Hiebert\n"
    result = ct.evaluate_version_output(_spec("ctags"), "/usr/bin/ctags", output)
    assert result.status is ct.Status.FAIL
    assert "Universal Ctags" in result.detail
    assert "Exuberant Ctags 5.8" in result.detail


def test_h5cxx_without_hdf5_version_fails() -> None:
    # h5c++ also answers --version, with the wrapped compiler's version; that is not HDF5's.
    output = "g++ (GCC) 16.2.1 20261029 (Red Hat 16.2.1-3)\n"
    result = ct.evaluate_version_output(_spec("hdf5"), "/usr/bin/h5c++", output)
    assert result.status is ct.Status.FAIL
    assert "hdf5-devel" in result.detail


def test_missing_tool_fails() -> None:
    spec = ct.ToolSpec("nosuch", "sim-no-such-tool-xyz", _spec("ruff").version_re, (1,), "hint")
    result = ct.check_tool(spec)
    assert result.status is ct.Status.FAIL
    assert "not found on PATH" in result.detail


def test_failed_check_writes_no_manifest(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    spec = ct.ToolSpec("nosuch", "sim-no-such-tool-xyz", _spec("ruff").version_re, (1,), "hint")
    manifest = tmp_path / "toolchain.txt"
    assert ct.run_checks(manifest, write=True, specs=(spec,)) == ct.EXIT_FAILED
    out = capsys.readouterr().out
    assert "FAIL  nosuch" in out
    assert "toolchain check FAILED: nosuch" in out
    assert not manifest.exists()


def test_compare_manifest(tmp_path: Path) -> None:
    manifest = tmp_path / "toolchain.txt"
    missing, _ = ct.compare_manifest(manifest, "a\nb\n")
    assert missing.status is ct.Status.FAIL
    assert "--write-manifest" in missing.detail
    manifest.write_text("a\nb\n", encoding="utf-8")
    same, diff = ct.compare_manifest(manifest, "a\nb\n")
    assert same.status is ct.Status.OK
    assert diff == []
    stale, diff = ct.compare_manifest(manifest, "a\nc\n")
    assert stale.status is ct.Status.FAIL
    assert "out of date" in stale.detail
    assert "rerun with --write-manifest" in stale.detail
    assert diff == ["-b", "+c"]


def test_display_path() -> None:
    assert ct.display_path(Path.home() / "x" / "y") == "~/x/y"
    assert (
        ct.display_path(ct.REPO_ROOT / "manifests" / "toolchain.txt") == "manifests/toolchain.txt"
    )
    assert ct.display_path("/usr/bin/gcc") == "/usr/bin/gcc"


def test_unknown_pkg_config_package_fails_with_hint() -> None:
    result = ct.evaluate_version_output(
        _spec("gtest"), "/usr/bin/pkg-config", "Package 'gtest' not found\n"
    )
    assert result.status is ct.Status.FAIL
    assert result.detail.startswith("unrecognised version output")
    assert "install GoogleTest (dnf install gtest-devel)" in result.detail


def test_unknown_pkg_config_package_fails_through_check_tool(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    def fake_which(_command: str) -> str:
        return "/usr/bin/pkg-config"

    def fake_run(argv: Sequence[str]) -> subprocess.CompletedProcess[str]:
        return subprocess.CompletedProcess(
            list(argv), 1, "Package 'sim-no-such-package-xyz' was not found\n", ""
        )

    monkeypatch.setattr(ct.shutil, "which", fake_which)
    monkeypatch.setattr(ct, "_run", fake_run)
    spec = ct.ToolSpec(
        "nosuch",
        "pkg-config",
        _spec("gtest").version_re,
        (1,),
        "install nosuch",
        ("--modversion", "sim-no-such-package-xyz"),
    )
    result = ct.check_tool(spec)
    assert result.status is ct.Status.FAIL
    assert result.detail.startswith("unrecognised version output")
    assert "install nosuch" in result.detail
