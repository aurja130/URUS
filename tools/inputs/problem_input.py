# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Aurora Jahan
# Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.
"""Problem input files, format version 1 (docs/contracts/problem_input.md).

`write_problem_input` writes a `ProblemInput` without validating it: the engine
(src/nts/io/problem.hpp) reads and checks every file and is the authority.

Node-state fields and the composition map are NumPy arrays, so `==` on
`ProblemInput` or `ReactorDescription` is ambiguous. Compare those fields with
`numpy.testing`.
"""

from __future__ import annotations

from collections.abc import Iterable
from dataclasses import dataclass, field
from enum import StrEnum
from pathlib import Path
from typing import Protocol, cast

import h5py
import numpy as np
from numpy.typing import NDArray

FORMAT_VERSION = 1  # contract section 2

type NodeValue = float | NDArray[np.float64]


class BoundaryKind(StrEnum):
    """Kind of a face condition (contract section 4)."""

    REFLECTIVE = "reflective"
    VACUUM = "vacuum"
    ZERO_FLUX = "zero_flux"
    ALBEDO = "albedo"


class KernelChoice(StrEnum):
    """Nodal kernel of the solver options (contract section 7)."""

    FINITE_DIFFERENCE = "finite_difference"
    NEM = "nem"
    ANM = "anm"
    HYBRID = "hybrid"


@dataclass(frozen=True)
class BoundaryCondition:
    """Condition of one face. `albedo` is (fast, thermal) and is written when set."""

    kind: BoundaryKind
    albedo: tuple[float, float] | None = None


@dataclass(frozen=True)
class ReactorDescription:
    """Geometry, composition map and face conditions (contract sections 3 and 4).

    `composition_map[k, j, i]` is the input composition at x index i, y index j and
    z index k, or -1 for an excluded position. `exterior` is required by the engine
    when the map excludes a position.
    """

    width_x_cm: tuple[float, ...]
    width_y_cm: tuple[float, ...]
    width_z_cm: tuple[float, ...]
    composition_map: NDArray[np.int64]
    x_minus: BoundaryCondition
    x_plus: BoundaryCondition
    y_minus: BoundaryCondition
    y_plus: BoundaryCondition
    z_minus: BoundaryCondition
    z_plus: BoundaryCondition
    exterior: BoundaryCondition | None = None


@dataclass(frozen=True)
class InputComposition:
    """Input composition: a composition of a library set (contract section 5)."""

    library_set: str
    library_composition: str


@dataclass(frozen=True)
class LibraryBinding:
    """Nodal library file and the input compositions the map indexes (contract section 5).

    A relative ``library_file`` is written as given and resolved by the engine against the
    directory that holds the problem input, not the current directory.
    """

    library_file: Path
    compositions: tuple[InputComposition, ...]


@dataclass(frozen=True)
class OperatingState:
    """Node states (contract section 6). A float applies to every node; an array is [z][y][x]."""

    thermal_power_w: float
    doppler_temperature_k: NodeValue
    moderator_density_kg_m3: NodeValue
    moderator_temperature_k: NodeValue
    boron_ppm: NodeValue


@dataclass(frozen=True)
class SolverOptions:
    """Solver options (contract section 7). None members are not written: the default applies."""

    kernel: KernelChoice | None = None
    kernel_switch_threshold: float | None = None
    wielandt_shift: float | None = None
    eigenvalue_tolerance: float | None = None
    fission_source_l2_tolerance: float | None = None
    fission_source_linf_tolerance: float | None = None
    inner_reduction: float | None = None
    inner_iteration_limit: int | None = None
    outer_iteration_limit: int | None = None
    nodal_update_period: int | None = None
    coupling_relaxation: float | None = None
    adjoint: bool | None = None


@dataclass(frozen=True)
class ProblemInput:
    """A whole problem input, format version 1."""

    reactor: ReactorDescription
    library: LibraryBinding
    operating_state: OperatingState
    solver_options: SolverOptions = field(default_factory=SolverOptions)


