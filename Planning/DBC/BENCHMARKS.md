# Benchmarks

Verification and validation cases, mapped to [CONCEPTS](CONCEPTS.md), [CODE](CODE.md),
[DEFINITIONS](DEFINITIONS.md) and [TESTS](TESTS.md). Each section is one case family from the
theory manual's matrix (TM §19.2). "Equations checked" means a throwaway script on 2026-11-16
tested the printed equations (TM §19.1); it is not a test of the engine, and every case must
become a real test or run when its concept is built. A case is run on engine code where its
Comments say so; the others have not been run.

## Bare homogeneous slab and cuboid
**Concepts:** `CmfdSolve`, `EigenvalueSolve`
**Comments:** 2026-11-16, orchestrator: from TM §19.2, row "CMFD assembly, boundary conditions". Not run. 2026-11-17, orchestrator: reference and tolerances set in the milestone 1 plan §5; infinite medium and mirror symmetry added. 2026-11-26, orchestrator: case set settled at the freeze of sub-milestone 1.4 (plan §3): per axis, slabs with one kind on both faces (vacuum, zero flux, per-group albedo); per face, reflective against zero flux; cuboids with zero-flux and reflective faces. Mirror symmetry gains 1e-10 absolute in k; a neutron balance of 1e-9 is added (plan §5). Not yet run. 2026-11-27, orchestrator: run on engine code as `tests/verification/bare_homogeneous_test.cpp` (17 cases of `tests/verification/bare_homogeneous_reference.csv`, from `tools/references/bare_homogeneous.py`) and `tests/verification/cmfd_properties_test.cpp`; all pass. Slabs on 20, 40, 80 and 160 nodes along their axis (60 × 50 × 40 cm), cuboids (24 × 20 × 16 cm) on 3³, 6³, 9³ and 12³. Meshes changed from the first choice: the albedo slabs are pre-asymptotic at 10 nodes (observed order 1.84 for x from 10-20-40), and 24³ takes minutes per run in Debug because the incomplete-LU preconditioner is refactored every outer iteration. Observed orders from the three finest meshes all within 2 ± 0.1 (e.g. slab_x_albedo errors 4.968e-4, 1.384e-4, 3.576e-5, 9.019e-6; cuboid_zero_flux 1.577e-2, 3.818e-3, 1.687e-3, 9.469e-4). Finest-mesh errors 3.1e-7 to 1.0e-3. Every run converged at the verification tolerances (ε_k 1e-12, ε_L2 1e-11, ε_L∞ 1e-10, ε_erf 0.01, N_in 1, Wielandt shift 0.04): slabs 8–12 outer iterations, cuboids 14–38, one inner iteration each; final δ_k ≤ 2.9e-13, δ_L2 ≤ 9.7e-12, δ_L∞ ≤ 2.9e-11, residual ≤ 1.9e-12.
**Reference:** Analytic two-group eigenvalue: closed form for zero-flux and reflective faces; for vacuum and albedo faces the root of the 2×2 determinant of the exact two-group slab solution, computed by a reference script; k_∞ for an infinite medium.
**Tolerance:** Finite differences with zero coupling corrections converge at order 2; the observed order must be 2 within 0.1 from the three finest of at least four meshes, for every boundary kind on every face, with the finest-mesh error above 1e3 times the eigenvalue tolerance. Infinite medium within 1e-11 in k; mirrored problems equal within 1e-10 per node flux; verification tolerances throughout.

Tests the coarse-mesh assembly and every boundary condition without the kernels.

## One-dimensional fuel and reflector slab
**Concepts:** `NodalKernels`, `AnmKernel`, `NemKernel`, `InterfaceSolve`
**Comments:** 2026-11-16, orchestrator: TM §19.1 row 2, equations checked. Engine test not written. 2026-11-17, orchestrator: reference changed to the analytic solution, which must reproduce 1.0507067; analytic-kernel tolerance tightened (milestone 1 plan §5).
**Reference:** Analytic transfer-matrix solution from a reference script; it must reproduce the fine-mesh finite-difference value k = 1.0507067 (0.5 and 0.25 cm, Richardson-extrapolated) to its seven printed digits.
**Tolerance:** Analytic kernel within 1e-9 relative in k and in node fluxes on 30 cm and 15 cm nodes (an exact kernel differs only by iteration error and round-off; 1000 times the verification eigenvalue tolerance); expansion kernel within the script values, −8.9 and −1.0 pcm, to 0.1 pcm.

120 cm fuel and 30 cm reflector, reflective and vacuum ends, no transverse leakage. Also: the analytic and expansion kernels agree in the fine-mesh limit, and the node pair is symmetric under reflection.

