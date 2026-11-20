# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Aurora Jahan
# Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.
"""Problem input files, format version 2 (docs/contracts/problem_input.md).

A problem input holds four documents (`DocumentKind`): the reactor, the initial state, the
control signals and the settings. Each document is written in place in the problem input, or
in a document file of its own that the problem input links to, so that one reactor can serve
many initial states and settings.

- `write_problem_input` writes a whole problem input; `document_files` names the documents to
  write to their own files.
- `write_document` writes one document alone to a file, for linking from many problem inputs.
- `link_problem_input` writes a problem input that only links existing document files.
- `read_problem_input` reads either form, resolving links against the problem input's directory.

The writers do not validate: the engine (src/nts/io/problem.hpp) reads and checks every file
and is the authority.

Node-state fields and the composition map are NumPy arrays, so `==` on
`ProblemInput` or `ReactorDescription` is ambiguous. Compare those fields with
`numpy.testing`.
"""

from __future__ import annotations

import os
from collections.abc import Callable, Iterable, Mapping
from contextlib import ExitStack
from dataclasses import dataclass, field
from enum import StrEnum
from pathlib import Path
from typing import ClassVar, Protocol, cast, runtime_checkable

import h5py
import numpy as np
from numpy.typing import NDArray

FORMAT_VERSION = 2  # contract section 2
INITIAL_STATE_SOURCE = "stated"  # contract section 6

type NodeValue = float | NDArray[np.float64]


class DocumentKind(StrEnum):
    """The input documents, named by their members of the problem input root (contract §2)."""

    REACTOR = "reactor"
    INITIAL_STATE = "initial_state"
    CONTROL_SIGNALS = "control_signals"
    SETTINGS = "settings"


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

    `library_file` is written as given. The engine resolves a relative path against the
    directory of the file that holds the reactor document (the problem input, or the reactor's
    document file when linked), not the current directory.
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
class ReactorDocument:
    """The reactor document: geometry, boundary conditions, library and compositions (§3-§5)."""

    kind: ClassVar[DocumentKind] = DocumentKind.REACTOR
    reactor: ReactorDescription
    library: LibraryBinding


@dataclass(frozen=True)
class InitialStateDocument:
    """The initial state document (contract §6), written with `source` = `stated`."""

    kind: ClassVar[DocumentKind] = DocumentKind.INITIAL_STATE
    operating_state: OperatingState


@dataclass(frozen=True)
class ControlSignalsDocument:
    """The control signals document (contract §8). Format version 2 writes it empty."""

    kind: ClassVar[DocumentKind] = DocumentKind.CONTROL_SIGNALS


@dataclass(frozen=True)
class SettingsDocument:
    """The settings document (contract §7). A None `allow_table_clamps` is not written."""

    kind: ClassVar[DocumentKind] = DocumentKind.SETTINGS
    solver_options: SolverOptions = field(default_factory=SolverOptions)
    allow_table_clamps: bool | None = None


type Document = ReactorDocument | InitialStateDocument | ControlSignalsDocument | SettingsDocument


@dataclass(frozen=True)
class ProblemInput:
    """A whole problem input, format version 2. A None optional document is not written."""

    reactor: ReactorDocument
    initial_state: InitialStateDocument
    control_signals: ControlSignalsDocument | None = None
    settings: SettingsDocument | None = None


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

    def __setitem__(self, name: str, value: object, /) -> None: ...

    def get(self, name: str, /, *, getlink: bool) -> object: ...

    def create_group(self, name: str, /) -> _Group: ...

    def create_dataset(self, name: str, /, *, data: object, dtype: object) -> _Dataset: ...


@runtime_checkable
class _ExternalLink(Protocol):
    """An `h5py.ExternalLink`; the installed h5py stubs do not declare the class."""

    @property
    def filename(self) -> str: ...

    @property
    def path(self) -> str: ...


# B009: attribute access would fail basedpyright, since the stubs lack `ExternalLink`.
_EXTERNAL_LINK = cast(Callable[[str, str], object], getattr(h5py, "ExternalLink"))  # noqa: B009


