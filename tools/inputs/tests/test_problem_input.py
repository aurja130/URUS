# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Aurora Jahan
# Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.
"""Round-trip and raw-layout tests for the problem input (format version 2)."""

from __future__ import annotations

import dataclasses
import itertools
from collections.abc import Callable
from pathlib import Path
from typing import Protocol, cast

import h5py
import numpy as np
import pytest
from numpy.testing import assert_array_equal
from numpy.typing import NDArray

from tools.inputs import problem_input as pi

# B009: attribute access would fail basedpyright, since the stubs lack `ExternalLink`.
_EXTERNAL_LINK = cast(Callable[[str, str], object], getattr(h5py, "ExternalLink"))  # noqa: B009

SHAPE = (2, 3, 4)  # composition map and node arrays, [z][y][x]
FACES = ("x_minus", "x_plus", "y_minus", "y_plus", "z_minus", "z_plus")
NODE_FIELDS = (
    "doppler_temperature_k",
    "moderator_density_kg_m3",
    "moderator_temperature_k",
    "boron_ppm",
)
SETTINGS_KEYS = {
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
# Every subset of the four documents may be linked; the rest are written in place.
LINKED_CASES: list[tuple[pi.DocumentKind, ...]] = [
    subset
    for size in range(len(pi.DocumentKind) + 1)
    for subset in itertools.combinations(pi.DocumentKind, size)
]


class _LinkGroup(Protocol):
    """`h5py.Group.get` with `getlink`, which the installed h5py stubs omit."""

    def get(self, name: str, /, *, getlink: bool) -> object: ...


class _Link(Protocol):
    @property
    def filename(self) -> str: ...

    @property
    def path(self) -> str: ...


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
    """A problem input with the reactor and initial state documents only."""
    return pi.ProblemInput(
        reactor=pi.ReactorDocument(reactor=reactor, library=LIBRARY),
        initial_state=pi.InitialStateDocument(operating_state=_scalar_state()),
    )


def _minimal_problem() -> pi.ProblemInput:
    return _problem(_reactor(_zero_map()))


def _with_state(problem: pi.ProblemInput, state: pi.OperatingState) -> pi.ProblemInput:
    return dataclasses.replace(problem, initial_state=pi.InitialStateDocument(state))


def _with_settings(problem: pi.ProblemInput, settings: pi.SettingsDocument) -> pi.ProblemInput:
    return dataclasses.replace(problem, settings=settings)


def _full_problem() -> pi.ProblemInput:
    """Every document set: mixed faces, an exterior, array node states with a NaN."""
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
        reactor=pi.ReactorDocument(reactor=reactor, library=LIBRARY),
        initial_state=pi.InitialStateDocument(
            operating_state=pi.OperatingState(
                thermal_power_w=3.0e9,
                doppler_temperature_k=doppler,
                moderator_density_kg_m3=740.0,
                moderator_temperature_k=_field(580.0),
                boron_ppm=600.0,
            ),
        ),
        control_signals=pi.ControlSignalsDocument(),
        settings=pi.SettingsDocument(
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
            allow_table_clamps=True,
        ),
    )


def _round_trip(tmp_path: Path, problem: pi.ProblemInput) -> pi.ProblemInput:
    path = tmp_path / "problem.h5"
    pi.write_problem_input(problem, path)
    return pi.read_problem_input(path)


