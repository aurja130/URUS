# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Aurora Jahan
# Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.
"""Analytic eigenvalues of the bare homogeneous two-group diffusion benchmark (plan section 5).

Continuous two-group diffusion with Sigma_r1 = Sigma_a1 + Sigma_12 and Sigma_r2 = Sigma_a2 has,
for a mode cos(Bx) with buckling B^2 = b, the group ratio A2 = Sigma_12 / (D2 b + Sigma_r2) at
A1 = 1, and the eigenvalue relation (TM Eq. (4.7) for a homogeneous medium)

    k(b) = (nuSigma_f1 + nuSigma_f2 Sigma_12 / (Sigma_a2 + D2 b)) / (Sigma_r1 + D1 b).

Zero-flux faces and reflective/zero-flux axes give b in closed form. A slab with vacuum or
albedo faces is symmetric, so its flux is a sum of the modes cos(mu x) and cosh(nu x), the two
roots b = mu^2 and b = -nu^2 of the dispersion quadratic at k. The Robin conditions
-D_g phi_g'(a) = alpha_g phi_g(a) at a = L/2 give a 2x2 determinant in k; its largest root below
k_infinity is the fundamental eigenvalue.

The cases and their eigenvalues are written to tests/verification/bare_homogeneous_reference.csv
by `python3 -m tools.references.bare_homogeneous --write`; the verification test reads that file.
Without --write the table is printed. This module shares no code with the engine.
"""

from __future__ import annotations

import argparse
import math
import sys
from collections.abc import Callable, Sequence
from dataclasses import dataclass
from enum import StrEnum
from pathlib import Path
from typing import Final, cast

import numpy as np
from scipy.optimize import (  # pyright: ignore[reportMissingTypeStubs]
    brentq,  # pyright: ignore[reportUnknownVariableType]
)

DESCRIPTION: Final = "Analytic eigenvalues of the bare homogeneous two-group benchmark."
REPO_ROOT: Final = Path(__file__).resolve().parents[2]
REFERENCE_CSV: Final = REPO_ROOT / "tests" / "verification" / "bare_homogeneous_reference.csv"

ROOT_XTOL: Final = 1.0e-14
SCAN_LOWEST_K: Final = 0.5
SCAN_POINTS: Final = 2000
VACUUM_ALPHA: Final = 0.5  # Marshak condition: J_out = 0.5 phi_s
ZERO_FLUX_ALPHA: Final = 1.0e30  # alpha of a zero-flux face (limit of TM Eq. (4.4))
ALBEDO_PAIR: Final = (0.3, 0.6)  # per-group alpha of the albedo cases

AXIS_NAMES: Final = ("x", "y", "z")
FACE_NAMES: Final = ("x_minus", "x_plus", "y_minus", "y_plus", "z_minus", "z_plus")
FACE_COUNT: Final = 6
SLAB_LENGTHS_CM: Final = (60.0, 50.0, 40.0)
CUBOID_LENGTHS_CM: Final = (24.0, 20.0, 16.0)

CSV_COLUMNS: Final = (
    "case",
    "geometry",
    *(f"{face}_{field}" for face in FACE_NAMES for field in ("kind", "albedo_1", "albedo_2")),
    "lx_cm",
    "ly_cm",
    "lz_cm",
    "d1_cm",
    "d2_cm",
    "sigma_a1_per_cm",
    "sigma_a2_per_cm",
    "sigma_12_per_cm",
    "nu_sigma_f1_per_cm",
    "nu_sigma_f2_per_cm",
    "k_ref",
)

# scipy ships no type stubs here; root_of narrows the untyped result to float.


class Kind(StrEnum):
    REFLECTIVE = "reflective"
    VACUUM = "vacuum"
    ZERO_FLUX = "zero_flux"
    ALBEDO = "albedo"


@dataclass(frozen=True)
class FaceSpec:
    kind: Kind
    albedo: tuple[float, float] = (0.0, 0.0)  # per group; used by Kind.ALBEDO only


@dataclass(frozen=True)
class TwoGroup:
    d1_cm: float
    d2_cm: float
    sigma_a1_per_cm: float
    sigma_a2_per_cm: float
    sigma_12_per_cm: float
    nu_sigma_f1_per_cm: float
    nu_sigma_f2_per_cm: float

    @property
    def removal1(self) -> float:
        return self.sigma_a1_per_cm + self.sigma_12_per_cm

    @property
    def removal2(self) -> float:
        return self.sigma_a2_per_cm


