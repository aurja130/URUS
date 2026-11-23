# Current Project State

Where the project stands: milestones and sub-milestones, every prerequisite of
[the project plan](PROJECT_PLAN.md) §5 with its state, and the last gate results. Updated in
the commit that changes any of them. Terms: [DEFINITIONS](DBC/DEFINITIONS.md).

## 1. Milestones

| Milestone | Branch | State |
|---|---|---|
| 1 Core engine | `milestone-1` | In progress since 2026-11-17 ([plan](MILESTONE_1/MILESTONE_1_PLAN.md), approved 2026-11-17) |
| DM1 Single-assembly library | — | In progress since 2026-11-20: the sizing campaign (scope 1; 653 runs on ENDF/B-VII.1, one with photon transport for the direct heating fraction) ran on adama until 2026-11-23 10:00 and finished 186 runs; their analysis (`dm1_sizing/SIZING_ANALYSIS.md`) awaits the Captain's decisions on the grid, the statistics target, the compute and the relaunch. The run list now has 698 runs (moderator scan plus 550 and 650 K, density scan plus 0.35 g/cm³) in relaunch priority order. The development library (17,280 runs at 1e6 active histories with the library tallies, coarse tier of 4,800 first) is set up for the 192-core server, manual in `dm1_sizing/README.md`; not yet started. Work in `external/BEAVRS_minimal`, directory `dm1_sizing/` |
| DM2 Full-core references | — | Not started |
| 2 Coupled steady state | — | Not started |
| 3 Kinetics | — | Not started |
| 4 Primary loop | — | Not started |
| 5 Fidelity | — | Not started |
| 6 Polishing | — | Not started |

### 1.1 Milestone 1 sub-milestones

| Sub-milestone | Branch | State |
|---|---|---|
| 1.1 Foundations | `milestone-1.1` | Done 2026-11-17; merged into `milestone-1` |
| 1.2 Library contract and the present library | `milestone-1.2` | Done 2026-11-19; merged into `milestone-1` ([nodal library contract](../docs/contracts/nodal_library.md), format version 1) |
| 1.3 Problem input and cross-section model | `milestone-1.3` | Done 2026-11-19; merged into `milestone-1` ([problem input contract](../docs/contracts/problem_input.md), format version 1) |
| 1.3a Input documents | `milestone-1.3a` | Done 2026-11-20; merged into `milestone-1` ([problem input contract](../docs/contracts/problem_input.md), format version 2) |
| 1.4 CMFD with zero coupling corrections, eigenvalue, output | — | Not started |
| 1.5 Nodal kernels and the interface solve | — | Not started |
| 1.6 Transverse leakage and the three-dimensional nodal solution | — | Not started |
| 1.7 Adjoint, BEAVRS on the present library, milestone gate | — | Not started |

## 2. Prerequisites

States: open, done (with date), waived (by the Captain, with date).