def write_problem_input(
    problem: ProblemInput,
    path: Path,
    *,
    document_files: Mapping[DocumentKind, Path] | None = None,
) -> None:
    """Write `problem` to `path`, replacing any file there. Nothing is validated.

    A document named in `document_files` is written to that file, at its root, and linked from
    `path`; the other documents are written in place. Each link name is relative to the
    directory of `path`, or absolute when no relative name exists. A relative library path is
    resolved against the reactor document's own file, so linking the reactor elsewhere changes
    which library the engine opens. Raises ValueError, writing nothing, when `document_files`
    names a document that `problem` does not have, or names one file for two documents.
    """
    files = dict(document_files or {})
    documents = _present_documents(problem)
    absent = [kind.value for kind in files if kind not in documents]
    if absent:
        raise ValueError(f"document file given for absent document {', '.join(absent)}")
    resolved = [document_file.resolve() for document_file in files.values()]
    if len(set(resolved)) != len(resolved):
        raise ValueError("document_files names the same file for two documents")
    members: dict[DocumentKind, Document | Path] = {}
    for kind, document in documents.items():
        if kind in files:
            write_document(document, files[kind])
            members[kind] = files[kind]
        else:
            members[kind] = document
    _write_root(path, members)


def write_document(document: Document, path: Path) -> None:
    """Write one document alone to the file `path`, at the file's root, replacing any file there.

    The file is a document file: link it from a problem input with `write_problem_input`'s
    `document_files` or `link_problem_input`.
    """
    with h5py.File(str(path), "w") as raw:
        _fill_document(cast(_Group, raw), document)


def link_problem_input(path: Path, documents: Mapping[DocumentKind, Path]) -> None:
    """Write a problem input at `path` whose members link the existing document files.

    Only the links and the format version are written; each file must hold its document (see
    `write_document`) for the problem input to be read.
    """
    _write_root(path, documents)


def read_problem_input(path: Path) -> ProblemInput:
    """Read back a problem input written in place, linked, or mixed.

    External links are resolved here, against the directory of `path`. Checks the format
    versions, the document kinds and the members this reads; the engine is the authority.
    """
    with h5py.File(str(path), "r") as raw, ExitStack() as files:
        root = cast(_Group, raw)
        _check_format_version(root)
        reactor = _open_required(root, path, DocumentKind.REACTOR, files)
        initial_state = _open_required(root, path, DocumentKind.INITIAL_STATE, files)
        control_signals = _open_document(root, path, DocumentKind.CONTROL_SIGNALS, files)
        settings = _open_document(root, path, DocumentKind.SETTINGS, files)
        return ProblemInput(
            reactor=ReactorDocument(
                reactor=_read_reactor(reactor),
                library=_read_library(reactor),
            ),
            initial_state=InitialStateDocument(
                operating_state=_read_operating_state(initial_state),
            ),
            control_signals=None if control_signals is None else ControlSignalsDocument(),
            settings=None if settings is None else _read_settings(settings),
        )


def _present_documents(problem: ProblemInput) -> dict[DocumentKind, Document]:
    """The documents `problem` has, in the order of the problem input's root."""
    parts = (problem.reactor, problem.initial_state, problem.control_signals, problem.settings)
    documents: dict[DocumentKind, Document] = {
        document.kind: document for document in parts if document is not None
    }
    return documents


def _write_root(path: Path, members: Mapping[DocumentKind, Document | Path]) -> None:
    """Write the problem input root: a document in place, or a link to a document file."""
    with h5py.File(str(path), "w") as raw:
        root = cast(_Group, raw)
        root.attrs["format_version"] = FORMAT_VERSION
        for kind, member in members.items():
            if isinstance(member, Path):
                root[kind.value] = _EXTERNAL_LINK(_link_name(path, member), "/")
            else:
                _fill_document(root.create_group(kind.value), member)


def _link_name(problem_path: Path, document_file: Path) -> str:
    """`document_file` relative to the directory of `problem_path`, absolute if unrelatable.

    Both paths are resolved first, so a symbolic link in either path does not change the name.
    """
    document = document_file.resolve()
    directory = problem_path.parent.resolve()
    try:
        return Path(os.path.relpath(document, directory)).as_posix()
    except ValueError:  # no relative path exists, e.g. on another drive
        return document.as_posix()


def _fill_document(root: _Group, document: Document) -> None:
    """Write the attributes and members of `document` into its root group `root`."""
    root.attrs["document"] = document.kind.value
    root.attrs["format_version"] = FORMAT_VERSION
    match document:
        case ReactorDocument():
            _write_geometry(root, document.reactor)
            _write_boundary_conditions(root, document.reactor)
            _write_library(root, document.library)
        case InitialStateDocument():
            root.attrs["source"] = INITIAL_STATE_SOURCE
            _write_operating_state(root, document.operating_state)
        case SettingsDocument():
            _write_settings(root, document)
        case ControlSignalsDocument():
            pass


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
    _put_float(root, "thermal_power", state.thermal_power_w, units="W")
    _put_float(root, "doppler_temperature", state.doppler_temperature_k, units="K")
    _put_float(root, "moderator_density", state.moderator_density_kg_m3, units="kg m^-3")
    _put_float(root, "moderator_temperature", state.moderator_temperature_k, units="K")
    _put_float(root, "boron", state.boron_ppm, units="ppm")


