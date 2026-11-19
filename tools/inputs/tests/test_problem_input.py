# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Aurora Jahan
# Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.
"""Round-trip and raw-layout tests for the problem input writer (format version 1)."""

from __future__ import annotations

import dataclasses
from pathlib import Path
from typing import cast

import h5py
import numpy as np
import pytest
from numpy.testing import assert_array_equal
from numpy.typing import NDArray

from tools.inputs import problem_input as pi

SHAPE = (2, 3, 4)  # composition map and node arrays, [z][y][x]
FACES = ("x_minus", "x_plus", "y_minus", "y_plus", "z_minus", "z_plus")
NODE_FIELDS = (
    "doppler_temperature_k",
    "moderator_density_kg_m3",
    "moderator_temperature_k",
    "boron_ppm",
)
VACUUM = pi.BoundaryCondition(pi.BoundaryKind.VACUUM)
LIBRARY = pi.LibraryBinding(
    library_file=Path("nodal_library.h5"),
    compositions=(
        pi.InputComposition(library_set="test_set", library_composition="fuel"),
        pi.InputComposition(library_set="test_set", library_composition="reflector"),
    ),
)
SOLVER_OPTION_CASES: list[tuple[str, object]] = [
    ("kernel", pi.KernelChoice.FINITE_DIFFERENCE),
    ("kernel", pi.KernelChoice.NEM),
    ("kernel", pi.KernelChoice.ANM),
    ("kernel", pi.KernelChoice.HYBRID),
    ("kernel_switch_threshold", 0.05),
    ("wielandt_shift", 0.1),
    ("eigenvalue_tolerance", 1.0e-7),
    ("fission_source_l2_tolerance", 1.0e-6),
    ("fission_source_linf_tolerance", 1.0e-5),
    ("inner_reduction", 0.001),
    ("coupling_relaxation", 0.5),
    ("inner_iteration_limit", 3),
    ("outer_iteration_limit", 50),
    ("nodal_update_period", 2),
    ("adjoint", False),
    ("adjoint", True),
]


def _zero_map() -> NDArray[np.int64]:
    return np.zeros(SHAPE, dtype=np.int64)


def _field(base: float) -> NDArray[np.float64]:
    """Distinct node values in [z][y][x] order, starting at `base`."""
    return base + 0.5 * np.arange(24, dtype=np.float64).reshape(SHAPE)


def _reactor(composition_map: NDArray[np.int64]) -> pi.ReactorDescription:
    """Vacuum on every bounding face, no exterior; widths follow the map shape."""
    nz, ny, nx = composition_map.shape
    return pi.ReactorDescription(
        width_x_cm=tuple(1.5 + 0.25 * i for i in range(nx)),
        width_y_cm=tuple(2.0 + 0.5 * j for j in range(ny)),
        width_z_cm=tuple(10.0 + k for k in range(nz)),
        composition_map=composition_map,
        x_minus=VACUUM,
        x_plus=VACUUM,
        y_minus=VACUUM,
        y_plus=VACUUM,
        z_minus=VACUUM,
        z_plus=VACUUM,
    )


def _scalar_state() -> pi.OperatingState:
    return pi.OperatingState(
        thermal_power_w=1.0e6,
        doppler_temperature_k=900.0,
        moderator_density_kg_m3=740.0,
        moderator_temperature_k=580.0,
        boron_ppm=0.0,
    )


def _problem(reactor: pi.ReactorDescription) -> pi.ProblemInput:
    return pi.ProblemInput(reactor=reactor, library=LIBRARY, operating_state=_scalar_state())


def _minimal_problem() -> pi.ProblemInput:
    return _problem(_reactor(_zero_map()))


