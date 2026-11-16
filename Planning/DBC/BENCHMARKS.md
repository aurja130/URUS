# Benchmarks

Verification and validation cases, mapped to [CONCEPTS](CONCEPTS.md), [CODE](CODE.md),
[DEFINITIONS](DEFINITIONS.md) and [TESTS](TESTS.md). Each section is one case family from the
theory manual's matrix (TM §19.2). "Equations checked" means a throwaway script on 2026-11-16
tested the printed equations (TM §19.1); it is not a test of the engine, and every case must
become a real test or run when its concept is built. No case has been run on engine code.

## Bare homogeneous slab and cuboid
**Concepts:** `CmfdSolve`, `EigenvalueSolve`
**Comments:** 2026-11-16, orchestrator: from TM §19.2, row "CMFD assembly, boundary conditions". Not run.
**Reference:** Analytic eigenvalue from the geometric buckling of a bare homogeneous body.
**Tolerance:** Finite differences with zero coupling corrections converge at order 2 under mesh refinement; the observed order must be 2 within 0.1 over at least three meshes.

Tests the coarse-mesh assembly and every boundary condition without the kernels.

## One-dimensional fuel and reflector slab
**Concepts:** `NodalKernels`, `AnmKernel`, `NemKernel`, `InterfaceSolve`
**Comments:** 2026-11-16, orchestrator: TM §19.1 row 2, equations checked. Engine test not written.
**Reference:** Fine-mesh finite difference at 0.5 and 0.25 cm, Richardson-extrapolated: k = 1.0507067.
**Tolerance:** Analytic kernel within 0.01 pcm on 30 cm and 15 cm nodes (an exact kernel differs only by round-off); expansion kernel within the script values, −8.9 and −1.0 pcm, to 0.1 pcm.

120 cm fuel and 30 cm reflector, reflective and vacuum ends, no transverse leakage. Also: the analytic and expansion kernels agree in the fine-mesh limit, and the node pair is symmetric under reflection.

## IAEA two- and three-dimensional PWR benchmarks
**Concepts:** `NodalKernels`, `TransverseLeakage`, `EigenvalueSolve`
**Comments:** 2026-11-16, orchestrator: TM §19.1 row 1 checked the 2D eigenvalue only (analytic kernel +2.3 pcm, expansion kernel +2.4 pcm at 10 cm nodes; Wielandt shift 0.04; fission source and k changes below 1e-9; 11 nodal updates, 203 outer iterations).
**Reference:** Published reference k = 1.029585 for 2D. **Open:** the specification, the 3D reference and the reference assembly powers are not in Resources and must be obtained.
**Tolerance:** **Open:** set once the reference powers are in hand; published nodal solutions at 10 cm reach a few pcm in k.

Becomes an acceptance test only with the reference powers.

## Two-region slab with discontinuity factors
**Concepts:** `DiscontinuityFactors`, `InterfaceSolve`
**Comments:** 2026-11-16, orchestrator: from TM §19.2. Not run.
**Reference:** Analytic two-region solution with an imposed flux jump.
**Tolerance:** Round-off, about 1e-10 relative, since the analytic kernel is exact in one dimension.

## Cross-section table interpolation
**Concepts:** `CrossSectionModel`, `BranchGrid`
**Comments:** 2026-11-16, orchestrator: from TM §19.2. Not run.
**Reference:** The table itself, and data linear in every axis.
**Tolerance:** Every table point reproduced to round-off; linear data reproduced exactly; every state outside the table counted as a clamp.

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
**Comments:** 2026-11-16, orchestrator: from TM §19.2. Not run.
**Reference:** The forward eigenvalue; the static reactivity 1 − 1/k.
**Tolerance:** Forward and adjoint eigenvalues equal within the eigenvalue tolerance; unexplained reactivity near zero; dynamic reactivity equals the static difference for small uniform changes, to second order in the change.

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