class _Attrs(Protocol):
    def __contains__(self, name: object, /) -> bool: ...

    def __getitem__(self, name: str, /) -> object: ...

    def __setitem__(self, name: str, value: object, /) -> None: ...


class _Dataset(Protocol):
    @property
    def attrs(self) -> _Attrs: ...

    def __getitem__(self, key: object, /) -> object: ...


class _Group(Protocol):
    @property
    def attrs(self) -> _Attrs: ...

    def __contains__(self, name: object, /) -> bool: ...

    def __getitem__(self, name: str, /) -> object: ...

    def create_group(self, name: str, /) -> _Group: ...

    def create_dataset(self, name: str, /, *, data: object, dtype: object) -> _Dataset: ...


def write_problem_input(problem: ProblemInput, path: Path) -> None:
    """Write `problem` to `path`, replacing any file there. Nothing is validated."""
    with h5py.File(str(path), "w") as raw:
        root = cast(_Group, raw)
        root.attrs["format_version"] = FORMAT_VERSION
        _write_geometry(root, problem.reactor)
        _write_boundary_conditions(root, problem.reactor)
        _write_library(root, problem.library)
        _write_operating_state(root, problem.operating_state)
        if problem.solver_options != SolverOptions():
            _write_solver_options(root, problem.solver_options)


def read_problem_input(path: Path) -> ProblemInput:
    """Read back a problem input written by ``write_problem_input``.

    Only the format version, the boundary kinds and the members this reads are checked; the
    engine is the authority on what is valid.
    """
    with h5py.File(str(path), "r") as raw:
        root = cast(_Group, raw)
        _check_format_version(root)
        return ProblemInput(
            reactor=_read_reactor(root),
            library=_read_library(root),
            operating_state=_read_operating_state(root),
            solver_options=_read_solver_options(root),
        )


def _put(
    group: _Group,
    name: str,
    data: object,
    *,
    dtype: object = None,
    units: str | None = None,
) -> None:
    """Create dataset `name` in `group`, with a `units` attribute when given."""
    dataset = group.create_dataset(name, data=data, dtype=dtype)
    if units is not None:
        dataset.attrs["units"] = units


def _put_float(
    group: _Group,
    name: str,
    value: NodeValue,
    *,
    units: str | None = None,
) -> None:
    """Write a float64 scalar or array."""
    _put(group, name, np.asarray(value, dtype=np.float64), units=units)


def _write_geometry(root: _Group, reactor: ReactorDescription) -> None:
    geometry = root.create_group("geometry")
    _put(geometry, "width_x", np.asarray(reactor.width_x_cm, dtype=np.float64), units="cm")
    _put(geometry, "width_y", np.asarray(reactor.width_y_cm, dtype=np.float64), units="cm")
    _put(geometry, "width_z", np.asarray(reactor.width_z_cm, dtype=np.float64), units="cm")
    _put(geometry, "composition_map", np.asarray(reactor.composition_map, dtype=np.int64))


def _write_boundary_conditions(root: _Group, reactor: ReactorDescription) -> None:
    faces = root.create_group("boundary_conditions")
    _write_face(faces, "x_minus", reactor.x_minus)
    _write_face(faces, "x_plus", reactor.x_plus)
    _write_face(faces, "y_minus", reactor.y_minus)
    _write_face(faces, "y_plus", reactor.y_plus)
    _write_face(faces, "z_minus", reactor.z_minus)
    _write_face(faces, "z_plus", reactor.z_plus)
    if reactor.exterior is not None:
        _write_face(faces, "exterior", reactor.exterior)


def _write_face(faces: _Group, name: str, condition: BoundaryCondition) -> None:
    face = faces.create_group(name)
    face.attrs["kind"] = condition.kind.value
    if condition.albedo is not None:
        _put(face, "albedo", np.asarray(condition.albedo, dtype=np.float64), units="1")


def _write_library(root: _Group, binding: LibraryBinding) -> None:
    library = root.create_group("library")
    library.attrs["path"] = str(binding.library_file)
    compositions = root.create_group("compositions")
    _put(
        compositions,
        "library_set",
        [entry.library_set for entry in binding.compositions],
        dtype=h5py.string_dtype(),
    )
    _put(
        compositions,
        "library_composition",
        [entry.library_composition for entry in binding.compositions],
        dtype=h5py.string_dtype(),
    )