@dataclass(frozen=True)
class Case:
    name: str
    geometry: str  # "slab_x", "slab_y", "slab_z" or "cuboid"
    faces: tuple[FaceSpec, ...]  # order: x_minus, x_plus, y_minus, y_plus, z_minus, z_plus
    lengths_cm: tuple[float, float, float]
    xs: TwoGroup


EXAMPLE_FUEL: Final = TwoGroup(
    d1_cm=1.4,
    d2_cm=0.4,
    sigma_a1_per_cm=0.010,
    sigma_a2_per_cm=0.08,
    sigma_12_per_cm=0.017,
    nu_sigma_f1_per_cm=0.006,
    nu_sigma_f2_per_cm=0.11,
)

REFLECTIVE_FACE: Final = FaceSpec(Kind.REFLECTIVE)
ZERO_FLUX_FACE: Final = FaceSpec(Kind.ZERO_FLUX)
VACUUM_FACE: Final = FaceSpec(Kind.VACUUM)
ALBEDO_FACE: Final = FaceSpec(Kind.ALBEDO, ALBEDO_PAIR)


def root_of(residual: Callable[[float], float], lo: float, hi: float) -> float:
    """Root of residual in [lo, hi] by scipy's brentq, with an absolute tolerance of ROOT_XTOL."""
    return cast("float", brentq(residual, lo, hi, xtol=ROOT_XTOL))


def face_albedo(face: FaceSpec) -> tuple[float, float]:
    """alpha_b per group of a face (TM Eq. (4.4)): fixed by the kind, or the input for albedo."""
    match face.kind:
        case Kind.REFLECTIVE:
            return (0.0, 0.0)
        case Kind.VACUUM:
            return (VACUUM_ALPHA, VACUUM_ALPHA)
        case Kind.ZERO_FLUX:
            return (ZERO_FLUX_ALPHA, ZERO_FLUX_ALPHA)
        case Kind.ALBEDO:
            return face.albedo


def k_at_buckling(xs: TwoGroup, buckling_sq: float) -> float:
    """k of a mode cos(Bx) with B^2 = buckling_sq (TM Eq. (4.7) for a homogeneous medium)."""
    b = buckling_sq
    coupling = xs.nu_sigma_f2_per_cm * xs.sigma_12_per_cm / (xs.sigma_a2_per_cm + xs.d2_cm * b)
    return (xs.nu_sigma_f1_per_cm + coupling) / (xs.removal1 + xs.d1_cm * b)


def k_infinity(xs: TwoGroup) -> float:
    return k_at_buckling(xs, 0.0)


def axis_buckling_sq(minus: FaceSpec, plus: FaceSpec, length_cm: float) -> float | None:
    """B^2 of an axis whose faces are zero flux or reflective: (pi/L)^2 for two zero-flux faces,
    (pi/(2L))^2 for one zero-flux face, 0 for two reflective faces. None when a face is vacuum
    or albedo, which needs the slab determinant."""
    kinds = (minus.kind, plus.kind)
    if Kind.VACUUM in kinds or Kind.ALBEDO in kinds:
        return None
    zero_faces = kinds.count(Kind.ZERO_FLUX)
    if zero_faces == 2:
        return (math.pi / length_cm) ** 2
    if zero_faces == 1:
        return (math.pi / (2.0 * length_cm)) ** 2
    return 0.0


def dispersion_roots(xs: TwoGroup, k: float) -> tuple[float, float]:
    """The roots b = B^2 of the two-group dispersion relation at k, larger first.

    Eliminating A2 from the mode equations gives (D1 b + Sigma_r1 - nuSigma_f1/k)(D2 b + Sigma_r2)
    - nuSigma_f2 Sigma_12 / k = 0, a quadratic in b. For k <= k_infinity the larger root is >= 0
    (the cosine mode) and the smaller one lies below -Sigma_r2/D2, because the quadratic is
    -nuSigma_f2 Sigma_12 / k < 0 at b = -Sigma_r2/D2; hence D2 b + Sigma_r2 never vanishes.
    """
    p1 = xs.nu_sigma_f1_per_cm / k
    qa = xs.d1_cm * xs.d2_cm
    qb = xs.d1_cm * xs.removal2 + xs.d2_cm * (xs.removal1 - p1)
    qc = xs.removal2 * (xs.removal1 - p1) - xs.nu_sigma_f2_per_cm * xs.sigma_12_per_cm / k
    disc = qb * qb - 4.0 * qa * qc
    if disc < 0.0:
        raise ValueError(f"complex dispersion roots at k = {k}")
    # Stable form of the quadratic formula: no cancellation for the root near zero.
    q = -0.5 * (qb + math.copysign(math.sqrt(disc), qb))
    if q == 0.0:
        raise ValueError(f"degenerate dispersion relation at k = {k}")
    root_a = q / qa
    root_b = qc / q
    return (max(root_a, root_b), min(root_a, root_b))