## IAEA two- and three-dimensional PWR benchmarks
**Concepts:** `NodalKernels`, `TransverseLeakage`, `EigenvalueSolve`
**Comments:** 2026-11-16, orchestrator: TM §19.1 row 1 checked the 2D eigenvalue only (analytic kernel +2.3 pcm, expansion kernel +2.4 pcm at 10 cm nodes; Wielandt shift 0.04; fission source and k changes below 1e-9; 11 nodal updates, 203 outer iterations). 2026-11-17, orchestrator: the engine must first reproduce those script values within 0.1 pcm with the same options (milestone 1 plan §5).
**Reference:** Published reference k = 1.029585 for 2D. **Open:** the specification, the 3D reference and the reference assembly powers are not in Resources and must be obtained. Code against script: TM §19.1 row 1, finite difference 1.029650, expansion kernel 1.029609, analytic kernel 1.029608.
**Tolerance:** Code against script: 0.1 pcm, the script's printed resolution. Against the published reference: **Open:** set from the spread of the published nodal solutions in the obtained document, before any engine comparison.

Becomes an acceptance test only with the reference powers.

## Two-region slab with discontinuity factors
**Concepts:** `DiscontinuityFactors`, `InterfaceSolve`
**Comments:** 2026-11-16, orchestrator: from TM §19.2. Not run. 2026-11-17, orchestrator: tolerance made explicit (milestone 1 plan §5).
**Reference:** Analytic two-region transfer-matrix solution with the imposed flux jump, from a reference script.
**Tolerance:** 1e-10 relative in k and 1e-9 in node fluxes: the analytic kernel is exact in one dimension, so only iteration error and round-off remain, at 100 times the verification eigenvalue tolerance.

## Cross-section table interpolation
**Concepts:** `CrossSectionModel`, `BranchGrid`
**Comments:** 2026-11-16, orchestrator: from TM §19.2. Not run. 2026-11-17, orchestrator: round-off quantified (milestone 1 plan §5). 2026-11-19, orchestrator: run on engine code as `tests/verification/cross_section_interpolation_test.cpp`, in every preset. The reference is a four-axis non-uniform table (√T_D, ρ_m, T_m, C_B; 4×3×2×3 points) of designed multilinear functions with cross terms, different for every component of a family within a composition; 1/(3D) is the designed quantity for D. All 72 table points and 200 seeded interior states within 1e-14 relative; clamps exact per node, axis and side, end points never clamped, a missing axis never clamps, a length-one axis clamps every other value. 16 of 16 pass. No iteration is involved. Review fix: the designed weights first repeated every five components (a swap of components k and k+5 would have passed); the reference now has a self-check that they differ. 2026-11-20, orchestrator: the problem input is now format version 2; the whole-problem clamp cases set `allow_table_clamps` = 1 in the settings document, since states outside the table are otherwise rejected by input validation. Reference and tolerance unchanged; 16 of 16 pass in every preset.
**Reference:** The table itself, and data linear in every axis (linear in 1/(3D) for the diffusion coefficients).
**Tolerance:** Every table point and linear data within 1e-14 relative (a multilinear evaluation rounds within a few machine epsilons); every state outside the table counted as a clamp, exactly, per node, axis and side; an axis of length one clamps every other value.

## Rod cusping
**Concepts:** `ControlRods`, `CuspingCorrection`, `RoddedMixing`
**Comments:** 2026-11-16, orchestrator: the two tests that decide whether the kernel form of the three-node problem replaces fine-mesh finite differences (D5, TM §8.3). Not run.
**Reference:** Verification: k against tip position across one node, compared with a run whose node is split at the tip. Validation: BEAVRS rod bank worths (BEAVRS §3.2; NUREG §3.3.1).
**Tolerance:** k smooth in tip position (no change of sign of the second difference beyond the iteration tolerance) and equal to the split-node run within the eigenvalue tolerance. Bank worths: **Open:** tolerance set from the measurement uncertainty when the comparison is planned.

## Critical boron search
**Concepts:** `CriticalBoron`, `SteadyCoupledSolve`
**Comments:** 2026-11-16, orchestrator: from TM §19.2. Not run.
**Reference:** Verification: a fixed-boron run at the found concentration. Validation: BEAVRS hot zero power, 975 ppm with bank D at step 213 (BEAVRS Table 21; NUREG §3.3.1).
**Tolerance:** Same converged state within the steady-state tolerances. Measured boron: **Open:** set from the measurement and data uncertainty when the comparison is planned.

