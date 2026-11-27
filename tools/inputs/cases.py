# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Aurora Jahan
# Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.
"""Named example problem inputs for the end-to-end tests (`tests/end_to_end/urus_test.cpp`).

Every case is written with `problem_input.py` against the library of the C++ fixture
(`tests/unit/io/library_fixture.cpp`, `make_valid_fixture`): composition set `test_set` with
compositions `fuel` (fissile) and `reflector` (no fission), on the axes fuel_temperature
[600, 900] K and boron [0, 1000] ppm. Every node state lies inside those tables unless a case
says otherwise.

CLI: `python3 -m tools.inputs.cases <case> <output_dir> --library <path>` writes
`<output_dir>/problem.h5` and, for a linked case, its document files next to it. The library
path is written as given: an absolute path, or a relative one resolved against `<output_dir>`
(the directory of the reactor document).
"""

from __future__ import annotations

import argparse
from collections.abc import Callable, Sequence
from dataclasses import dataclass, replace
from pathlib import Path
from typing import Final

import numpy as np

from tools.inputs import problem_input as pi

FIXTURE_SET: Final = "test_set"
FUEL: Final = "fuel"
REFLECTOR: Final = "reflector"

PROBLEM_FILE: Final = "problem.h5"
THERMAL_POWER_W: Final = 1.0e8
FUEL_TEMPERATURE_K: Final = 750.0  # inside [600, 900]
BORON_PPM: Final = 500.0  # inside [0, 1000]
MODERATOR_DENSITY_KG_M3: Final = 740.0
MODERATOR_TEMPERATURE_K: Final = 580.0
CLAMPED_TEMPERATURE_K: Final = 950.0  # above 900: one node, clamped

# The tight tolerances of the verification runs (milestone 1 plan §2.1).
VERIFICATION_EIGENVALUE_TOLERANCE: Final = 1e-12
VERIFICATION_L2_TOLERANCE: Final = 1e-11
VERIFICATION_LINF_TOLERANCE: Final = 1e-10


@dataclass(frozen=True)
class Case:
    """A problem input, and the documents written to files of their own (linked)."""

    problem: pi.ProblemInput
    linked: tuple[pi.DocumentKind, ...] = ()


type CaseBuilder = Callable[[Path], Case]


def _reactor(library: Path) -> pi.ReactorDocument:
    """A 3 x 3 x 2 core; one reflector node at the top corner, the rest fuel.

    Reflective faces on x_minus, y_minus and z_minus, vacuum on the three plus faces.
    """
    composition_map = np.zeros((2, 3, 3), dtype=np.int64)  # [z][y][x]
    composition_map[1, 2, 2] = 1
    reflective = pi.BoundaryCondition(pi.BoundaryKind.REFLECTIVE)
    vacuum = pi.BoundaryCondition(pi.BoundaryKind.VACUUM)
    return pi.ReactorDocument(
        reactor=pi.ReactorDescription(
            width_x_cm=(10.0, 10.0, 10.0),
            width_y_cm=(10.0, 10.0, 10.0),
            width_z_cm=(15.0, 15.0),
            composition_map=composition_map,
            x_minus=reflective,
            x_plus=vacuum,
            y_minus=reflective,
            y_plus=vacuum,
            z_minus=reflective,
            z_plus=vacuum,
        ),
        library=pi.LibraryBinding(
            library_file=library,
            compositions=(
                pi.InputComposition(library_set=FIXTURE_SET, library_composition=FUEL),
                pi.InputComposition(library_set=FIXTURE_SET, library_composition=REFLECTOR),
            ),
        ),
    )


def _state(
    *, thermal_power_w: float = THERMAL_POWER_W, doppler: pi.NodeValue = FUEL_TEMPERATURE_K
) -> pi.OperatingState:
    return pi.OperatingState(
        thermal_power_w=thermal_power_w,
        doppler_temperature_k=doppler,
        moderator_density_kg_m3=MODERATOR_DENSITY_KG_M3,
        moderator_temperature_k=MODERATOR_TEMPERATURE_K,
        boron_ppm=BORON_PPM,
    )