| Milestone | Category | Item | When | State |
|---|---|---|---|---|
| 1 | Decide | P6: the C++ standard | Start | Done 2026-11-17: C++23 |
| 1 | Decide | P1: data work as separate data milestones | Start | Done 2026-11-17: yes |
| 1 | Obtain | GoogleTest installed (`gtest-devel`) | Scope 1 | Done 2026-11-17: `gtest-devel-1.17.0-2.fc44` |
| 1 | Obtain | Statepoint archive published on Zenodo | Before 1.2 | Waived 2026-11-18 by the Captain: risk accepted; moved to DM1 close |
| 1 | Obtain | IAEA 2D and 3D PWR specifications, reference eigenvalues and assembly powers | Scope 5 | Open |
| DM1 | Decide | P2: reflector data, discontinuity factors, form functions and rodded data generated now | Start | Decided 2026-11-19: yes |
| DM1 | Decide | O5: effective or physical delayed-neutron fractions | Start | Decided 2026-11-19: effective fractions tallied into the library |
| DM1 | Run | Machine time for the sizing campaign | Scope 1 | Done 2026-11-20: 64 cores until 2026-11-23 10:00, used on adama (186 of 653 runs); a relaunch of 107 priority runs (about 37 h on adama) awaits the Captain |
| DM1 | Obtain | Decay-heat curve and its group fits | Scope 2 | Open |
| DM1 | Approve data | Decay-heat group constants (L8) | Scope 2 | Open |
| DM1 | Run | Run list | Scope 3 | Open |
| DM1 | Run | Machine time for the pilot | Scope 3 | Open |
| DM1 | Run | Machine time for the development library | Scope 3 | Requested 2026-11-23: a 192-core server (no root rights); runs set up, start planned 2026-11-24 |
| DM1 | Run | Machine time for the production runs and reflector models | Scope 4 | Open |
| DM1 | Obtain | Zenodo record of the DM1 statepoints and the present statepoints, DOI recorded | Close | Open |
| DM2 | Run | Run list | Start | Open |
| DM2 | Run | Machine time for the runs | Start | Open |
| 2 | Decide | P3: grid and nozzle loss coefficients now | Start | Open |
| 2 | Decide | P4: rod banks and rodded mixing in milestone 2 | Start | Open |
| 2 | Decide | P5: validation limited to beginning of cycle | Start | Open |
| 2 | Decide | P8: recording the run time at every gate | Start | Open |
| 2 | Obtain | IAPWS-IF97 release with its verification tables | Scope 1 | Open |
| 2 | Obtain | Source for spacer grid and nozzle loss coefficients | Scope 2 | Open |
| 2 | Approve data | Grid and nozzle loss coefficients | Scope 2 | Open |
| 2 | Obtain | MATPRO Zircaloy conductivity | Scope 3 | Open |
| 3 | Decide | P7: six-group benchmarks as eight-group data | Start | Open |
| 3 | Obtain | LMW and NEACRP specifications and reference solutions | Close | Open |
| 3 | Obtain | Published PARCS or SIMULATE transient results for BEAVRS (optional) | Close | Open |
| 4 | Obtain | Sources for a generic four-loop PWR | Start | Open |
| 4 | Approve data | Generic four-loop data set | Scope 1 | Open |
| 5 | Decide | O13: critical heat flux and drift-flux correlations | Scope 4 | Open |
| 5 | Obtain | Documents of the chosen correlations | Scope 4 | Open |
| 5 | Obtain | Published PARCS or SIMULATE steady-state results for BEAVRS (optional) | Close | Open |
| 6 | Decide | P8: the run-time target | Start | Open |
| 6 | Decide | Worked scenarios, documentation, Python versions, distribution | Start | Open |

## 3. Last gate results

1.3a close, 2026-11-20, on the final snapshot: the engine reader approved by its reviewer with
two test strengthenings it proposed and checked, applied as proposed; the writer approved; only
`Planning/` documents changed after the run. `python3 -m tools.ci.local_ci` passed all 14 steps
in 322.6 s.

| Step | Result |
|---|---|
| toolchain | Every pinned tool matches `manifests/toolchain.txt` |
| license-headers | Every code file of ours has its header |
| clang-format | No change needed |
| ruff check, ruff format | Clean |
| basedpyright | 0 errors |
| pytest | 194 passed |
| dbc | `check_dbc.py` ok |
| preset `dev-gcc` | 1123 of 1123 tests passed |
| preset `dev-clang` | 1123 of 1123 tests passed |
| preset `asan-ubsan-clang` | 1123 of 1123 tests passed |
| preset `tsan-gcc` | 1123 of 1123 tests passed |
| preset `release-gcc` | 1123 of 1123 tests passed |
| clang-tidy | No finding on every engine source in the `dev-clang` database |