def _full_problem() -> pi.ProblemInput:
    """Every member set: mixed faces, an exterior, array node states with a NaN."""
    composition_map = _zero_map()
    composition_map[1] = 1
    composition_map[0, 0, 0] = -1
    doppler = _field(900.0)
    doppler[0, 0, 0] = np.nan
    reactor = dataclasses.replace(
        _reactor(composition_map),
        x_minus=pi.BoundaryCondition(pi.BoundaryKind.ALBEDO, albedo=(0.25, 0.5)),
        x_plus=pi.BoundaryCondition(pi.BoundaryKind.REFLECTIVE),
        y_minus=pi.BoundaryCondition(pi.BoundaryKind.ALBEDO, albedo=(0.1, 0.2)),
        y_plus=pi.BoundaryCondition(pi.BoundaryKind.ZERO_FLUX),
        z_minus=pi.BoundaryCondition(pi.BoundaryKind.ALBEDO, albedo=(0.3, 0.4)),
        exterior=pi.BoundaryCondition(pi.BoundaryKind.ALBEDO, albedo=(0.0, 0.125)),
    )
    return pi.ProblemInput(
        reactor=reactor,
        library=LIBRARY,
        operating_state=pi.OperatingState(
            thermal_power_w=3.0e9,
            doppler_temperature_k=doppler,
            moderator_density_kg_m3=740.0,
            moderator_temperature_k=_field(580.0),
            boron_ppm=600.0,
        ),
        solver_options=pi.SolverOptions(
            kernel=pi.KernelChoice.NEM,
            kernel_switch_threshold=0.02,
            wielandt_shift=0.05,
            eigenvalue_tolerance=1.0e-6,
            fission_source_l2_tolerance=1.0e-5,
            fission_source_linf_tolerance=1.0e-4,
            inner_reduction=0.1,
            inner_iteration_limit=2,
            outer_iteration_limit=500,
            nodal_update_period=3,
            coupling_relaxation=0.8,
            adjoint=True,
        ),
    )


def _round_trip(tmp_path: Path, problem: pi.ProblemInput) -> pi.ProblemInput:
    path = tmp_path / "problem.h5"
    pi.write_problem_input(problem, path)
    return pi.read_problem_input(path)


def _faces(reactor: pi.ReactorDescription) -> tuple[pi.BoundaryCondition | None, ...]:
    return (
        reactor.x_minus,
        reactor.x_plus,
        reactor.y_minus,
        reactor.y_plus,
        reactor.z_minus,
        reactor.z_plus,
        reactor.exterior,
    )


def _assert_node_same(actual: pi.NodeValue, expected: pi.NodeValue) -> None:
    """An array must match in shape and dtype; a scalar must not match an array."""
    if isinstance(expected, np.ndarray):
        assert isinstance(actual, np.ndarray)
        assert_array_equal(actual, expected, strict=True)
    else:
        assert not isinstance(actual, np.ndarray)
        assert actual == expected


def _assert_same(actual: pi.ProblemInput, expected: pi.ProblemInput) -> None:
    """Compare field by field: `==` is ambiguous for the NumPy-valued fields."""
    got, want = actual.reactor, expected.reactor
    assert got.width_x_cm == want.width_x_cm
    assert got.width_y_cm == want.width_y_cm
    assert got.width_z_cm == want.width_z_cm
    assert_array_equal(got.composition_map, want.composition_map, strict=True)
    assert _faces(got) == _faces(want)
    assert actual.library == expected.library
    state, want_state = actual.operating_state, expected.operating_state
    assert state.thermal_power_w == want_state.thermal_power_w
    _assert_node_same(state.doppler_temperature_k, want_state.doppler_temperature_k)
    _assert_node_same(state.moderator_density_kg_m3, want_state.moderator_density_kg_m3)
    _assert_node_same(state.moderator_temperature_k, want_state.moderator_temperature_k)
    _assert_node_same(state.boron_ppm, want_state.boron_ppm)
    assert actual.solver_options == expected.solver_options


def _group(parent: h5py.Group, name: str) -> h5py.Group:
    member = parent[name]
    assert isinstance(member, h5py.Group)
    return member


def _dataset(parent: h5py.Group, name: str) -> h5py.Dataset:
    member = parent[name]
    assert isinstance(member, h5py.Dataset)
    return member


def _attr(member: h5py.Group | h5py.Dataset, name: str) -> object:
    value: object = member.attrs[name]
    return value


def _text(value: object) -> str:
    assert isinstance(value, bytes | str)
    return value.decode() if isinstance(value, bytes) else value


def _check_node_dataset(
    group: h5py.Group,
    name: str,
    shape: tuple[int, ...],
    units: str,
) -> None:
    dataset = _dataset(group, name)
    assert dataset.dtype == np.float64
    assert dataset.shape == shape
    assert _attr(dataset, "units") == units


def _scalar(dataset: h5py.Dataset) -> float:
    value = np.asarray(dataset[()])
    assert value.ndim == 0
    return float(value)


@pytest.mark.parametrize("kind", list(pi.BoundaryKind))
@pytest.mark.parametrize("face", FACES)
def test_every_face_and_kind_round_trips(
    tmp_path: Path,
    face: str,
    kind: pi.BoundaryKind,
) -> None:
    albedo = (0.25, 0.5) if kind is pi.BoundaryKind.ALBEDO else None
    condition = pi.BoundaryCondition(kind, albedo=albedo)
    expected = _problem(dataclasses.replace(_reactor(_zero_map()), **{face: condition}))
    _assert_same(_round_trip(tmp_path, expected), expected)


