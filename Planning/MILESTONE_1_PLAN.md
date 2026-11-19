# Milestone 1 Plan · Core engine: steady-state nodal neutronics

**Status.** Approved by the Captain, 2026-11-17, as a working plan: problems found during
implementation are corrected through §9. Each sub-milestone's contract freezes when its
session starts (§4).

**Outcome** (Project plan §3). The engine reads a problem input and a nodal library, solves
the steady-state two-group eigenvalue problem with CMFD and the ANM/NEM hybrid, computes the
adjoint flux, and writes the node fluxes, powers and the run record. BEAVRS runs end to end
on the present library as a code-path exercise.

**Closes on** (Project plan §2, §3). Bare homogeneous slab and cuboid; one-dimensional fuel
and reflector slab; two-region slab with discontinuity factors; cross-section table
interpolation; IAEA two- and three-dimensional PWR benchmarks; forward and adjoint
eigenvalues equal; a converged BEAVRS run on the present library. IAEA 3D is the non-BEAVRS
core of Project plan §2 item 3.

## 1. Decisions and prerequisites

### 1.1 Decided at the start, 2026-11-17

| Item | Decision | Consequence for this plan |
|---|---|---|
| P6 | C++23 | `std::expected` carries input and data errors, `std::mdspan` indexes node arrays, `std::print` formats output; no `fmt` dependency. Both pinned compilers build all three, with Eigen 5.0.1 and the HDF5 C++ API, under `-Wall -Wextra -Wpedantic -Werror` (probe run 2026-11-17). Students need GCC ≥ 14 or Clang ≥ 18 (milestone 6). |
| P1 | Data milestones DM1 and DM2 beside the engine milestones | DM1's Start items are presented when the library contract freezes (end of 1.2), not before. |
| Library route | The submodule re-extracts the present library from its 72 statepoints and writes it under the contract | Replaces Project plan §3 scope item 2's "converter". One library writer, which DM1 extends. No transport run: statepoints are only read. |
| Library file | The contract file replaces `beavrs_kinetics_library.hdf5` in the submodule | One library format; the old layout is not read by anything. |
| Test framework | GoogleTest 1.17 (`gtest-devel`) | CTest discovers each test; value-parameterized tests carry the sweeps of AGENTS.md Review rule 5; tests check that a violated invariant check stops the program. |

### 1.2 Prerequisites and when they are presented

| Category | Item | Due | Presented |
|---|---|---|---|
| Obtain | Install GoogleTest: `sudo dnf install gtest-devel` (1.17.0, BSD-3-Clause) | Start of 1.1 | Done 2026-11-17 (`gtest-devel-1.17.0-2.fc44`) |
| Obtain | IAEA 2D and 3D PWR specifications, reference eigenvalues and assembly powers (ANL-7416 Suppl. 2, TM Appendix D) | Start of 1.5 (scope item 5) | When 1.5 is planned |
| Obtain | Statepoint archive published on Zenodo | Before 1.2 starts | Waived 2026-11-18 by the Captain: risk accepted for milestone 1; the upload moved to DM1's close (Project plan §5.2) |

The statepoints in `~/Programming/OpenMC_inputs/BEAVRS_minimal/beavrs_parameter_sweep/`
(610 MB) are in no repository. If they are lost, the present library can only be rebuilt
with 72 new OpenMC runs (8.3 h). The library writer records the SHA-256 of each one it reads.
**Risk accepted (2026-11-18):** the Captain keeps no backup during milestone 1; losing the
files costs the 72 runs, or turns the bit-identical re-extraction benchmark into a
statistical one. They go to Zenodo together with the DM1 statepoints when DM1 closes. The
README, `SHA256SUMS` of the present files and the BEAVRS MIT notice stay in
`~/Programming/OpenMC_inputs/zenodo_beavrs_parameter_sweep/`; the README needs a limitations
section first (low statistics, boron fixed at 975 ppm, rods all in or all out, code exercise
only). This plan does not commit them.

## 2. Engineering baseline (all sub-milestones)

Fixed in `CODING_STANDARDS.md` in 1.1. Listed here so that the Captain approves them with
the plan.