## Adjoint and reactivity edits
**Concepts:** `AdjointEdits`
**Comments:** 2026-11-16, orchestrator: from TM §19.2. Not run. 2026-11-17, orchestrator: adjoint tolerances set (milestone 1 plan §5); the reactivity parts wait for their milestone.
**Reference:** The forward eigenvalue; the static reactivity 1 − 1/k. For volume weighting: the fast forward flux of a problem with no group transfer and no thermal fission on a non-uniform mesh, whose fast-group operator is self-adjoint.
**Tolerance:** Forward and adjoint eigenvalues within 1e-10 at verification tolerances (eigenvalues of transposed matrices); fast adjoint proportional to the fast forward flux within 1e-9 per node in the self-adjoint problem; unexplained reactivity near zero; dynamic reactivity equals the static difference for small uniform changes, to second order in the change.

## Xenon and samarium
**Concepts:** `XenonSamarium`
**Comments:** 2026-11-16, orchestrator: TM §19.1 row 6, equations checked (one step of 3e4 s, relative difference below 3e-13).
**Reference:** Stiff ODE integration at tolerance 1e-12; the analytic two-nuclide solution for the shutdown peak time; equilibrium.
**Tolerance:** 1e-10 relative for the exact step (round-off with margin); equilibrium reached from zero.

## Precursor integration weights
**Concepts:** `PrecursorIntegration`
**Comments:** 2026-11-16, orchestrator: TM §19.1 row 3, equations checked (largest relative difference 3e-12; weights sum to 1 − κ within 4e-17).
**Reference:** Numerical quadrature of the convolution integral with a quadratic fission source, unequal steps, the eight library decay constants.
**Tolerance:** 1e-10 relative.

## Infinite-medium kinetics ramps
**Concepts:** `Kinetics`, `ThetaMethod`, `ExponentialTransform`
**Comments:** 2026-11-16, orchestrator: TM §19.1 row 4, equations checked: θ = 1/2 order 2.0 over 20 to 320 steps, also with the transform; θ = 1 order 1; a step change drops θ = 1/2 to order 1.
**Reference:** Stiff ODE integration (Radau, relative tolerance 1e-11) of the two-group, eight-precursor infinite medium with the central library node's data; ramps to 0.56 dollars of reactivity in 0.5 s and 1.2 dollars in 0.1 s.
**Tolerance:** Observed order within 0.1 of the theoretical order.

## Point-kinetics equivalence
**Concepts:** `Kinetics`, `AdjointEdits`
**Comments:** 2026-11-16, orchestrator: from TM §19.2. Not run.
**Reference:** Point kinetics with the edited parameters, for a spatially uniform problem.
**Tolerance:** Agreement to the time-integration error, shown by refining the step.

## Null transient
**Concepts:** `TransientStep`, `SteadyCoupledSolve`, `Kinetics`, `FuelRod`, `PrimaryLoop`
**Comments:** 2026-11-16, orchestrator: TM §18.6; the first test of every transient capability. Not run.
**Reference:** The converged steady state.
**Tolerance:** Power, temperatures, flows and precursor densities held within the steady-state tolerances for an arbitrary number of steps.

## LMW and NEACRP transient benchmarks
**Concepts:** `Kinetics`, `ControlRods`, `TransientStep`
**Comments:** 2026-11-16, orchestrator: PARCS §10.2.1 uses NEACRP case C1.
**Reference:** **Open:** the specifications and reference solutions are not in Resources and must be obtained.
**Tolerance:** **Open:** set from the published spread of nodal solutions once obtained.

## Decay heat
**Concepts:** `DecayHeat`
**Comments:** 2026-11-16, orchestrator: TM §19.1 row 6, equations checked (one step with linearly varying power, relative difference below 3e-13).
**Reference:** ODE integration at tolerance 1e-12; the stored decay-heat energy before shutdown.
**Tolerance:** 1e-10 relative for the step; energy released after shutdown equals the stored energy to round-off.

## Pin power reconstruction
**Concepts:** `PinPower`
**Comments:** 2026-11-16, orchestrator: from TM §19.2. Detector response (TM §13.7) is part of this concept. Not run.
**Reference:** Verification: a homogeneous node with the analytic 2D mode. Validation: full-core OpenMC pin fission rates at a library state; BEAVRS flux maps (BEAVRS §3.1–3.3; the hot-zero-power map uncertainty is 3.7 %).
**Tolerance:** Analytic mode reproduced to round-off; pin powers sum to the node power to round-off. Against OpenMC and flux maps: **Open:** set from the tally and measurement uncertainty when planned.