@pytest.mark.parametrize("kind", list(pi.BoundaryKind))
def test_every_exterior_kind_round_trips(tmp_path: Path, kind: pi.BoundaryKind) -> None:
    composition_map = _zero_map()
    composition_map[0, 0, 0] = -1
    albedo = (0.5, 0.0) if kind is pi.BoundaryKind.ALBEDO else None
    exterior = pi.BoundaryCondition(kind, albedo=albedo)
    expected = _problem(dataclasses.replace(_reactor(composition_map), exterior=exterior))
    _assert_same(_round_trip(tmp_path, expected), expected)


@pytest.mark.parametrize("field_name", NODE_FIELDS)
@pytest.mark.parametrize("array_valued", [False, True])
def test_node_state_round_trips_as_scalar_or_array(
    tmp_path: Path,
    field_name: str,
    array_valued: bool,
) -> None:
    value: pi.NodeValue = _field(700.0) if array_valued else 700.0
    state = dataclasses.replace(_scalar_state(), **{field_name: value})
    expected = dataclasses.replace(_minimal_problem(), operating_state=state)
    _assert_same(_round_trip(tmp_path, expected), expected)


@pytest.mark.parametrize(("option_name", "value"), SOLVER_OPTION_CASES)
def test_each_solver_option_round_trips_alone(
    tmp_path: Path,
    option_name: str,
    value: object,
) -> None:
    options = dataclasses.replace(pi.SolverOptions(), **{option_name: value})
    expected = dataclasses.replace(_minimal_problem(), solver_options=options)
    _assert_same(_round_trip(tmp_path, expected), expected)


def test_full_problem_round_trips(tmp_path: Path) -> None:
    expected = _full_problem()
    _assert_same(_round_trip(tmp_path, expected), expected)


def test_minimal_problem_round_trips_with_default_options(tmp_path: Path) -> None:
    expected = _minimal_problem()
    actual = _round_trip(tmp_path, expected)
    _assert_same(actual, expected)
    assert actual.reactor.exterior is None
    assert actual.solver_options == pi.SolverOptions()


def test_rewrite_replaces_the_previous_file(tmp_path: Path) -> None:
    path = tmp_path / "problem.h5"
    pi.write_problem_input(_full_problem(), path)
    expected = _minimal_problem()
    pi.write_problem_input(expected, path)
    with h5py.File(str(path), "r") as file:
        assert "solver_options" not in file
        assert "exterior" not in _group(file, "boundary_conditions")
    _assert_same(pi.read_problem_input(path), expected)


def test_composition_map_is_stored_as_z_y_x_with_x_fastest(tmp_path: Path) -> None:
    composition_map = np.arange(24, dtype=np.int64).reshape(4, 2, 3)
    path = tmp_path / "problem.h5"
    pi.write_problem_input(_problem(_reactor(composition_map)), path)
    with h5py.File(str(path), "r") as file:
        stored = _dataset(file, "geometry/composition_map")
        assert stored.shape == (4, 2, 3)
        assert_array_equal(np.asarray(stored[()]), composition_map)