def _write_operating_state(root: _Group, state: OperatingState) -> None:
    group = root.create_group("operating_state")
    _put_float(group, "thermal_power", state.thermal_power_w, units="W")
    _put_float(group, "doppler_temperature", state.doppler_temperature_k, units="K")
    _put_float(group, "moderator_density", state.moderator_density_kg_m3, units="kg m^-3")
    _put_float(group, "moderator_temperature", state.moderator_temperature_k, units="K")
    _put_float(group, "boron", state.boron_ppm, units="ppm")


def _write_solver_options(root: _Group, options: SolverOptions) -> None:
    group = root.create_group("solver_options")
    if options.kernel is not None:
        _put(group, "kernel", options.kernel.value, dtype=h5py.string_dtype())
    for name, threshold in (
        ("kernel_switch_threshold", options.kernel_switch_threshold),
        ("wielandt_shift", options.wielandt_shift),
        ("eigenvalue_tolerance", options.eigenvalue_tolerance),
        ("fission_source_l2_tolerance", options.fission_source_l2_tolerance),
        ("fission_source_linf_tolerance", options.fission_source_linf_tolerance),
        ("inner_reduction", options.inner_reduction),
        ("coupling_relaxation", options.coupling_relaxation),
    ):
        if threshold is not None:
            _put_float(group, name, threshold)
    for name, limit in (
        ("inner_iteration_limit", options.inner_iteration_limit),
        ("outer_iteration_limit", options.outer_iteration_limit),
        ("nodal_update_period", options.nodal_update_period),
    ):
        if limit is not None:
            _put(group, name, np.asarray(limit, dtype=np.int64))
    if options.adjoint is not None:
        _put(group, "adjoint", np.asarray(options.adjoint, dtype=np.int64))


def _check_format_version(root: _Group) -> None:
    if "format_version" not in root.attrs:
        raise ValueError("format_version attribute is missing")
    version = root.attrs["format_version"]
    if not isinstance(version, int | np.integer) or version != FORMAT_VERSION:
        raise ValueError(f"unsupported format_version {version!r}; expected {FORMAT_VERSION}")


def _group(parent: _Group, name: str) -> _Group:
    member = parent[name]
    if not isinstance(member, h5py.Group):
        raise TypeError(f"{name} must be a group")
    return cast(_Group, member)


def _dataset(parent: _Group, name: str) -> _Dataset:
    member = parent[name]
    if not isinstance(member, h5py.Dataset):
        raise TypeError(f"{name} must be a dataset")
    return cast(_Dataset, member)


def _text(value: object) -> str:
    """Decode a stored string; h5py returns bytes for variable-length strings."""
    if isinstance(value, bytes):
        return value.decode("utf-8")
    return str(value)


def _texts(group: _Group, name: str) -> tuple[str, ...]:
    values = cast(Iterable[object], _dataset(group, name)[()])
    return tuple(_text(value) for value in values)


def _float_scalar(group: _Group, name: str) -> float:
    value = np.asarray(_dataset(group, name)[()])
    if value.ndim != 0 or not np.issubdtype(value.dtype, np.floating):
        raise TypeError(f"{name} must be a floating-point scalar")
    return float(value)


def _int_scalar(group: _Group, name: str) -> int:
    value = np.asarray(_dataset(group, name)[()])
    if value.ndim != 0 or not np.issubdtype(value.dtype, np.integer):
        raise TypeError(f"{name} must be an integer scalar")
    return int(value)


def _node_value(group: _Group, name: str) -> NodeValue:
    values = np.asarray(_dataset(group, name)[()], dtype=np.float64)
    if values.ndim == 0:
        return float(values)
    return values


def _widths(geometry: _Group, name: str) -> tuple[float, ...]:
    values = np.asarray(_dataset(geometry, name)[()], dtype=np.float64)
    return tuple(float(width) for width in values)


