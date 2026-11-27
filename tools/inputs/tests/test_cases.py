# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Aurora Jahan
# Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.
"""The named example cases write and read back with the settings each one is meant to test."""

from __future__ import annotations

from pathlib import Path
from typing import Protocol, cast

import h5py
import numpy as np
import pytest

from tools.inputs import cases
from tools.inputs import problem_input as pi


class _LinkGroup(Protocol):
    """`h5py.Group.get` with `getlink`, which the installed h5py stubs omit."""

    def get(self, name: str, /, *, getlink: bool) -> object: ...


class _Link(Protocol):
    @property
    def filename(self) -> str: ...


def _library(tmp_path: Path) -> Path:
    return tmp_path / "library.h5"


def _write(tmp_path: Path, name: str) -> pi.ProblemInput:
    path = cases.write_case(name, tmp_path / name, _library(tmp_path))
    return pi.read_problem_input(path)


def _settings(problem: pi.ProblemInput) -> pi.SolverOptions:
    assert problem.settings is not None
    return problem.settings.solver_options


def _assert_common_reactor(problem: pi.ProblemInput, library: Path) -> None:
    reactor = problem.reactor.reactor
    assert problem.reactor.library.library_file == library
    assert problem.reactor.library.compositions == (
        pi.InputComposition(library_set="test_set", library_composition="fuel"),
        pi.InputComposition(library_set="test_set", library_composition="reflector"),
    )
    assert reactor.width_x_cm == (10.0, 10.0, 10.0)
    assert reactor.width_y_cm == (10.0, 10.0, 10.0)
    assert reactor.width_z_cm == (15.0, 15.0)
    expected_map = np.zeros((2, 3, 3), dtype=np.int64)
    expected_map[1, 2, 2] = 1
    np.testing.assert_array_equal(reactor.composition_map, expected_map)
    assert reactor.x_minus.kind == pi.BoundaryKind.REFLECTIVE
    assert reactor.y_minus.kind == pi.BoundaryKind.REFLECTIVE
    assert reactor.z_minus.kind == pi.BoundaryKind.REFLECTIVE
    assert reactor.x_plus.kind == pi.BoundaryKind.VACUUM
    assert reactor.y_plus.kind == pi.BoundaryKind.VACUUM
    assert reactor.z_plus.kind == pi.BoundaryKind.VACUUM
    assert reactor.exterior is None


@pytest.mark.parametrize("name", sorted(cases.CASES))
def test_every_case_writes_and_reads_back(tmp_path: Path, name: str) -> None:
    problem = _write(tmp_path, name)
    assert problem.reactor.library.library_file == _library(tmp_path)
    assert problem.initial_state.operating_state.boron_ppm == cases.BORON_PPM


def test_unknown_case_is_rejected(tmp_path: Path) -> None:
    with pytest.raises(ValueError, match="unknown case 'nope'"):
        cases.write_case("nope", tmp_path, _library(tmp_path))


def test_valid_uses_finite_difference_and_states_inside_tables(tmp_path: Path) -> None:
    problem = _write(tmp_path, "valid")
    _assert_common_reactor(problem, _library(tmp_path))
    assert _settings(problem).kernel == pi.KernelChoice.FINITE_DIFFERENCE
    assert _settings(problem).adjoint is None
    assert problem.settings is not None
    assert problem.settings.allow_table_clamps is None
    assert problem.control_signals is None
    state = problem.initial_state.operating_state
    assert state.thermal_power_w == cases.THERMAL_POWER_W
    assert state.doppler_temperature_k == cases.FUEL_TEMPERATURE_K
    assert state.boron_ppm == cases.BORON_PPM
    assert state.moderator_density_kg_m3 == cases.MODERATOR_DENSITY_KG_M3
    assert state.moderator_temperature_k == cases.MODERATOR_TEMPERATURE_K