def _write_settings(root: _Group, settings: SettingsDocument) -> None:
    options = settings.solver_options
    if options.kernel is not None:
        _put(root, "kernel", options.kernel.value, dtype=h5py.string_dtype())
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
            _put_float(root, name, threshold)
    for name, limit in (
        ("inner_iteration_limit", options.inner_iteration_limit),
        ("outer_iteration_limit", options.outer_iteration_limit),
        ("nodal_update_period", options.nodal_update_period),
    ):
        if limit is not None:
            _put(root, name, np.asarray(limit, dtype=np.int64))
    if options.adjoint is not None:
        _put(root, "adjoint", np.asarray(options.adjoint, dtype=np.int64))
    if settings.allow_table_clamps is not None:
        _put(
            root,
            "allow_table_clamps",
            np.asarray(settings.allow_table_clamps, dtype=np.int64),
        )


def _check_format_version(root: _Group) -> None:
    if "format_version" not in root.attrs:
        raise ValueError("format_version attribute is missing")
    version = root.attrs["format_version"]
    if not isinstance(version, int | np.integer) or version != FORMAT_VERSION:
        raise ValueError(f"unsupported format_version {version!r}; expected {FORMAT_VERSION}")


def _check_document(root: _Group, kind: DocumentKind) -> None:
    """The document attribute names `kind`, and the document's format version is 2."""
    if "document" not in root.attrs:
        raise ValueError(f"{kind.value}: document attribute is missing")
    name = _text(root.attrs["document"])
    if name != kind.value:
        raise ValueError(f"document kind '{name}' is not '{kind.value}'")
    _check_format_version(root)


def _linked_path(problem_path: Path, filename: str) -> Path:
    """A relative link name is resolved against the directory of the problem input."""
    link = Path(filename)
    return link if link.is_absolute() else problem_path.parent / link


def _open_document(
    problem: _Group,
    problem_path: Path,
    kind: DocumentKind,
    files: ExitStack,
) -> _Group | None:
    """The root group of document `kind`, in place or in its linked file; None when absent."""
    member = problem.get(kind.value, getlink=True)
    if member is None:
        return None
    if isinstance(member, _ExternalLink):
        if member.path != "/":
            raise ValueError(
                f"/{kind.value}: the external link must name the root group '/' of a document file"
            )
        document_file = _linked_path(problem_path, member.filename)
        root = cast(_Group, files.enter_context(h5py.File(str(document_file), "r")))
    else:
        root = _group(problem, kind.value)
    _check_document(root, kind)
    return root


def _open_required(
    problem: _Group,
    problem_path: Path,
    kind: DocumentKind,
    files: ExitStack,
) -> _Group:
    root = _open_document(problem, problem_path, kind, files)
    if root is None:
        raise ValueError(f"{kind.value} document is missing")
    return root


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
    return OperatingState(
        thermal_power_w=_float_scalar(root, "thermal_power"),
        doppler_temperature_k=_node_value(root, "doppler_temperature"),
        moderator_density_kg_m3=_node_value(root, "moderator_density"),
        moderator_temperature_k=_node_value(root, "moderator_temperature"),
        boron_ppm=_node_value(root, "boron"),
    )


def _optional_float(group: _Group, name: str) -> float | None:
    return _float_scalar(group, name) if name in group else None


def _optional_int(group: _Group, name: str) -> int | None:
    return _int_scalar(group, name) if name in group else None


def _optional_kernel(group: _Group) -> KernelChoice | None:
    if "kernel" not in group:
        return None
    return KernelChoice(_text(_dataset(group, "kernel")[()]))


def _optional_flag(group: _Group, name: str) -> bool | None:
    """A 0/1 integer member, read as a bool; None when absent."""
    if name not in group:
        return None
    value = _int_scalar(group, name)
    if value not in (0, 1):
        raise ValueError(f"{name} must be 0 or 1")
    return value == 1


def _read_solver_options(group: _Group) -> SolverOptions:
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
        adjoint=_optional_flag(group, "adjoint"),
    )


def _read_settings(group: _Group) -> SettingsDocument:
    return SettingsDocument(
        solver_options=_read_solver_options(group),
        allow_table_clamps=_optional_flag(group, "allow_table_clamps"),
    )