def mode_at_face(b: float, half_cm: float) -> tuple[float, float]:
    """f(a) and f'(a) at a = half_cm of the mode with B^2 = b: cos(Bx) for b > 0, 1 for b = 0,
    cosh(nu x) for b = -nu^2 < 0."""
    if b > 0.0:
        mu = math.sqrt(b)
        return math.cos(mu * half_cm), -mu * math.sin(mu * half_cm)
    if b == 0.0:
        return 1.0, 0.0
    nu = math.sqrt(-b)
    return math.cosh(nu * half_cm), nu * math.sinh(nu * half_cm)


def slab_determinant(xs: TwoGroup, k: float, half_cm: float, alpha: tuple[float, float]) -> float:
    """Determinant of the Robin system of the symmetric slab [-a, a] at k.

    Columns are the two modes of dispersion_roots, with A1 = 1 and
    A2 = Sigma_12 / (D2 b + Sigma_r2). Rows are the Robin conditions
    sum_m c_m (D_g f_m'(a) A_gm + alpha_g f_m(a) A_gm) = 0, g = 1, 2.
    """
    matrix = np.zeros((2, 2), dtype=np.float64)
    for column, b in enumerate(dispersion_roots(xs, k)):
        value, slope = mode_at_face(b, half_cm)
        a2 = xs.sigma_12_per_cm / (xs.d2_cm * b + xs.removal2)
        matrix[0, column] = xs.d1_cm * slope + alpha[0] * value
        matrix[1, column] = a2 * (xs.d2_cm * slope + alpha[1] * value)
    return float(np.linalg.det(matrix))


def slab_root(xs: TwoGroup, half_cm: float, alpha: tuple[float, float]) -> float:
    """Fundamental eigenvalue of the symmetric slab [-a, a] with Robin factors alpha.

    The determinant is scanned from k_infinity downwards; the first sign change is the largest
    root below k_infinity, i.e. the fundamental mode, which brentq then refines. The check
    mu a <= pi/2 (up to rounding) confirms that the cosine mode, and hence the flux, is
    positive on the slab.
    """

    def residual(k: float) -> float:
        return slab_determinant(xs, k, half_cm, alpha)

    grid: list[float] = np.linspace(k_infinity(xs), SCAN_LOWEST_K, SCAN_POINTS).tolist()
    values = [residual(k) for k in grid]
    for index in range(1, len(grid)):
        if (values[index - 1] > 0.0) != (values[index] > 0.0):
            root = root_of(residual, grid[index], grid[index - 1])
            b_cos, _ = dispersion_roots(xs, root)
            # mu a = pi/2 exactly in the zero-flux limit; allow 1e-9 relative rounding slack.
            mu_half = math.sqrt(b_cos) * half_cm if b_cos > 0.0 else 0.0
            if mu_half > (math.pi / 2.0) * (1.0 + 1.0e-9):
                raise ValueError(f"root {root} is not the fundamental mode")
            return root
    raise ValueError("no eigenvalue of the slab below k_infinity")


def reference_k(case: Case) -> float:
    """Exact eigenvalue of a case: closed form when every axis is zero flux or reflective;
    otherwise the slab determinant of the single axis named by the geometry, with the transverse
    axes reflective."""
    xs = case.xs
    buckling = [
        axis_buckling_sq(case.faces[2 * axis], case.faces[2 * axis + 1], case.lengths_cm[axis])
        for axis in range(3)
    ]
    known = [b for b in buckling if b is not None]
    if len(known) == 3:
        return k_at_buckling(xs, sum(known))
    if case.geometry == "cuboid":
        raise ValueError(f"case {case.name}: a cuboid needs zero-flux or reflective faces")
    slab_axis = AXIS_NAMES.index(case.geometry.removeprefix("slab_"))
    for axis, value in enumerate(buckling):
        if axis != slab_axis and value != 0.0:
            raise ValueError(f"case {case.name}: transverse axis {axis} is not reflective")
    minus = case.faces[2 * slab_axis]
    plus = case.faces[2 * slab_axis + 1]
    if minus != plus:
        raise ValueError(f"case {case.name}: the two faces of the slab differ")
    return slab_root(xs, 0.5 * case.lengths_cm[slab_axis], face_albedo(minus))