def test_raw_layout_matches_format_version_1(tmp_path: Path) -> None:
    path = tmp_path / "problem.h5"
    pi.write_problem_input(_full_problem(), path)
    with h5py.File(str(path), "r") as file:
        assert _attr(file, "format_version") == 1
        assert set(file.keys()) == {
            "geometry",
            "boundary_conditions",
            "library",
            "compositions",
            "operating_state",
            "solver_options",
        }

        geometry = _group(file, "geometry")
        for name, length in (("width_x", 4), ("width_y", 3), ("width_z", 2)):
            width = _dataset(geometry, name)
            assert width.dtype == np.float64
            assert width.shape == (length,)
            assert _attr(width, "units") == "cm"
        composition_map = _dataset(geometry, "composition_map")
        assert composition_map.dtype.kind == "i"
        assert composition_map.shape == SHAPE
        assert "units" not in composition_map.attrs

        faces = _group(file, "boundary_conditions")
        assert set(faces.keys()) == {*FACES, "exterior"}
        x_minus = _group(faces, "x_minus")
        assert _attr(x_minus, "kind") == "albedo"
        albedo = _dataset(x_minus, "albedo")
        assert albedo.dtype == np.float64
        assert albedo.shape == (2,)
        assert _attr(albedo, "units") == "1"
        assert_array_equal(np.asarray(albedo[()]), [0.25, 0.5])
        assert _attr(_group(faces, "x_plus"), "kind") == "reflective"
        assert "albedo" not in _group(faces, "x_plus")
        assert _attr(_group(faces, "exterior"), "kind") == "albedo"

        assert _attr(_group(file, "library"), "path") == "nodal_library.h5"

        compositions = _group(file, "compositions")
        for name, expected_values in (
            ("library_set", ["test_set", "test_set"]),
            ("library_composition", ["fuel", "reflector"]),
        ):
            strings = _dataset(compositions, name)
            assert strings.dtype.kind == "O"
            assert strings.shape == (2,)
            assert [_text(value) for value in strings[()]] == expected_values

        state = _group(file, "operating_state")
        _check_node_dataset(state, "thermal_power", (), "W")
        _check_node_dataset(state, "doppler_temperature", SHAPE, "K")
        _check_node_dataset(state, "moderator_density", (), "kg m^-3")
        _check_node_dataset(state, "moderator_temperature", SHAPE, "K")
        _check_node_dataset(state, "boron", (), "ppm")

        options = _group(file, "solver_options")
        assert set(options.keys()) == {
            "kernel",
            "kernel_switch_threshold",
            "wielandt_shift",
            "eigenvalue_tolerance",
            "fission_source_l2_tolerance",
            "fission_source_linf_tolerance",
            "inner_reduction",
            "inner_iteration_limit",
            "outer_iteration_limit",
            "nodal_update_period",
            "coupling_relaxation",
            "adjoint",
        }
        assert _text(_dataset(options, "kernel")[()]) == "nem"
        assert _dataset(options, "adjoint").dtype.kind == "i"
        assert _dataset(options, "adjoint")[()] == 1
        assert _dataset(options, "outer_iteration_limit").dtype.kind == "i"
        assert _dataset(options, "wielandt_shift").dtype == np.float64
        for name in options:
            assert "units" not in _dataset(options, name).attrs


def test_int_and_float32_inputs_are_stored_as_float64(tmp_path: Path) -> None:
    reactor = dataclasses.replace(
        _reactor(_zero_map()),
        width_x_cm=(1, 2, 3, 4),
        width_y_cm=(2, 3, 4),
        width_z_cm=(10, 11),
        x_minus=pi.BoundaryCondition(pi.BoundaryKind.ALBEDO, albedo=(0, 1)),
    )
    doppler = cast(pi.NodeValue, _field(900.0).astype(np.float32))
    state = pi.OperatingState(
        thermal_power_w=1_000_000,
        doppler_temperature_k=doppler,
        moderator_density_kg_m3=740,
        moderator_temperature_k=580,
        boron_ppm=0,
    )
    problem = pi.ProblemInput(
        reactor=reactor,
        library=LIBRARY,
        operating_state=state,
        solver_options=pi.SolverOptions(coupling_relaxation=1),
    )
    path = tmp_path / "problem.h5"
    pi.write_problem_input(problem, path)
    with h5py.File(str(path), "r") as file:
        geometry = _group(file, "geometry")
        for name in ("width_x", "width_y", "width_z"):
            assert _dataset(geometry, name).dtype == np.dtype(np.float64)
        faces = _group(file, "boundary_conditions")
        albedo = _dataset(_group(faces, "x_minus"), "albedo")
        assert albedo.dtype == np.dtype(np.float64)
        operating_state = _group(file, "operating_state")
        for name in (
            "thermal_power",
            "doppler_temperature",
            "moderator_density",
            "moderator_temperature",
            "boron",
        ):
            assert _dataset(operating_state, name).dtype == np.dtype(np.float64)
        coupling = _dataset(_group(file, "solver_options"), "coupling_relaxation")
        assert coupling.dtype == np.dtype(np.float64)
    twin = dataclasses.replace(
        problem,
        operating_state=dataclasses.replace(state, doppler_temperature_k=_field(900.0)),
    )
    _assert_same(pi.read_problem_input(path), twin)