def _face(faces: _Group, name: str) -> BoundaryCondition:
    face = _group(faces, name)
    kind = BoundaryKind(_text(face.attrs["kind"]))
    if "albedo" not in face:
        return BoundaryCondition(kind=kind)
    albedo = np.asarray(_dataset(face, "albedo")[()], dtype=np.float64)
    if albedo.shape != (2,):
        raise ValueError(f"{name}/albedo must have shape (2,)")
    return BoundaryCondition(kind=kind, albedo=(float(albedo[0]), float(albedo[1])))


def _read_reactor(root: _Group) -> ReactorDescription:
    geometry = _group(root, "geometry")
    faces = _group(root, "boundary_conditions")
    return ReactorDescription(
        width_x_cm=_widths(geometry, "width_x"),
        width_y_cm=_widths(geometry, "width_y"),
        width_z_cm=_widths(geometry, "width_z"),
        composition_map=np.asarray(_dataset(geometry, "composition_map")[()], dtype=np.int64),
        x_minus=_face(faces, "x_minus"),
        x_plus=_face(faces, "x_plus"),
        y_minus=_face(faces, "y_minus"),
        y_plus=_face(faces, "y_plus"),
        z_minus=_face(faces, "z_minus"),
        z_plus=_face(faces, "z_plus"),
        exterior=_face(faces, "exterior") if "exterior" in faces else None,
    )


def _read_library(root: _Group) -> LibraryBinding:
    library = _group(root, "library")
    compositions = _group(root, "compositions")
    sets = _texts(compositions, "library_set")
    names = _texts(compositions, "library_composition")
    return LibraryBinding(
        library_file=Path(_text(library.attrs["path"])),
        compositions=tuple(
            InputComposition(library_set=set_name, library_composition=name)
            for set_name, name in zip(sets, names, strict=True)
        ),
    )


def _read_operating_state(root: _Group) -> OperatingState:
    state = _group(root, "operating_state")
    return OperatingState(
        thermal_power_w=_float_scalar(state, "thermal_power"),
        doppler_temperature_k=_node_value(state, "doppler_temperature"),
        moderator_density_kg_m3=_node_value(state, "moderator_density"),
        moderator_temperature_k=_node_value(state, "moderator_temperature"),
        boron_ppm=_node_value(state, "boron"),
    )


def _optional_float(group: _Group, name: str) -> float | None:
    return _float_scalar(group, name) if name in group else None


def _optional_int(group: _Group, name: str) -> int | None:
    return _int_scalar(group, name) if name in group else None


def _optional_kernel(group: _Group) -> KernelChoice | None:
    if "kernel" not in group:
        return None
    return KernelChoice(_text(_dataset(group, "kernel")[()]))


def _optional_adjoint(group: _Group) -> bool | None:
    if "adjoint" not in group:
        return None
    value = _int_scalar(group, "adjoint")
    if value not in (0, 1):
        raise ValueError("adjoint must be 0 or 1")
    return value == 1


def _read_solver_options(root: _Group) -> SolverOptions:
    if "solver_options" not in root:
        return SolverOptions()
    group = _group(root, "solver_options")
    return SolverOptions(
        kernel=_optional_kernel(group),
        kernel_switch_threshold=_optional_float(group, "kernel_switch_threshold"),
        wielandt_shift=_optional_float(group, "wielandt_shift"),
        eigenvalue_tolerance=_optional_float(group, "eigenvalue_tolerance"),
        fission_source_l2_tolerance=_optional_float(group, "fission_source_l2_tolerance"),
        fission_source_linf_tolerance=_optional_float(group, "fission_source_linf_tolerance"),
        inner_reduction=_optional_float(group, "inner_reduction"),
        inner_iteration_limit=_optional_int(group, "inner_iteration_limit"),
        outer_iteration_limit=_optional_int(group, "outer_iteration_limit"),
        nodal_update_period=_optional_int(group, "nodal_update_period"),
        coupling_relaxation=_optional_float(group, "coupling_relaxation"),
        adjoint=_optional_adjoint(group),
    )