## Fuel rod conduction
**Concepts:** `FuelRod`
**Comments:** 2026-11-16, orchestrator: TM §19.1 row 5, equations checked: constant conductivity exact for 1 to 10 rings; Fink conductivity centreline error 0.75, 0.12, 0.030, 0.0075 K for 2, 5, 10, 20 rings.
**Reference:** Closed form for constant conductivity (Kolev Eqs. (2.6), (2.18), (2.31), (2.35)); the conductivity integral for temperature-dependent conductivity; the analytic response of a lumped rod for the transient.
**Tolerance:** Constant conductivity to round-off; temperature-dependent conductivity converging at order 2 in the ring count; energy balance to round-off.

## Coolant channels
**Concepts:** `CoolantChannel`, `FlowDistribution`, `Closures`
**Comments:** 2026-11-16, orchestrator: from TM §19.2. Not run.
**Reference:** Enthalpy rise P/W; hand calculation of each pressure-drop term.
**Tolerance:** Energy balance to round-off; enthalpy rise exact; pressure-drop terms to round-off; mesh refinement at the scheme's order.

## Water properties
**Concepts:** `WaterProperties`
**Comments:** 2026-11-16, orchestrator: from TM §19.2. Not run.
**Reference:** The verification tables printed in the IAPWS-IF97 release (**not in Resources**).
**Tolerance:** The digits printed in those tables.

## Primary loop checks
**Concepts:** `PrimaryLoop`, `LoopClosure`, `SteamGenerator`, `Pump`
**Comments:** 2026-11-16, orchestrator: the checks of TM §17.9. Not run.
**Reference:** Loop energy, mass and boron balances; transport delays against pipe volume over flow.
**Tolerance:** Balances to round-off; delays exact on the transport mesh.

## Coupled steady state
**Concepts:** `SteadyCoupledSolve`, `FieldExchange`, `ConvergenceTest`
**Comments:** 2026-11-16, orchestrator: from TM §19.2. Not run.
**Reference:** Verification: the same state for different nodal update counts, relaxation factors and initial guesses. Validation: BEAVRS hot zero power and hot full power states, power defect (NUREG §3.3).
**Tolerance:** Verification within the steady-state tolerances. Validation: **Open:** set from the measurement uncertainty when the comparison is planned.

## Coupled transients
**Concepts:** `TransientStep`, `TimeStepControl`, `BalanceChecks`
**Comments:** 2026-11-16, orchestrator: from TM §19.2. Includes the PARCS-sequence option against the converged iteration (D1).
**Reference:** Time-step and mesh refinement; energy balance; published PARCS and SIMULATE results for BEAVRS (Vision §6) as they become available.
**Tolerance:** Observed order matching the scheme; energy balance to round-off. Code-to-code: **Open:** set when the published results are obtained.

## Full-core OpenMC reference at library states
**Concepts:** `FullCoreReference`, `CrossSectionModel`, `NodalKernels`
**Comments:** 2026-11-16, orchestrator: the present library comes from these runs (TM §19.3); the nodal solution at a library state must reproduce their node powers and eigenvalue.
**Reference:** Full-core OpenMC eigenvalue and node-wise tallies at the same state.
**Tolerance:** **Open:** set from the tally standard deviations and the homogenization error once lattice data exist; with full-core data both errors are small, so this checks the solver more than the method.

## Present-library reproduction
**Concepts:** `LibraryContract`, `FullCoreReference`
**Comments:** 2026-11-17, orchestrator: the library writer re-extracts the present library from its 72 statepoints before its contract file replaces the old one (milestone 1 plan §3, 1.2). 2026-11-18, orchestrator: run; passed. 72 runs, 13 raw datasets (D, Σa, raw Σ12, νΣf, κΣf in eV, 1/v per group; β and λ over eight delayed groups), 7 435 800 values bit-identical with NaN at the same entries; the result is stored in the library's provenance. The replaced library holds 227 invalid entries of 275 400, all in corner columns; per quantity D1 46, D2 79, Σa1 41, Σa2 77 (TM §19.3), effective Σ12 222 (the 220 raw zeros plus 2 corner entries made negative by Eq. (3.5), each within its standard deviation), 1/v 41 and 77.
**Reference:** The present library file.
**Tolerance:** Every raw quantity bit-identical, with NaN at the same entries: the same extraction code reads the same statepoints.

## BEAVRS on the present library
**Concepts:** `ProblemInput`, `CrossSectionModel`, `EigenvalueSolve`, `RunRecord`
**Comments:** 2026-11-17, orchestrator: a code-path exercise, not a validation; the present library has no reflectors and 2–6 % tally noise (TM §19.3). Not run.
**Reference:** None; no accuracy claim is made.
**Tolerance:** Converged at the production tolerances with the hybrid kernel and with finite differences; no clamps; a valid run record; every flux finite and positive in the domain; residual reported.