def _document_files(
    folder: Path,
    kinds: tuple[pi.DocumentKind, ...],
) -> dict[pi.DocumentKind, Path]:
    folder.mkdir(parents=True, exist_ok=True)
    return {kind: folder / f"{kind.value}.h5" for kind in kinds}


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
    got, want = actual.reactor.reactor, expected.reactor.reactor
    assert got.width_x_cm == want.width_x_cm
    assert got.width_y_cm == want.width_y_cm
    assert got.width_z_cm == want.width_z_cm
    assert_array_equal(got.composition_map, want.composition_map, strict=True)
    assert _faces(got) == _faces(want)
    assert actual.reactor.library == expected.reactor.library
    state = actual.initial_state.operating_state
    want_state = expected.initial_state.operating_state
    assert state.thermal_power_w == want_state.thermal_power_w
    _assert_node_same(state.doppler_temperature_k, want_state.doppler_temperature_k)
    _assert_node_same(state.moderator_density_kg_m3, want_state.moderator_density_kg_m3)
    _assert_node_same(state.moderator_temperature_k, want_state.moderator_temperature_k)
    _assert_node_same(state.boron_ppm, want_state.boron_ppm)
    assert actual.control_signals == expected.control_signals
    assert actual.settings == expected.settings


def _group(parent: h5py.Group, name: str) -> h5py.Group:
    member = parent[name]
    assert isinstance(member, h5py.Group)
    return member


def _dataset(parent: h5py.Group, name: str) -> h5py.Dataset:
    member = parent[name]
    assert isinstance(member, h5py.Dataset)
    return member


def _external_link(parent: h5py.Group, name: str) -> _Link:
    link = cast(_LinkGroup, parent).get(name, getlink=True)
    assert hasattr(link, "filename")
    return cast(_Link, link)


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
    expected = _with_state(_minimal_problem(), state)
    _assert_same(_round_trip(tmp_path, expected), expected)


@pytest.mark.parametrize(("option_name", "value"), SOLVER_OPTION_CASES)
def test_each_solver_option_round_trips_alone(
    tmp_path: Path,
    option_name: str,
    value: object,
) -> None:
    options = dataclasses.replace(pi.SolverOptions(), **{option_name: value})
    expected = _with_settings(_minimal_problem(), pi.SettingsDocument(solver_options=options))
    _assert_same(_round_trip(tmp_path, expected), expected)


def test_full_problem_round_trips(tmp_path: Path) -> None:
    expected = _full_problem()
    _assert_same(_round_trip(tmp_path, expected), expected)


def test_minimal_problem_round_trips_with_absent_optional_documents(tmp_path: Path) -> None:
    expected = _minimal_problem()
    actual = _round_trip(tmp_path, expected)
    _assert_same(actual, expected)
    assert actual.reactor.reactor.exterior is None
    assert actual.control_signals is None
    assert actual.settings is None


@pytest.mark.parametrize("allow", [None, False, True])
def test_allow_table_clamps_round_trips(tmp_path: Path, allow: bool | None) -> None:
    expected = _with_settings(_minimal_problem(), pi.SettingsDocument(allow_table_clamps=allow))
    actual = _round_trip(tmp_path, expected)
    assert actual.settings == expected.settings
    with h5py.File(str(tmp_path / "problem.h5"), "r") as file:
        settings = _group(file, "settings")
        if allow is None:
            assert "allow_table_clamps" not in settings
        else:
            assert _scalar(_dataset(settings, "allow_table_clamps")) == int(allow)


def test_empty_control_signals_round_trip(tmp_path: Path) -> None:
    expected = dataclasses.replace(
        _minimal_problem(),
        control_signals=pi.ControlSignalsDocument(),
    )
    actual = _round_trip(tmp_path, expected)
    assert actual.control_signals == pi.ControlSignalsDocument()
    with h5py.File(str(tmp_path / "problem.h5"), "r") as file:
        assert set(_group(file, "control_signals").keys()) == set()


def test_rewrite_replaces_the_previous_file(tmp_path: Path) -> None:
    path = tmp_path / "problem.h5"
    pi.write_problem_input(_full_problem(), path)
    expected = _minimal_problem()
    pi.write_problem_input(expected, path)
    with h5py.File(str(path), "r") as file:
        assert "settings" not in file
        assert "control_signals" not in file
        assert "exterior" not in _group(_group(file, "reactor"), "boundary_conditions")
    _assert_same(pi.read_problem_input(path), expected)


