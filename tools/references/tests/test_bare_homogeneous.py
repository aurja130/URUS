# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Aurora Jahan
# Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.
"""Tests of the bare homogeneous analytic reference and its committed table."""

from __future__ import annotations

import math

import pytest

from tools.references import bare_homogeneous as bh


def test_closed_form_satisfies_row_one_balance() -> None:
    xs = bh.EXAMPLE_FUEL
    b = (math.pi / 60.0) ** 2
    k = bh.k_at_buckling(xs, b)
    a2 = xs.sigma_12_per_cm / (xs.d2_cm * b + xs.removal2)
    residual = (xs.d1_cm * b + xs.removal1 - xs.nu_sigma_f1_per_cm / k) - (
        xs.nu_sigma_f2_per_cm / k
    ) * a2
    assert abs(residual) < 1.0e-14


def test_infinite_medium_matches_hand_value() -> None:
    xs = bh.EXAMPLE_FUEL
    expected = 0.029375 / 0.027
    assert bh.k_infinity(xs) == pytest.approx(expected, rel=1.0e-14)


def test_zero_flux_slab_root_matches_closed_form() -> None:
    xs = bh.EXAMPLE_FUEL
    half = 30.0
    numeric = bh.slab_root(xs, half, (bh.ZERO_FLUX_ALPHA, bh.ZERO_FLUX_ALPHA))
    closed = bh.k_at_buckling(xs, (math.pi / 60.0) ** 2)
    assert numeric == pytest.approx(closed, abs=1.0e-10)


def test_weak_robin_slab_approaches_infinite_medium() -> None:
    xs = bh.EXAMPLE_FUEL
    k_inf = bh.k_infinity(xs)
    numeric = bh.slab_root(xs, 30.0, (1.0e-9, 1.0e-9))
    assert numeric < k_inf
    assert abs(numeric - k_inf) < 1.0e-7


def test_boundary_condition_ordering() -> None:
    xs = bh.EXAMPLE_FUEL
    half = 30.0
    k_zero = bh.slab_root(xs, half, (bh.ZERO_FLUX_ALPHA, bh.ZERO_FLUX_ALPHA))
    k_vacuum = bh.slab_root(xs, half, (bh.VACUUM_ALPHA, bh.VACUUM_ALPHA))
    k_inf = bh.k_infinity(xs)
    assert k_zero < k_vacuum < k_inf


def is_determinant_case(case: bh.Case) -> bool:
    """True when the slab axis has a vacuum or albedo face (reference from the determinant)."""
    if not case.geometry.startswith("slab_"):
        return False
    axis = bh.AXIS_NAMES.index(case.geometry.removeprefix("slab_"))
    minus, plus = case.faces[2 * axis], case.faces[2 * axis + 1]
    return bh.axis_buckling_sq(minus, plus, case.lengths_cm[axis]) is None


@pytest.mark.parametrize(
    "case",
    [c for c in bh.build_cases() if is_determinant_case(c)],
    ids=lambda c: c.name,
)
def test_slab_reference_is_a_determinant_root(case: bh.Case) -> None:
    axis = bh.AXIS_NAMES.index(case.geometry.removeprefix("slab_"))
    half = 0.5 * case.lengths_cm[axis]
    k_ref = bh.reference_k(case)
    alpha = bh.face_albedo(case.faces[2 * axis])
    below = bh.slab_determinant(case.xs, k_ref - 1.0e-9, half, alpha)
    above = bh.slab_determinant(case.xs, k_ref + 1.0e-9, half, alpha)
    assert (below > 0.0) != (above > 0.0)


def test_committed_table_matches_generator() -> None:
    cases = bh.build_cases()
    assert len(cases) == 17
    committed = bh.REFERENCE_CSV.read_text(encoding="utf-8")
    assert committed == bh.render_csv(cases)