def _problem(
    reactor: pi.ReactorDocument,
    state: pi.OperatingState,
    settings: pi.SettingsDocument | None,
) -> pi.ProblemInput:
    return pi.ProblemInput(
        reactor=reactor,
        initial_state=pi.InitialStateDocument(operating_state=state),
        settings=settings,
    )


_FINITE_DIFFERENCE: Final = pi.SolverOptions(kernel=pi.KernelChoice.FINITE_DIFFERENCE)


def _valid(library: Path) -> Case:
    settings = pi.SettingsDocument(solver_options=_FINITE_DIFFERENCE)
    return Case(_problem(_reactor(library), _state(), settings))


def _linked(library: Path) -> Case:
    settings = pi.SettingsDocument(solver_options=_FINITE_DIFFERENCE)
    return Case(
        _problem(_reactor(library), _state(), settings),
        linked=(pi.DocumentKind.INITIAL_STATE, pi.DocumentKind.SETTINGS),
    )


def _default_kernel(library: Path) -> Case:
    return Case(_problem(_reactor(library), _state(), None))


def _adjoint(library: Path) -> Case:
    options = replace(_FINITE_DIFFERENCE, adjoint=True)
    settings = pi.SettingsDocument(solver_options=options)
    return Case(_problem(_reactor(library), _state(), settings))


def _invalid(library: Path) -> Case:
    settings = pi.SettingsDocument(solver_options=_FINITE_DIFFERENCE)
    return Case(_problem(_reactor(library), _state(thermal_power_w=0.0), settings))


def _not_converged(library: Path) -> Case:
    options = replace(
        _FINITE_DIFFERENCE,
        outer_iteration_limit=1,
        eigenvalue_tolerance=VERIFICATION_EIGENVALUE_TOLERANCE,
        fission_source_l2_tolerance=VERIFICATION_L2_TOLERANCE,
        fission_source_linf_tolerance=VERIFICATION_LINF_TOLERANCE,
    )
    return Case(_problem(_reactor(library), _state(), pi.SettingsDocument(options)))


def _clamped(library: Path) -> Case:
    # One node (x 0, y 0, z 0) above the last fuel_temperature point; the rest inside.
    doppler = np.full((2, 3, 3), FUEL_TEMPERATURE_K, dtype=np.float64)
    doppler[0, 0, 0] = CLAMPED_TEMPERATURE_K
    settings = pi.SettingsDocument(solver_options=_FINITE_DIFFERENCE, allow_table_clamps=True)
    return Case(_problem(_reactor(library), _state(doppler=doppler), settings))


CASES: Final[dict[str, CaseBuilder]] = {
    "valid": _valid,
    "linked": _linked,
    "default_kernel": _default_kernel,
    "adjoint": _adjoint,
    "invalid": _invalid,
    "not_converged": _not_converged,
    "clamped": _clamped,
}


def write_case(case_name: str, output_dir: Path, library: Path) -> Path:
    """Write case `case_name` into `output_dir`; return the path of its problem input."""
    if case_name not in CASES:
        known = ", ".join(sorted(CASES))
        raise ValueError(f"unknown case {case_name!r}; known cases: {known}")
    case = CASES[case_name](library)
    output_dir.mkdir(parents=True, exist_ok=True)
    problem_path = output_dir / PROBLEM_FILE
    document_files = {kind: output_dir / f"{kind.value}.h5" for kind in case.linked}
    pi.write_problem_input(case.problem, problem_path, document_files=document_files)
    return problem_path


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="python3 -m tools.inputs.cases",
        description="Write a named example problem input.",
    )
    parser.add_argument("case", choices=sorted(CASES))
    parser.add_argument("output_dir", type=Path)
    parser.add_argument("--library", type=Path, required=True, help="nodal library file")
    args = parser.parse_args(argv)
    write_case(args.case, args.output_dir, args.library)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