def test_composition_map_is_stored_as_z_y_x_with_x_fastest(tmp_path: Path) -> None:
    composition_map = np.arange(24, dtype=np.int64).reshape(4, 2, 3)
    path = tmp_path / "problem.h5"
    pi.write_problem_input(_problem(_reactor(composition_map)), path)
    with h5py.File(str(path), "r") as file:
        stored = _dataset(_group(_group(file, "reactor"), "geometry"), "composition_map")
        assert stored.shape == (4, 2, 3)
        assert_array_equal(np.asarray(stored[()]), composition_map)


def test_raw_layout_matches_format_version_2(tmp_path: Path) -> None:
    path = tmp_path / "problem.h5"
    pi.write_problem_input(_full_problem(), path)
    with h5py.File(str(path), "r") as file:
        assert _attr(file, "format_version") == 2
        assert set(file.keys()) == {"reactor", "initial_state", "control_signals", "settings"}

        reactor = _group(file, "reactor")
        assert _attr(reactor, "document") == "reactor"
        assert _attr(reactor, "format_version") == 2
        assert set(reactor.keys()) == {
            "geometry",
            "boundary_conditions",
            "library",
            "compositions",
        }

        geometry = _group(reactor, "geometry")
        for name, length in (("width_x", 4), ("width_y", 3), ("width_z", 2)):
            width = _dataset(geometry, name)
            assert width.dtype == np.float64
            assert width.shape == (length,)
            assert _attr(width, "units") == "cm"
        composition_map = _dataset(geometry, "composition_map")
        assert composition_map.dtype.kind == "i"
        assert composition_map.shape == SHAPE
        assert "units" not in composition_map.attrs

        faces = _group(reactor, "boundary_conditions")
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

        assert _attr(_group(reactor, "library"), "path") == "nodal_library.h5"

        compositions = _group(reactor, "compositions")
        for name, expected_values in (
            ("library_set", ["test_set", "test_set"]),
            ("library_composition", ["fuel", "reflector"]),
        ):
            strings = _dataset(compositions, name)
            assert strings.dtype.kind == "O"
            assert strings.shape == (2,)
            assert [_text(value) for value in strings[()]] == expected_values

        initial = _group(file, "initial_state")
        assert _attr(initial, "document") == "initial_state"
        assert _attr(initial, "format_version") == 2
        assert _attr(initial, "source") == "stated"
        _check_node_dataset(initial, "thermal_power", (), "W")
        _check_node_dataset(initial, "doppler_temperature", SHAPE, "K")
        _check_node_dataset(initial, "moderator_density", (), "kg m^-3")
        _check_node_dataset(initial, "moderator_temperature", SHAPE, "K")
        _check_node_dataset(initial, "boron", (), "ppm")

        control = _group(file, "control_signals")
        assert _attr(control, "document") == "control_signals"
        assert _attr(control, "format_version") == 2
        assert set(control.keys()) == set()

        settings = _group(file, "settings")
        assert _attr(settings, "document") == "settings"
        assert _attr(settings, "format_version") == 2
        assert set(settings.keys()) == {*SETTINGS_KEYS, "allow_table_clamps"}
        assert _text(_dataset(settings, "kernel")[()]) == "nem"
        assert _dataset(settings, "adjoint").dtype.kind == "i"
        assert _dataset(settings, "adjoint")[()] == 1
        assert _dataset(settings, "outer_iteration_limit").dtype.kind == "i"
        assert _dataset(settings, "wielandt_shift").dtype == np.float64
        assert _dataset(settings, "allow_table_clamps")[()] == 1
        for name in settings:
            assert "units" not in _dataset(settings, name).attrs


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
        reactor=pi.ReactorDocument(reactor=reactor, library=LIBRARY),
        initial_state=pi.InitialStateDocument(operating_state=state),
        settings=pi.SettingsDocument(solver_options=pi.SolverOptions(coupling_relaxation=1)),
    )
    path = tmp_path / "problem.h5"
    pi.write_problem_input(problem, path)
    with h5py.File(str(path), "r") as file:
        geometry = _group(_group(file, "reactor"), "geometry")
        for name in ("width_x", "width_y", "width_z"):
            assert _dataset(geometry, name).dtype == np.dtype(np.float64)
        faces = _group(_group(file, "reactor"), "boundary_conditions")
        albedo = _dataset(_group(faces, "x_minus"), "albedo")
        assert albedo.dtype == np.dtype(np.float64)
        initial = _group(file, "initial_state")
        for name in (
            "thermal_power",
            "doppler_temperature",
            "moderator_density",
            "moderator_temperature",
            "boron",
        ):
            assert _dataset(initial, name).dtype == np.dtype(np.float64)
        coupling = _dataset(_group(file, "settings"), "coupling_relaxation")
        assert coupling.dtype == np.dtype(np.float64)
    twin = dataclasses.replace(
        problem,
        initial_state=pi.InitialStateDocument(
            operating_state=dataclasses.replace(state, doppler_temperature_k=_field(900.0)),
        ),
    )
    _assert_same(pi.read_problem_input(path), twin)