Cross-section table interpolation benchmark (BENCHMARKS), in every preset, its problem now
allowing table clamps in its settings document: all 72 table points and 200 seeded interior
states within 1e-14 relative; clamp counts and sides exact; 16 of 16 pass. The model has no
iteration, so there is no convergence state to report.
Submodule `external/BEAVRS_minimal` at `f041ee7` since 2026-11-20 (was `4bf2d26`, then
`aedc5a8`): adds the DM1 sizing campaign (`dm1_sizing/`) on ENDF/B-VII.1 with one
photon-transport run, reviewed and approved; local end-to-end test with OpenMC 0.15.3 (setup,
data subset of 119 files verified, smoke runs of two replicas and the photon run, copy-back with
removal at `aedc5a8`). Smoke runs have 2 active batches of 2000 histories and are not converged;
they test the chain only. Caveats for the analysis: fuel temperatures between the six neutron
data temperatures, and moderator temperatures between the nine thermal-scattering temperatures
(294–800 K in 50 K steps up to 650 K), are OpenMC's stochastic interpolation, so the scans there
show the data grid, not physics; the moderator-temperature spacing found holds for ENDF/B-VII.1
with interpolation only. The photon cost factor (2.5) was measured with JEFF-4.0; the photon
run's wall time re-measures it.

Sizing campaign result (2026-11-23): 186 of 653 runs done on adama (4 runners × 16 threads),
none failed; every run 2e7 active histories after 40 inactive batches, the long run 1e8. The
analysis module `dm1_sizing/analyze.py` regenerates every number; findings and the decisions they
ask for in `dm1_sizing/SIZING_ANALYSIS.md`. Convergence: the two-group balance reproduces the
Monte Carlo k in 185 runs (rms 0.96 σ, max 2.69 σ); σ·√n constant within 8 % over 100–1000
active batches; no Shannon entropy tallied. Grid: moderator nodes on the thermal-scattering
data temperatures (550 K required); proposed 4 × 6 × 8 × 6 = 1,152 states per composition,
worst interval errors 35–62 pcm per axis at nominal curvature (157 pcm for rodded ρ at
0.3–0.4 g/cm³ with the unrodded off-nominal factor 2.5); the ρ node 0.8 keeps the isothermal
temperature coefficient's grid bias near +0.1 pcm/°F.
Open for the Captain: the delayed-group count (the library has 6, the engine 8), the grid
accuracy criterion (per axis at nominal curvature proposed), the statistics target (1e7
proposed), production compute (the proposed tensor takes 121 days on adama at 1e7, 41 without
scoring cost), the O5 route (fitted delayed-fraction ratio proposed), η_c (0.028 or 0.038,
O10), ω_D (Rowlands 4/9 until O9 profile runs), windowed multipole, and the relaunch (107
priority runs in the reordered list).

Development library set up (2026-11-23, submodule `dm1_sizing/`): campaign `devlib`, 17,280
runs (15 compositions × 1,152 states, the 4,800 of the coarse tier first), 25,000 histories ×
(40 inactive + 40 active) batches, IFP on, with the library tallies. Local checks with OpenMC
0.15.4-dev94 and ENDF/B-VII.1 (123 files, Xe-135 and Sm-149 added from the official archive):
one full-statistics run (A16, 294 K, 0.3 g/cm³, 0 ppm) took 2.2 min on 20 threads, k_inf 1.08818
± 0.00095 after 40 inactive batches, entropy 8.031 → 8.032 over the active batches; thermal
microscopic cross sections Xe-135 absorption 1.79e6 b, Sm-149 absorption 4.69e4 b, U-235
detector fission 353 b; `(n,fission)` and `fission` agree for U-235 in the fuel within 4e-5
relative (OpenMC skips a `fission` score where the material does not fission, so the detector
uses `(n,fission)`). The library tallies cost about 15 % of the active rate (two states, 20
batches each). Estimate [inference]: 6–7 min per run on an adama runner; on 12 runners × 16
threads, 1–2 days for the coarse tier and 4–7 days for all runs.