- **Language and build.** C++23, extensions off; CMake ≥ 3.28 with presets; Ninja.
  Dependencies, all GPL-3.0-compatible: HDF5 C++ API 1.14.6 (BSD-style), Eigen 5.0.1
  (MPL-2.0, sparse matrices and BiCGSTAB), GoogleTest 1.17 (BSD-3-Clause). Third-party
  headers are included as system headers so that warnings-as-errors applies to our code only.
- **Names** (approved 2026-11-17). Library target `nts_core` and namespace `nts` ("nodal
  thermal-hydraulic simulator"); executable `urus` (Uppsala universitets reaktorsimulator).
  Types `PascalCase`; functions and variables `snake_case`; members end in `_`. Physics
  symbols follow TM §2. Every variable with a unit names it where the type does not
  (`width_cm`, `power_w`).
- **Units.** Inside the engine: cm, s, K, kg m⁻³, ppm, W, J. Conversions happen only when
  reading input files; the contracts name one accepted unit per quantity and reject the rest.
- **Layout.** `src/nts/<area>/` (`common`, `io`, `xs`, `cmfd`, `nodal`, `solve`); `app/`
  (the `urus` main); `tests/unit/`, `tests/verification/`, `tests/end_to_end/`;
  `tools/inputs/` (Python problem-input writer); `tools/references/` (reference scripts);
  `tools/ci/` (local CI); `docs/contracts/` (problem input, nodal library, output).
- **Errors.** Input and data errors are collected as diagnostics in `std::expected` and
  reported together, each naming the file, dataset and entry. HDF5 exceptions are caught at
  the I/O boundary. Internal invariants use the invariant check `NTS_ASSERT`, which is on in
  every build including release and aborts with file, line and message. No raw
  `new`/`delete`, no mutable globals, `double` throughout, never `-ffast-math`.
- **Floating point.** Debug and sanitizer builds trap invalid operations, division by zero
  and overflow in the executable and the tests (glibc `feenableexcept`), so a NaN stops the
  run where it is made.
- **Node ordering.** x fastest, then y, then z, as in the present library. Group index 0 is
  fast and 1 is thermal; delayed groups are 0–7.
- **Equations.** Every function that implements a TM equation cites it, for example
  `// TM Eq. (5.23)`. A departure from PARCS or SIMULATE-5 needs the Captain's approval
  before code (AGENTS.md).
- **Tests.** Unit tests build their HDF5 fixtures in C++, so they need no Python. End-to-end
  tests run `urus` on inputs written by `tools/inputs/`. Benchmark references come from
  committed reference scripts that share no code with the engine.

### 2.1 Iteration tolerances

| Set | ε_k | ε_L2 | ε_L∞ | Inner ε_erf | Used for |
|---|---|---|---|---|---|
| Production (PARCS defaults, TM §4.6, §4.5) | 5e-6 | 5e-5 | 5e-4 | 0.01 | BEAVRS run; solver option defaults |
| Verification | 1e-12 | 1e-11 | 1e-10 | 0.01 | Every benchmark of §5 except BEAVRS and the TM §19.1 script comparison |

Justification: each verification tolerance of §5 must exceed the iteration error by at least
100×, so that an iteration error can never pass for agreement. The inner tolerance does not
change the converged solution (TM §4.5). Every run also reports the residual
‖Mφ − λFφ‖₂/‖λFφ‖₂ and the largest change of D̂ in the last nodal update (TM §4.6).

## 3. Sub-milestones

Ordered so that each can be tested against the ones before it (TM §22). One session each.
Each sub-milestone lists the DBC nodes it builds or changes; at contract freeze those nodes
become `partial`.

### 1.1 Foundations

**State.** Done 2026-11-17 (reviewed and merged into `milestone-1`; gate results in
`CURRENT_PROJECT_STATE.md` §3).

**Builds.** `CODING_STANDARDS.md` (§2 above, plus clang-format and clang-tidy settings:
100 columns as in ruff, 4-space indent; tidy families bugprone, performance, modernize,
readability, misc and clang-analyzer, warnings as errors). `CURRENT_PROJECT_STATE.md`
(milestone and sub-milestone state; every prerequisite with its state; last gate results).
`CMakeLists.txt` and `CMakePresets.json`. `.clang-format`, `.clang-tidy`. The local CI
script `tools/ci/local_ci.py`. The toolchain manifest extended with GoogleTest and Eigen
(`pkg-config --modversion`). `NTS_ASSERT` and the floating-point trap.

**Presets** (configure, build and test each):

| Preset | Compiler | Build | Purpose |
|---|---|---|---|
| `dev-gcc` | GCC | Debug | Default development; warnings as errors |
| `dev-clang` | Clang | Debug | Writes `build/dev-clang/compile_commands.json` for clangd and clang-tidy |
| `asan-ubsan-clang` | Clang | Debug | Address and undefined-behaviour sanitizers, no recovery |
| `tsan-gcc` | GCC | Debug | Thread sanitizer (separate build, LOCAL_ENVIRONMENT) |
| `release-gcc` | GCC | Release, `-O3` | Invariant checks stay on |

**Local CI**, cheapest step first; a failed step is a failed run: toolchain check, license
header check, `clang-format --dry-run -Werror`, `ruff check`, `ruff format --check`,
`basedpyright`, `pytest`, `check_dbc.py`, every preset (configure, build, `ctest`), and
`clang-tidy` on the `dev-clang` database. Prints one pass/fail line per step.

**DBC.** `dbc.toml` `test_globs` gains `tests/**/*_test.cpp` so that engine tests count; this
is a configuration change the Captain approves with this plan. Nodes: `BuildPresets`,
`LocalCi`, `ToolchainPin`, and `RuntimeChecks` (added in review: the invariant check and the
floating-point trap fitted no node).

**Acceptance.** All presets build and pass; the local CI passes; a test shows that
`NTS_ASSERT` aborts with its message; another shows that a floating-point trap stops the
program in debug builds.

### 1.2 Library contract and the present library

**State.** Done 2026-11-19 (reviewed and merged into `milestone-1`; gate results in
`CURRENT_PROJECT_STATE.md` §3).

**Contract** (`docs/contracts/nodal_library.md`, format version 1, frozen at the end of 1.2
because DM1 writes to it):

- Root attributes: format version, energy groups = 2, delayed groups = 8, group boundary
  0.625 eV, generator name and commit.
- Composition sets `/composition_sets/<set>/`: each set has its own axes, drawn from the
  vocabulary fuel temperature (K), moderator density (kg m⁻³), moderator temperature (K) and
  boron (ppm). Each axis is strictly ascending, with length ≥ 1. A length-1 axis is a fixed
  value, and any other state on that axis is a clamp. `names[N]` lists the compositions.
- Quantities shaped `[N, axes…]` (`[N, axes…, 8]` for β and λ), each with `value`,
  `std_dev` and a `units` attribute: D₁, D₂ (cm); Σ_a1, Σ_a2, Σ₁₂ as the effective
  down-scatter (TM Eq. (3.5)), Σ₂₁, νΣ_f1, νΣ_f2 (cm⁻¹); κΣ_f1, κΣ_f2 (J cm⁻¹); 1/v₁, 1/v₂
  (s cm⁻¹); β (1); λ (s⁻¹). Optional, all absent from the present library: Σ_f (L3) and
  discontinuity factors (6 faces × 2 groups; L10). Composition sets and optional quantities
  are additive; a later milestone (rod linkage, milestone 2) adds them without changing
  what version 1 means.
- `valid[N, axes…]` (uint8): 0 marks an entry that holds no physical value (L14). The writer
  sets 0 where any quantity breaks its entry rule (`docs/contracts/nodal_library.md` §3.3:
  non-finite, D ≤ 0, Σ_a ≤ 0, Σ₁₂ ≤ 0, 1/v ≤ 0, a negative σ, β or λ, …).
- Provenance group: per entry the statepoint SHA-256 and the full-core k ± σ of the run it
  came from; OpenMC version, nuclear data library, batches, inactive batches, particles.

**Submodule** (commits in `external/BEAVRS_minimal`, pushed to `origin`, then a pointer
update here):
- `run_model.py` and `sweep.py` share one extraction function (`openmc.mgxs` from a
  statepoint: mean and standard deviation of every quantity, Σ₂₁, flux).
- A library writer re-extracts all 72 states from a statepoint directory given as an
  argument and writes the contract file. Rodded and unrodded data become separate
  compositions (TM §7.1); the moderator-density axis is sorted ascending; κ is converted
  from eV to J. β's standard deviation is propagated to first order without correlations,
  and the attribute says so.
- **Reproduction check** before replacement: every re-extracted raw quantity (Σ₁₂ before
  the Eq. (3.5) correction) equals the present file's value bit for bit, with NaN at the
  same entries (same `openmc.mgxs` code path). A difference stops the replacement and is
  reported.

**Engine.** Reads the library into memory and validates it: format version; group counts
(any other count is rejected, O2); units; axes ascending and finite; shapes consistent;
every quantity and standard deviation present; a zero delayed abundance is allowed (P7).
Validity is checked per composition actually used (1.3): a used composition with any
invalid entry is rejected, naming the entry.

**DBC.** Nodes: `LibraryContract`, `FullCoreReference` (the writer), `LibraryReader` (the
library's rules); `InputValidation` redefined, planned until 1.3 (§9, 2026-11-18).

**Acceptance.** Every rule has a rejection test naming composition, quantity and entry,
swept over both energy groups and all eight delayed groups, NaN, ±Inf, a length-1 axis and a
non-ascending axis. An end-to-end test reads the replaced library and finds 7650
compositions with the same invalid-entry counts per quantity as TM §19.3, all in the 32
corner columns.

### 1.3 Problem input and cross-section model

**Contract** (`docs/contracts/problem_input.md`, format version 1, neutronics part; later
milestones add rods, channels, loop and knobs):
- Geometry: node widths along x, y and z (cm, non-uniform allowed); a composition map
  `int32[z][y][x]` indexing the input's composition list, with −1 marking an excluded
  position outside the domain.
- Boundary conditions per group on each of the six bounding faces, plus one for exterior
  faces (faces next to an excluded position), as the albedo α_b of TM Eq. (4.4),
  J_out = α_b φ_s: reflective (0), vacuum (Marshak, 1/2), zero flux (10³⁰), or an input
  albedo per group.
- Library binding: library path (relative to the input) and the library composition name
  of each input composition.
- Operating state: thermal power (W, > 0); node state as uniform values or `[z][y][x]`
  arrays of Doppler temperature, moderator density, moderator temperature and boron.
- Solver options with the defaults of §2.1 and TM: kernel ∈ {finite difference (D̂ = 0),
  NEM, ANM, hybrid}, default hybrid; switch threshold 0.01 (O8); Wielandt δk = 0.04 (TM
  §4.4); nodal update controls (TM §6.1); outer and inner iteration limits; adjoint on/off.

**Input validation**: positive finite widths; map shape matching the widths; at least one
included position; every index resolves to a library composition; per-node states finite
and physical; input albedos finite and ≥ 0 (TM §4.2); the validity of used compositions
(1.2). All errors are reported together.

**Python input writer** (`tools/inputs/`): dataclasses written to the contract with h5py;
pytest round trip. It does not validate; the engine is the authority.

**Cross-section model** (TM §7.1–§7.5, steps 1 and 4 of §7.4 only; rods and poisons are
later milestones): multilinear interpolation of Eq. (7.2) in (√T_D, ρ_m, C_B, and T_m when
the set has that axis), interpolating 1/(3D_g) and recovering D_g. Clamps are counted per
node, axis and side. For the present library the moderator states are one coupled axis
entered with ρ_m (TM §7.3).

**DBC.** Nodes: `ReactorDescription`, `OperatingState`, `SolverOptions`, `LibraryBinding`,
`InputValidation`, `CrossSectionModel`.

**Acceptance.** Cross-section table interpolation benchmark (§5); validation rejection tests
for every rule above, for interior, edge and corner nodes and for every face.

### 1.4 CMFD with zero coupling corrections, eigenvalue, output

**Builds.** TM §3.3 normalization; §4.1–§4.3 assembly with Eq. (4.6) in the corrected form of
TM Appendix C item 1; boundary conditions Eq. (4.4) on bounding and exterior faces;
Wielandt-shifted outer iteration Eqs. (4.8)–(4.9); BiCGSTAB with Eigen's incomplete-LU
preconditioner (TM §4.5 permits a simpler preconditioner than BILU3D). The inner iteration
stops when the residual has fallen by ε_erf relative to its starting value, or after N_in
iterations. Eigen's own criterion is relative to the right-hand side, so the tolerance is
rescaled at each call. Convergence by Eq. (4.10) plus the residual.

**Output contract** (`docs/contracts/output.md`): k_eff; flux `[2][z][y][x]`; node power (W)
and power density; the run record: convergence state (outer and inner iterations, nodal
updates, δ_k, δ_L2, δ_L∞, residual, largest D̂ change), clamp counts by axis and side, methods
and every solver option as used, SHA-256 of input and library, engine version and commit,
preset, wall time; a valid/invalid flag with the reason. `urus <input> -o <output>`. Exit
status: 0 valid result; 2 input or library rejected; 3 not converged; 4 converged with clamps.
The output is written in the cases 0, 3 and 4.

**DBC.** Nodes: `CmfdSolve`, `EigenvalueSolve`, `FieldOutput`, `RunRecord`. An L3 rule
diagram of the eigenvalue solve is added if the batch makes its steps real (depth
discipline).

**Acceptance.** Bare homogeneous slab and cuboid benchmark (§5); infinite medium; mirror
symmetry; neutron balance; exit status per case.

### 1.5 Nodal kernels and the interface solve

**Builds.** NEM (TM Eqs. (5.8)–(5.15)); ANM (5.16)–(5.22) with the implementation notes of
TM §5.5 (series of sin w/w and sinh w/w near w = 0; harmonic mode scaled by cosh w₁); the
two-node solve (5.23)–(5.24) with
discontinuity factors (one when the library carries none); the one-node boundary solve
(5.25)–(5.26); the kernel switch (5.27); D̂ from the interface current; the nonlinear
iteration of TM §6.1 and the safeguards of §6.2 (faces with non-positive flux sums are
skipped and counted; D̂ is never clipped; faces with |D̂| > D̃ are counted and reported).
Transverse leakage comes in 1.6. Until then, input validation rejects a nodal kernel on a
problem with more than one node across any transverse direction.

**DBC.** Nodes: `NodalKernels`, `NemKernel`, `AnmKernel`, `KernelSwitch`,
`DiscontinuityFactors`, `InterfaceSolve`.

**Acceptance.** One-dimensional fuel and reflector slab; two-region slab with discontinuity
factors (§5). Unit tests: ANM equals NEM in the fine-mesh limit; the node pair is symmetric
under reflection; switch thresholds 0 (ANM everywhere) and above 1 (NEM everywhere); the
decoupled branch Σ₁₂ = 0; no overflow at a large node buckling; the series branch continuous
with the closed form at its threshold; both groups and every boundary kind on both sides.

### 1.6 Transverse leakage and the three-dimensional nodal solution

**Builds.** Quadratic transverse leakage (TM Eqs. (5.4)–(5.6)) with the ghost node of O7 at
boundaries and next to excluded positions; the nonlinear iteration in two and three
dimensions; the 1.5 restriction removed.

**DBC.** Node: `TransverseLeakage`; `NodalKernels` and `CmfdSolve` change.

**Acceptance.** IAEA 2D and 3D benchmarks (§5); the 1D cases of 1.5 unchanged when run as
thin 3D problems with reflective transverse faces.

### 1.7 Adjoint, BEAVRS on the present library, milestone gate

**Builds.** The adjoint of the final CMFD operator with D̂ frozen (TM §9.1, Eq. (9.1)), with
volume weighting applied once, to the transposed volume-integrated system; adjoint flux and
eigenvalue in the output. `tools/inputs/` gains the BEAVRS writer for the present library:
15 × 15 × 17 nodes, radial pitch and axial layers from the library's mesh; the 32 corner
columns excluded using the core map in the submodule's `beavrs` package; vacuum (Marshak) on
every exterior face; unrodded compositions; T_D = 750 K and ρ_m = 740 kg m⁻³ (both between
grid values, so interpolation is exercised without clamps); 975 ppm (the only boron value);
thermal power from the BEAVRS specification, cited.

**DBC.** `AdjointEdits` splits into an L3 diagram (adjoint flux, dynamic reactivity,
point-kinetics parameters); the adjoint flux becomes `implemented`, the rest stays
`planned`.

**Gate.** §6.

## 4. Contract freeze and status flips

When a sub-milestone's session starts, its contract (the "Builds" and "Contract" parts
above, with any change the Captain makes) freezes, and its nodes become `partial` in the same
commit. When it closes, they become `implemented`, or `planned` if deferred. The problem
input, library and output contracts are versioned documents. A change after freeze needs a
new format version, unless the change is additive.

## 5. Benchmarks, references and tolerances

Every engine run is converged to the verification tolerances of §2.1 unless stated
otherwise, and every reported result carries its convergence state.

| Case | Reference | Tolerance | Justification |
|---|---|---|---|
| Bare homogeneous slab and cuboid, finite difference | Analytic two-group eigenvalue: closed form for zero-flux and reflective faces; for vacuum (Marshak) and albedo faces, the root of the 2×2 determinant of the exact two-group slab solution (reference script, scipy root finder at 1e-14) | Observed order 2 ± 0.1 from the three finest of ≥ 4 uniform meshes, each boundary kind on each of the six faces (1D slabs rotated to x, y, z) and the cuboid | Node-centred finite differences are second order (TM §4.1). The finest-mesh error must exceed 1e3 × ε_k, so the observed order is not polluted by iteration error |
| Infinite medium (all faces reflective) | k_∞ of the two-group data, closed form | 1e-11 absolute in k | Iteration error ≤ 1e-12; 10× margin for round-off |
| Mirror symmetry | The mirrored solution | 1e-10 relative per node flux | Equal to ε_L∞ |
| One-dimensional fuel and reflector slab | Analytic transfer-matrix solution (reference script). It must reproduce the TM §19.1 value k = 1.0507067 to its 7 printed digits, or the discrepancy is resolved first | ANM: 1e-9 relative in k and in node fluxes, on 30 cm and 15 cm nodes. NEM: error −8.9 and −1.0 pcm (TM §19.1) within 0.1 pcm | ANM is exact in 1D without transverse leakage (TM §5.5), so only iteration and round-off remain: 1000× ε_k. NEM's error is its truncation; the script values carry 0.1 pcm resolution |
| Two-region slab with discontinuity factors | Analytic transfer-matrix solution with the imposed flux jump (reference script) | 1e-10 relative in k, 1e-9 in node fluxes | ANM is exact in 1D; 100× ε_k |
| Cross-section table interpolation | The table; data linear in each axis (linear in 1/(3D) for D) | 1e-14 relative at table points and for linear data; clamp counts exact per node, axis and side; length-1 axes clamp every other value | A multilinear evaluation of 2³–2⁴ corners rounds within a few machine epsilons (2.2e-16); 1e-14 leaves margin and still catches any wrong weight |
| IAEA 2D, code against script | TM §19.1 row 1: finite difference 1.029650, NEM 1.029609, ANM 1.029608, at 10 cm nodes with that run's options (albedo 0.4692, Wielandt 0.04, k and fission-source changes below 1e-9) | 0.1 pcm | Same equations and options; the script printed six decimals (0.1 pcm); its iteration error is far below that |
| IAEA 2D and 3D, against the published reference | Published k (2D 1.029585) and assembly powers (to be obtained) | **Open:** set at the start of 1.5 from the spread of the published nodal solutions in the obtained document, before any engine comparison | A tolerance chosen after seeing the result proves nothing |
| Forward and adjoint eigenvalues | The forward eigenvalue | 1e-10 absolute, for finite difference and for frozen nodal D̂, both groups, every boundary kind | The two are eigenvalues of transposed matrices, equal in exact arithmetic; each is converged to ε_k = 1e-12 |
| Adjoint volume weighting | The forward flux of a problem with Σ₁₂ = 0 and νΣ_f2 = 0 on a non-uniform mesh, whose fast-group operator is self-adjoint | Fast adjoint ∝ fast forward within 1e-9 relative per node | Catches the double volume weighting TM §9.1 warns of |
| Present-library reproduction | The present library file | Bit-identical means, identical NaN positions | Same extraction code on the same statepoints |
| BEAVRS on the present library, code-path exercise | None: no accuracy claim | Converged at production tolerances with the hybrid kernel and with finite difference; zero clamps; run record valid; every flux finite and positive in the domain; residual reported | The present library lacks reflectors and carries 2–6 % tally noise (TM §19.3); a comparison would measure the data, not the code |

## 6. Gate

On one verified snapshot: every preset builds and its tests pass; the local CI passes; every
§5 case passes or, for the published IAEA comparison, has its reference and tolerance
recorded before the run; reviewers approve the snapshot (AGENTS.md Review); DBC statuses
flipped, `CODE.md`, `TESTS.md`, `BENCHMARKS.md` and `DEFINITIONS.md` current and the checker
passing; `CURRENT_PROJECT_STATE.md` and Project plan §3 marked; throwaway scripts removed. A
skipped case is reported as skipped, never as passed.

## 7. Workflow per sub-milestone

1. Create the sub-milestone branch `milestone-1.<k>` from `milestone-1`; freeze the contract;
   flip nodes to `partial`; run the usage guard.
2. Workers (`task`; `sonic` for mechanical edits) get their target files, the cited and
   restated equations, the standards, interfaces with units, non-goals, the tests that must
   pass and their node IDs. One owner per file. Workers do not build or test.
3. The orchestrator builds and tests the batch on every preset and runs the local CI.
4. Reviewers (`reviewer`, read-only, same acceptance criteria) review the verified
   snapshot; one blocker ledger per sub-milestone (AGENTS.md Review).
5. DBC update, `CURRENT_PROJECT_STATE.md`, commit; merge `milestone-1.<k>` into
   `milestone-1` with `git merge --no-ff`; stop and report with the convergence state of
   every result. `milestone-1` merges into `main` the same way when the gate of §6 passes.

## 8. Risks

| Risk | Consequence | Mitigation |
|---|---|---|
| Statepoints outside any repository | Library not rebuildable without 72 runs | SHA-256 recorded; risk accepted by the Captain until DM1's Zenodo record (§1.2) |
| Library about 130 MB with standard deviations | Submodule history grows | gzip with shuffle filter in HDF5; the written file is 85 MB |
| ANM overflow or cancellation | NaN or lost digits at large or small node buckling | Scaled hyperbolics and the series of TM §5.5; floating-point traps; unit tests at both ends |
| Eigen's stopping criterion differs from PARCS's | Inner iterations stop at the wrong residual | Tolerance rescaled to the starting residual each call; a test checks the reduction |
| Thread sanitizer with uninstrumented HDF5 | False reports | No threads in this milestone; reports are triaged, never suppressed silently |
| Verification tolerances too tight for 3D runs | Long runs | Only on small problems; IAEA 3D at verification tolerances runs once per gate |

## 9. Changes

| Date | Change |
|---|---|
| 2026-11-17 | First version, with P1, P6 and the three start decisions of §1.1. |
| 2026-11-17 | The Captain named the executable `urus` (Uppsala universitets reaktorsimulator); library `nts_core` and namespace `nts` approved. |
| 2026-11-17 | Approved by the Captain as a working plan; GoogleTest installed. |
| 2026-11-17 | The Captain set the branch workflow: one branch per milestone (`milestone-1`, from `main`) and one per sub-milestone (`milestone-1.<k>`, from `milestone-1`); each merges into its parent when it closes (§7). Sub-milestone 1.1 started; its contract frozen as written. |
| 2026-11-17 | 1.1 review: the local CI order follows its own rule, cheapest step first (the Python checks and the concept-map check now run before the preset builds, clang-tidy last on the database the `dev-clang` step writes); the floating-point trap follows the build type (`NDEBUG`) instead of a separate CMake option. |
| 2026-11-17 | 1.1 closed: both reviews approved after two rounds; nodes `ToolchainPin`, `BuildPresets`, `LocalCi`, `RuntimeChecks` and `Tooling` implemented; merged into `milestone-1`. |
| 2026-11-18 | The Captain chose a public Zenodo record as the statepoints' backup (§1.2); the archive is staged and its upload is hers. |
| 2026-11-18 | Reversed the same day: the present library serves only as a code-path exercise, so the Captain accepted the risk for milestone 1; the statepoints go to Zenodo with the DM1 data (Project plan §5.2). |
| 2026-11-18 | 1.2 started on `milestone-1.2`; contract frozen as `docs/contracts/nodal_library.md`. Details the plan left open: one composition set `beavrs_full_core` (7650 compositions); quantity groups holding `value` and `std_dev`; every quantity has an entry rule (§3.3 of the contract), extending the writer rule above to Σ₁₂ ≤ 0 (an exact zero is an empty tally, TM §19.3), 1/v, β, λ and σ; the writer restores the ±1.8 × 10³⁰⁸ that `openmc.mgxs` substitutes for ±∞; the water density is read from each run's materials, not from its directory name; provenance per entry through a run index. |
| 2026-11-18 | 1.2 DBC: the library's own rules are checked when it is read, by the new node `LibraryReader` (under `NodalLibrary`); `InputValidation` keeps only the check of the compositions a problem uses and stays planned until 1.3. The plan's 1.2 DBC line named `InputValidation`; corrected. |
| 2026-11-18 | 1.2 contract wording after the freeze, behaviour unchanged: items 3–5 of §4 are checked only for format version 1 with 2 energy and 8 delayed groups; in §5 the entry part is left out with its separator where none applies; at most 20 diagnostics per dataset (the check of used compositions included), then the number not listed. |
| 2026-11-18 | 1.2 results: the writer (run with 16 parallel jobs at the Captain's request) reproduced all 13 raw datasets of the old library bit for bit (7 435 800 values, NaN at the same entries) and replaced it (85 MB). Invalid entries per quantity: D₁ 46, D₂ 79, Σ_a1 41, Σ_a2 77, effective Σ₁₂ 222, 1/v₁ 41, 1/v₂ 77, others 0; 227 invalid entries, all in corner columns. The acceptance's "same counts as TM §19.3" holds for every quantity TM §19.3 counts; the effective Σ₁₂ has 222 against the raw 220 because Eq. (3.5) makes two corner entries slightly negative (−3.4 × 10⁻⁴ ± 9.1 × 10⁻³ and −4.0 × 10⁻⁴ ± 1.4 × 10⁻² cm⁻¹, within one σ of zero); no rule changed. |
| 2026-11-19 | 1.2 review: the writer approved in round 1 after four fixes (water density units, −∞ in the rule tests, quotes, the DBC line above); the reader and its tests needed changes in round 1 (no overflow in the delayed-fraction sum, the cap per dataset, float64 checks, an HDF5 failure on one dataset reported there, entry rules beside structural problems, space-padded strings; tests from the contract rather than the code, every §4 rule with its location and message, all four axes). Round 2, reviewers run one at a time: the reader needed entry rules beside validity flags other than 0 and 1; the tests gained a flag named in the fuel, a uint16 flag case, the broken rule asserted in every sweep case and the sets left unchecked after a bad version; the writer's tests gained +∞ through the delayed-fraction and decay-constant rules; all three approved in round 2. The C++ reader and tests were written by the orchestrator after the delegated worker failed. 1.2 closed: nodes `LibraryContract`, `FullCoreReference` and `LibraryReader` implemented, and with them `NodalLibrary`; merged into `milestone-1`. |
| 2026-11-19 | 1.3 started on `milestone-1.3`; contract frozen as `docs/contracts/problem_input.md`. Details the plan left open: every input composition names its composition set as well as its composition; the map may be any HDF5 integer type (the writer uses int32); unknown members are rejected so that a misspelled option cannot fall back to its default; scalars or `[z][y][x]` arrays for each of the four node-state fields, values at excluded positions unread; `exterior` required only when the map excludes a position; solver-option names and defaults as contract §7, with N_upd = 5 and an outer limit of 1000 [OURS] (PARCS gives neither), N_in = 1 (PARCS §10.1.7), ω_D = 1 (TM §6.2); λ_k interpolated like β_k (TM §7.2 lists β_k only; λ_k is the same in every fuel node, TM §7.5); a one-point axis clamps every other value. New DBC node `InputWriter` under `ProblemInput` for the Python writer. |