def test_full_problem_stores_each_value_under_its_contract_name(tmp_path: Path) -> None:
    path = tmp_path / "problem.h5"
    pi.write_problem_input(_full_problem(), path)
    with h5py.File(str(path), "r") as file:
        initial = _group(file, "initial_state")
        doppler = _field(900.0)
        doppler[0, 0, 0] = np.nan  # excluded position: not read, may be NaN (contract §6)
        stored_doppler = np.asarray(_dataset(initial, "doppler_temperature")[()])
        assert_array_equal(stored_doppler, doppler, strict=True)
        stored_moderator = np.asarray(_dataset(initial, "moderator_temperature")[()])
        assert_array_equal(stored_moderator, _field(580.0), strict=True)
        assert _scalar(_dataset(initial, "moderator_density")) == 740.0
        assert _scalar(_dataset(initial, "boron")) == 600.0
        assert _scalar(_dataset(initial, "thermal_power")) == 3.0e9

        faces = _group(_group(file, "reactor"), "boundary_conditions")
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

        settings = _group(file, "settings")
        assert _text(_dataset(settings, "kernel")[()]) == "nem"
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
            ("allow_table_clamps", 1),
        ):
            assert _scalar(_dataset(settings, name)) == value


def test_group_members_and_attributes_match_contract(tmp_path: Path) -> None:
    path = tmp_path / "problem.h5"
    pi.write_problem_input(_full_problem(), path)
    with h5py.File(str(path), "r") as file:
        assert set(file.attrs.keys()) == {"format_version"}
        reactor = _group(file, "reactor")
        assert set(reactor.attrs.keys()) == {"document", "format_version"}
        geometry = _group(reactor, "geometry")
        assert set(geometry.keys()) == {"width_x", "width_y", "width_z", "composition_map"}
        library = _group(reactor, "library")
        assert set(library.attrs.keys()) == {"path"}
        assert set(library.keys()) == set()
        compositions = _group(reactor, "compositions")
        assert set(compositions.keys()) == {"library_set", "library_composition"}
        initial = _group(file, "initial_state")
        assert set(initial.attrs.keys()) == {"document", "format_version", "source"}
        assert set(initial.keys()) == {
            "thermal_power",
            "doppler_temperature",
            "moderator_density",
            "moderator_temperature",
            "boron",
        }
        control = _group(file, "control_signals")
        assert set(control.attrs.keys()) == {"document", "format_version"}
        settings = _group(file, "settings")
        assert set(settings.attrs.keys()) == {"document", "format_version"}
        faces = _group(reactor, "boundary_conditions")
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
        assert "settings" not in file
        assert "control_signals" not in file
        faces = _group(_group(file, "reactor"), "boundary_conditions")
        assert "exterior" not in faces
        assert "albedo" not in _group(faces, "x_minus")


