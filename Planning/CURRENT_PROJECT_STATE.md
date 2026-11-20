# Current Project State

Where the project stands: milestones and sub-milestones, every prerequisite of
[the project plan](PROJECT_PLAN.md) §5 with its state, and the last gate results. Updated in
the commit that changes any of them. Terms: [DEFINITIONS](DBC/DEFINITIONS.md).

## 1. Milestones

| Milestone | Branch | State |
|---|---|---|
| 1 Core engine | `milestone-1` | In progress since 2026-11-17 ([plan](MILESTONE_1/MILESTONE_1_PLAN.md), approved 2026-11-17) |
| DM1 Single-assembly library | — | Not started; may start: the library contract froze when 1.2 closed (2026-11-19); its start decisions P2 and O5 were made 2026-11-19 |
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
| DM1 | Obtain | Decay-heat curve and its group fits | Scope 2 | Open |
| DM1 | Approve data | Decay-heat group constants (L8) | Scope 2 | Open |
| DM1 | Run | Run list | Scope 3 | Open |
| DM1 | Run | Machine time for the pilot | Scope 3 | Open |
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

1.3 close, 2026-11-19, on the snapshot the reviewers approved (code unchanged since the run;
only `Planning/` documents changed afterwards): `python3 -m tools.ci.local_ci` passed all 14
steps in 258.4 s.

| Step | Result |
|---|---|
| toolchain | Every pinned tool matches `manifests/toolchain.txt` |
| license-headers | Every code file of ours has its header |
| clang-format | No change needed |
| ruff check, ruff format | Clean |
| basedpyright | 0 errors |
| pytest | 156 passed |
| dbc | `check_dbc.py` ok |
| preset `dev-gcc` | 1044 of 1044 tests passed |
| preset `dev-clang` | 1044 of 1044 tests passed |
| preset `asan-ubsan-clang` | 1044 of 1044 tests passed |
| preset `tsan-gcc` | 1044 of 1044 tests passed |
| preset `release-gcc` | 1044 of 1044 tests passed |
| clang-tidy | No finding on every engine source in the `dev-clang` database |

Cross-section table interpolation benchmark (BENCHMARKS), in every preset: all 72 table points
and 200 seeded interior states within 1e-14 relative; clamp counts and sides exact; 16 of 16
pass. The model has no iteration, so there is no convergence state to report.
Submodule `external/BEAVRS_minimal` unchanged at `4bf2d26`.