def slab_faces(axis: int, minus: FaceSpec, plus: FaceSpec) -> tuple[FaceSpec, ...]:
    """Six faces with the given pair on `axis` and reflective transverse faces."""
    faces = [REFLECTIVE_FACE] * FACE_COUNT
    faces[2 * axis] = minus
    faces[2 * axis + 1] = plus
    return tuple(faces)


def build_cases() -> list[Case]:
    """The reference cases, in the order of the committed table."""
    cases: list[Case] = []
    for axis, axis_name in enumerate(AXIS_NAMES):
        for label, face in (
            ("zero_flux", ZERO_FLUX_FACE),
            ("vacuum", VACUUM_FACE),
            ("albedo", ALBEDO_FACE),
        ):
            cases.append(
                Case(
                    name=f"slab_{axis_name}_{label}",
                    geometry=f"slab_{axis_name}",
                    faces=slab_faces(axis, face, face),
                    lengths_cm=SLAB_LENGTHS_CM,
                    xs=EXAMPLE_FUEL,
                )
            )
    for index, face_name in enumerate(FACE_NAMES):
        axis = index // 2
        if index % 2 == 0:
            minus, plus = REFLECTIVE_FACE, ZERO_FLUX_FACE
        else:
            minus, plus = ZERO_FLUX_FACE, REFLECTIVE_FACE
        cases.append(
            Case(
                name=f"face_{face_name}_reflective",
                geometry=f"slab_{AXIS_NAMES[axis]}",
                faces=slab_faces(axis, minus, plus),
                lengths_cm=SLAB_LENGTHS_CM,
                xs=EXAMPLE_FUEL,
            )
        )
    cases.append(
        Case(
            name="cuboid_zero_flux",
            geometry="cuboid",
            faces=(ZERO_FLUX_FACE,) * FACE_COUNT,
            lengths_cm=CUBOID_LENGTHS_CM,
            xs=EXAMPLE_FUEL,
        )
    )
    cases.append(
        Case(
            name="cuboid_mixed",
            geometry="cuboid",
            faces=(
                REFLECTIVE_FACE,
                ZERO_FLUX_FACE,
                ZERO_FLUX_FACE,
                ZERO_FLUX_FACE,
                ZERO_FLUX_FACE,
                REFLECTIVE_FACE,
            ),
            lengths_cm=CUBOID_LENGTHS_CM,
            xs=EXAMPLE_FUEL,
        )
    )
    return cases


def render_csv(cases: Sequence[Case]) -> str:
    """The table as text. Inputs use their shortest exact form; k_ref has 17 significant digits;
    the albedo columns are 0 for every kind except albedo."""
    lines = [",".join(CSV_COLUMNS)]
    for case in cases:
        fields = [case.name, case.geometry]
        for face in case.faces:
            albedo = face.albedo if face.kind is Kind.ALBEDO else (0.0, 0.0)
            fields += [face.kind.value, repr(albedo[0]), repr(albedo[1])]
        fields += [repr(length) for length in case.lengths_cm]
        xs = case.xs
        fields += [
            repr(xs.d1_cm),
            repr(xs.d2_cm),
            repr(xs.sigma_a1_per_cm),
            repr(xs.sigma_a2_per_cm),
            repr(xs.sigma_12_per_cm),
            repr(xs.nu_sigma_f1_per_cm),
            repr(xs.nu_sigma_f2_per_cm),
            f"{reference_k(case):.17g}",
        ]
        lines.append(",".join(fields))
    return "\n".join(lines) + "\n"


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=DESCRIPTION)
    parser.add_argument(
        "--write",
        action="store_true",
        help=f"write {REFERENCE_CSV.relative_to(REPO_ROOT)} instead of printing the table",
    )
    args = parser.parse_args(argv)
    text = render_csv(build_cases())
    if args.write:
        REFERENCE_CSV.write_text(text, encoding="utf-8")
        print(f"wrote {REFERENCE_CSV}")
    else:
        sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