def test_missing_format_version_is_rejected(tmp_path: Path) -> None:
    path = tmp_path / "problem.h5"
    pi.write_problem_input(_minimal_problem(), path)
    with h5py.File(str(path), "r+") as file:
        del file.attrs["format_version"]
    with pytest.raises(ValueError, match="format_version"):
        pi.read_problem_input(path)


@pytest.mark.parametrize("version", [0, 1, 3])
def test_unsupported_format_version_is_rejected(tmp_path: Path, version: int) -> None:
    path = tmp_path / "problem.h5"
    pi.write_problem_input(_minimal_problem(), path)
    with h5py.File(str(path), "r+") as file:
        file.attrs["format_version"] = version
    with pytest.raises(ValueError, match="format_version"):
        pi.read_problem_input(path)


@pytest.mark.parametrize("version", [1, 3])
def test_document_format_version_is_rejected(tmp_path: Path, version: int) -> None:
    path = tmp_path / "problem.h5"
    pi.write_problem_input(_minimal_problem(), path)
    with h5py.File(str(path), "r+") as file:
        _group(file, "initial_state").attrs["format_version"] = version
    with pytest.raises(ValueError, match="format_version"):
        pi.read_problem_input(path)


def test_document_of_the_wrong_kind_is_rejected(tmp_path: Path) -> None:
    path = tmp_path / "problem.h5"
    pi.write_problem_input(
        _with_settings(_minimal_problem(), pi.SettingsDocument(allow_table_clamps=True)),
        path,
    )
    with h5py.File(str(path), "r+") as file:
        _group(file, "settings").attrs["document"] = "reactor"
    with pytest.raises(ValueError, match="document kind 'reactor' is not 'settings'"):
        pi.read_problem_input(path)


def test_wrong_kind_in_a_linked_document_file_is_rejected(tmp_path: Path) -> None:
    path = tmp_path / "problem.h5"
    settings_file = tmp_path / "settings.h5"
    pi.write_problem_input(
        _with_settings(_minimal_problem(), pi.SettingsDocument(allow_table_clamps=True)),
        path,
        document_files={pi.DocumentKind.SETTINGS: settings_file},
    )
    with h5py.File(str(settings_file), "r+") as file:
        file.attrs["document"] = "reactor"
    with pytest.raises(ValueError, match="document kind 'reactor' is not 'settings'"):
        pi.read_problem_input(path)


@pytest.mark.parametrize("name", ["reactor", "initial_state"])
def test_missing_required_document_is_rejected(tmp_path: Path, name: str) -> None:
    path = tmp_path / "problem.h5"
    pi.write_problem_input(_minimal_problem(), path)
    with h5py.File(str(path), "r+") as file:
        del file[name]
    with pytest.raises(ValueError, match=f"{name} document is missing"):
        pi.read_problem_input(path)


def test_unknown_boundary_kind_is_rejected(tmp_path: Path) -> None:
    path = tmp_path / "problem.h5"
    pi.write_problem_input(_minimal_problem(), path)
    with h5py.File(str(path), "r+") as file:
        file["reactor/boundary_conditions/x_minus"].attrs["kind"] = "periodic"
    with pytest.raises(ValueError, match="periodic"):
        pi.read_problem_input(path)


def test_document_file_for_an_absent_document_is_rejected(tmp_path: Path) -> None:
    path = tmp_path / "problem.h5"
    settings_file = tmp_path / "settings.h5"
    with pytest.raises(ValueError, match="settings"):
        pi.write_problem_input(
            _minimal_problem(),
            path,
            document_files={pi.DocumentKind.SETTINGS: settings_file},
        )
    assert not path.exists()
    assert not settings_file.exists()


@pytest.mark.parametrize("linked", LINKED_CASES)
def test_linked_and_mixed_forms_read_the_same_problem(
    tmp_path: Path,
    linked: tuple[pi.DocumentKind, ...],
) -> None:
    expected = _full_problem()
    path = tmp_path / "problem.h5"
    files = _document_files(tmp_path / "documents", linked)
    pi.write_problem_input(expected, path, document_files=files)
    _assert_same(pi.read_problem_input(path), expected)
    with h5py.File(str(path), "r") as file:
        for kind in pi.DocumentKind:
            if kind in files:
                link = _external_link(file, kind.value)
                assert link.filename == f"documents/{kind.value}.h5"
                assert link.path == "/"
                assert files[kind].is_file()
            else:
                assert _attr(_group(file, kind.value), "document") == kind.value