def test_full_problem_stores_each_value_under_its_contract_name(tmp_path: Path) -> None:
    path = tmp_path / "problem.h5"
    pi.write_problem_input(_full_problem(), path)
    with h5py.File(str(path), "r") as file:
        state = _group(file, "operating_state")
        doppler = _field(900.0)
        doppler[0, 0, 0] = np.nan  # excluded position: not read, may be NaN (contract §6)
        stored_doppler = np.asarray(_dataset(state, "doppler_temperature")[()])
        assert_array_equal(stored_doppler, doppler, strict=True)
        stored_moderator = np.asarray(_dataset(state, "moderator_temperature")[()])
        assert_array_equal(stored_moderator, _field(580.0), strict=True)
        assert _scalar(_dataset(state, "moderator_density")) == 740.0
        assert _scalar(_dataset(state, "boron")) == 600.0
        assert _scalar(_dataset(state, "thermal_power")) == 3.0e9

        faces = _group(file, "boundary_conditions")
        kinds = {
            "x_minus": "albedo",
            "x_plus": "reflective",
            "y_minus": "albedo",
            "y_plus": "zero_flux",
            "z_minus": "albedo",
            "z_plus": "vacuum",
            "exterior": "albedo",
        }
        for face, kind in kinds.items():
            assert _attr(_group(faces, face), "kind") == kind
        albedos = {
            "x_minus": (0.25, 0.5),
            "y_minus": (0.1, 0.2),
            "z_minus": (0.3, 0.4),
            "exterior": (0.0, 0.125),
        }
        for face, albedo in albedos.items():
            stored = np.asarray(_dataset(_group(faces, face), "albedo")[()])
            assert_array_equal(stored, albedo, strict=True)

        options = _group(file, "solver_options")
        assert _text(_dataset(options, "kernel")[()]) == "nem"
        for name, value in (
            ("kernel_switch_threshold", 0.02),
            ("wielandt_shift", 0.05),
            ("eigenvalue_tolerance", 1.0e-6),
            ("fission_source_l2_tolerance", 1.0e-5),
            ("fission_source_linf_tolerance", 1.0e-4),
            ("inner_reduction", 0.1),
            ("coupling_relaxation", 0.8),
            ("inner_iteration_limit", 2),
            ("outer_iteration_limit", 500),
            ("nodal_update_period", 3),
            ("adjoint", 1),
        ):
            assert _scalar(_dataset(options, name)) == value


def test_group_members_and_attributes_match_contract(tmp_path: Path) -> None:
    path = tmp_path / "problem.h5"
    pi.write_problem_input(_full_problem(), path)
    with h5py.File(str(path), "r") as file:
        assert set(file.attrs.keys()) == {"format_version"}
        geometry = _group(file, "geometry")
        assert set(geometry.keys()) == {"width_x", "width_y", "width_z", "composition_map"}
        library = _group(file, "library")
        assert set(library.attrs.keys()) == {"path"}
        assert set(library.keys()) == set()
        compositions = _group(file, "compositions")
        assert set(compositions.keys()) == {"library_set", "library_composition"}
        state = _group(file, "operating_state")
        assert set(state.keys()) == {
            "thermal_power",
            "doppler_temperature",
            "moderator_density",
            "moderator_temperature",
            "boron",
        }
        faces = _group(file, "boundary_conditions")
        albedo_faces = {"x_minus", "y_minus", "z_minus", "exterior"}
        for face in (*FACES, "exterior"):
            group = _group(faces, face)
            assert set(group.attrs.keys()) == {"kind"}
            expected: set[str] = {"albedo"} if face in albedo_faces else set()
            assert set(group.keys()) == expected


def test_unset_optional_members_are_omitted(tmp_path: Path) -> None:
    path = tmp_path / "problem.h5"
    pi.write_problem_input(_minimal_problem(), path)
    with h5py.File(str(path), "r") as file:
        assert "solver_options" not in file
        faces = _group(file, "boundary_conditions")
        assert "exterior" not in faces
        assert "albedo" not in _group(faces, "x_minus")


def test_missing_format_version_is_rejected(tmp_path: Path) -> None:
    path = tmp_path / "problem.h5"
    pi.write_problem_input(_minimal_problem(), path)
    with h5py.File(str(path), "r+") as file:
        del file.attrs["format_version"]
    with pytest.raises(ValueError, match="format_version"):
        pi.read_problem_input(path)


@pytest.mark.parametrize("version", [0, 2])
def test_unsupported_format_version_is_rejected(tmp_path: Path, version: int) -> None:
    path = tmp_path / "problem.h5"
    pi.write_problem_input(_minimal_problem(), path)
    with h5py.File(str(path), "r+") as file:
        file.attrs["format_version"] = version
    with pytest.raises(ValueError, match="format_version"):
        pi.read_problem_input(path)


def test_unknown_boundary_kind_is_rejected(tmp_path: Path) -> None:
    path = tmp_path / "problem.h5"
    pi.write_problem_input(_minimal_problem(), path)
    with h5py.File(str(path), "r+") as file:
        file["boundary_conditions/x_minus"].attrs["kind"] = "periodic"
    with pytest.raises(ValueError, match="periodic"):
        pi.read_problem_input(path)