def test_linked_moves_initial_state_and_settings_to_document_files(tmp_path: Path) -> None:
    problem = _write(tmp_path, "linked")
    output = tmp_path / "linked"
    _assert_common_reactor(problem, _library(tmp_path))
    assert _settings(problem).kernel == pi.KernelChoice.FINITE_DIFFERENCE
    assert problem.initial_state.operating_state.doppler_temperature_k == (cases.FUEL_TEMPERATURE_K)
    with h5py.File(str(output / cases.PROBLEM_FILE), "r") as raw:
        group = cast(_LinkGroup, raw)
        for name in ("initial_state", "settings"):
            link = cast(_Link, group.get(name, getlink=True))
            assert link.filename == f"{name}.h5"
            assert (output / f"{name}.h5").is_file()
        assert "control_signals" not in raw


def test_default_kernel_has_no_settings_document(tmp_path: Path) -> None:
    problem = _write(tmp_path, "default_kernel")
    assert problem.settings is None


def test_adjoint_asks_for_the_adjoint_with_finite_difference(tmp_path: Path) -> None:
    problem = _write(tmp_path, "adjoint")
    assert _settings(problem).kernel == pi.KernelChoice.FINITE_DIFFERENCE
    assert _settings(problem).adjoint is True


def test_invalid_has_non_positive_thermal_power(tmp_path: Path) -> None:
    problem = _write(tmp_path, "invalid")
    assert problem.initial_state.operating_state.thermal_power_w == 0.0


def test_not_converged_limits_the_outer_iterations_with_tight_tolerances(tmp_path: Path) -> None:
    problem = _write(tmp_path, "not_converged")
    options = _settings(problem)
    assert options.kernel == pi.KernelChoice.FINITE_DIFFERENCE
    assert options.outer_iteration_limit == 1
    assert options.eigenvalue_tolerance == cases.VERIFICATION_EIGENVALUE_TOLERANCE
    assert options.fission_source_l2_tolerance == cases.VERIFICATION_L2_TOLERANCE
    assert options.fission_source_linf_tolerance == cases.VERIFICATION_LINF_TOLERANCE


def test_clamped_allows_clamps_and_has_one_node_above_the_fuel_temperature(
    tmp_path: Path,
) -> None:
    problem = _write(tmp_path, "clamped")
    assert problem.settings is not None
    assert problem.settings.allow_table_clamps is True
    assert _settings(problem).kernel == pi.KernelChoice.FINITE_DIFFERENCE
    doppler = problem.initial_state.operating_state.doppler_temperature_k
    assert isinstance(doppler, np.ndarray)
    assert doppler.shape == (2, 3, 3)
    expected = np.full((2, 3, 3), cases.FUEL_TEMPERATURE_K)
    expected[0, 0, 0] = cases.CLAMPED_TEMPERATURE_K
    np.testing.assert_array_equal(doppler, expected)


def test_linked_adjoint_links_only_the_settings_document(tmp_path: Path) -> None:
    problem = _write(tmp_path, "linked_adjoint")
    output = tmp_path / "linked_adjoint"
    assert _settings(problem).kernel == pi.KernelChoice.FINITE_DIFFERENCE
    assert _settings(problem).adjoint is True
    with h5py.File(str(output / cases.PROBLEM_FILE), "r") as raw:
        group = cast(_LinkGroup, raw)
        link = cast(_Link, group.get("settings", getlink=True))
        assert link.filename == "settings.h5"
        assert "initial_state" in raw
    with h5py.File(str(output / "settings.h5"), "r") as document:
        adjoint = document["adjoint"]
        assert isinstance(adjoint, h5py.Dataset)
        assert int(cast(np.integer, adjoint[()])) == 1


def test_wielandt_shift_is_below_the_floor_the_engine_rejects(tmp_path: Path) -> None:
    problem = _write(tmp_path, "wielandt_shift")
    assert _settings(problem).kernel == pi.KernelChoice.FINITE_DIFFERENCE
    assert _settings(problem).wielandt_shift == cases.WIELANDT_SHIFT_TOO_SMALL


def test_cli_writes_the_case_and_takes_the_library_as_given(tmp_path: Path) -> None:
    library = Path("nodal_library.h5")  # relative: resolved against the output directory
    output = tmp_path / "cli"
    assert cases.main(["linked", str(output), "--library", str(library)]) == 0
    problem = pi.read_problem_input(output / cases.PROBLEM_FILE)
    assert problem.reactor.library.library_file == library
    assert (output / "initial_state.h5").is_file()
    assert (output / "settings.h5").is_file()