def test_linked_document_files_hold_each_document_at_their_root(tmp_path: Path) -> None:
    files = _document_files(tmp_path / "documents", tuple(pi.DocumentKind))
    pi.write_problem_input(_full_problem(), tmp_path / "problem.h5", document_files=files)
    for kind, document_file in files.items():
        with h5py.File(str(document_file), "r") as file:
            assert _attr(file, "document") == kind.value
            assert _attr(file, "format_version") == 2
    with h5py.File(str(files[pi.DocumentKind.REACTOR]), "r") as file:
        assert set(file.keys()) == {"geometry", "boundary_conditions", "library", "compositions"}
    with h5py.File(str(files[pi.DocumentKind.INITIAL_STATE]), "r") as file:
        assert _attr(file, "source") == "stated"
        assert "thermal_power" in file
    with h5py.File(str(files[pi.DocumentKind.SETTINGS]), "r") as file:
        assert "kernel" in file
        assert "allow_table_clamps" in file
    with h5py.File(str(files[pi.DocumentKind.CONTROL_SIGNALS]), "r") as file:
        assert len(file) == 0


def test_one_reactor_document_serves_two_problem_inputs(tmp_path: Path) -> None:
    shared = tmp_path / "shared"
    shared.mkdir()
    reactor_file = shared / "reactor.h5"
    full = _full_problem()
    pi.write_document(full.reactor, reactor_file)
    cold = _scalar_state()
    hot = dataclasses.replace(cold, thermal_power_w=3.0e9)
    for case, state in (("cold", cold), ("hot", hot)):
        folder = tmp_path / case
        folder.mkdir()
        state_file = folder / "initial_state.h5"
        pi.write_document(pi.InitialStateDocument(operating_state=state), state_file)
        problem_path = folder / "problem.h5"
        pi.link_problem_input(
            problem_path,
            {
                pi.DocumentKind.REACTOR: reactor_file,
                pi.DocumentKind.INITIAL_STATE: state_file,
            },
        )
        with h5py.File(str(problem_path), "r") as file:
            assert _external_link(file, "reactor").filename == "../shared/reactor.h5"
        actual = pi.read_problem_input(problem_path)
        assert actual.initial_state.operating_state.thermal_power_w == state.thermal_power_w
        _assert_same(
            actual,
            pi.ProblemInput(
                reactor=full.reactor,
                initial_state=pi.InitialStateDocument(operating_state=state),
            ),
        )


def test_write_document_writes_the_document_at_the_file_root(tmp_path: Path) -> None:
    path = tmp_path / "initial_state.h5"
    pi.write_document(_full_problem().initial_state, path)
    with h5py.File(str(path), "r") as file:
        assert _attr(file, "document") == "initial_state"
        assert _attr(file, "format_version") == 2
        assert _attr(file, "source") == "stated"
        assert set(file.keys()) == {
            "thermal_power",
            "doppler_temperature",
            "moderator_density",
            "moderator_temperature",
            "boron",
        }


def test_linked_problem_reads_from_any_working_directory(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.chdir(tmp_path)
    expected = _full_problem()
    files = _document_files(Path("case/documents"), tuple(pi.DocumentKind))
    pi.write_problem_input(expected, Path("case/problem.h5"), document_files=files)
    elsewhere = tmp_path / "elsewhere"
    elsewhere.mkdir()
    monkeypatch.chdir(elsewhere)
    _assert_same(pi.read_problem_input(tmp_path / "case" / "problem.h5"), expected)


def _decoy_reactor() -> pi.ReactorDocument:
    """The full problem's reactor with other widths, to tell a searched copy from the real one."""
    real = _full_problem().reactor
    return pi.ReactorDocument(
        reactor=dataclasses.replace(
            real.reactor,
            width_x_cm=tuple(9.0 for _ in real.reactor.width_x_cm),
        ),
        library=real.library,
    )


def test_reader_resolves_links_itself_not_through_the_hdf5_search_path(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    expected = _full_problem()
    case = tmp_path / "case"
    files = _document_files(case / "documents", (pi.DocumentKind.REACTOR,))
    pi.write_problem_input(expected, case / "problem.h5", document_files=files)
    (tmp_path / "prefix" / "documents").mkdir(parents=True)
    pi.write_document(_decoy_reactor(), tmp_path / "prefix" / "documents" / "reactor.h5")
    monkeypatch.setenv("HDF5_EXT_PREFIX", str(tmp_path / "prefix"))
    _assert_same(pi.read_problem_input(case / "problem.h5"), expected)


def test_missing_linked_file_is_not_searched_for_in_the_working_directory(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    case = tmp_path / "case"
    files = _document_files(case / "documents", (pi.DocumentKind.REACTOR,))
    pi.write_problem_input(_full_problem(), case / "problem.h5", document_files=files)
    files[pi.DocumentKind.REACTOR].unlink()
    cwd = tmp_path / "cwd"
    (cwd / "documents").mkdir(parents=True)
    pi.write_document(_decoy_reactor(), cwd / "documents" / "reactor.h5")
    monkeypatch.delenv("HDF5_EXT_PREFIX", raising=False)
    monkeypatch.chdir(cwd)
    with pytest.raises(FileNotFoundError):
        pi.read_problem_input(case / "problem.h5")


def test_symlinked_problem_directory_gets_links_that_resolve(tmp_path: Path) -> None:
    expected = _full_problem()
    (tmp_path / "scratch" / "runs" / "case").mkdir(parents=True)
    (tmp_path / "home").mkdir()
    (tmp_path / "home" / "runs").symlink_to(
        tmp_path / "scratch" / "runs",
        target_is_directory=True,
    )
    reactor_file = tmp_path / "home" / "reactors" / "reactor.h5"
    reactor_file.parent.mkdir()
    problem_path = tmp_path / "home" / "runs" / "case" / "problem.h5"
    pi.write_problem_input(
        expected,
        problem_path,
        document_files={pi.DocumentKind.REACTOR: reactor_file},
    )
    _assert_same(pi.read_problem_input(problem_path), expected)


def test_two_kinds_naming_one_file_are_rejected(tmp_path: Path) -> None:
    shared = tmp_path / "shared.h5"
    path = tmp_path / "problem.h5"
    with pytest.raises(ValueError, match="same file"):
        pi.write_problem_input(
            _full_problem(),
            path,
            document_files={pi.DocumentKind.REACTOR: shared, pi.DocumentKind.SETTINGS: shared},
        )
    assert not path.exists()
    assert not shared.exists()


def test_document_root_without_its_document_attribute_is_rejected(tmp_path: Path) -> None:
    path = tmp_path / "problem.h5"
    pi.write_problem_input(_minimal_problem(), path)
    with h5py.File(str(path), "r+") as file:
        del _group(file, "initial_state").attrs["document"]
    with pytest.raises(ValueError):
        pi.read_problem_input(path)


def test_link_to_a_member_other_than_the_root_is_rejected(tmp_path: Path) -> None:
    path = tmp_path / "problem.h5"
    pi.write_problem_input(_minimal_problem(), path)
    reactor_file = tmp_path / "documents" / "reactor.h5"
    reactor_file.parent.mkdir()
    pi.write_document(_full_problem().reactor, reactor_file)
    with h5py.File(str(path), "r+") as file:
        del file["reactor"]
        file["reactor"] = _EXTERNAL_LINK("documents/reactor.h5", "/reactor")
    with pytest.raises(ValueError, match="must name the root group"):
        pi.read_problem_input(path)
