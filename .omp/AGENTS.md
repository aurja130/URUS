# Agent Context

Development environment (tools, paths, versions):

@LOCAL_ENVIRONMENT.md

Scope, physics and stages: `Planning/VISION.md`. The first milestone creates `Planning/CODING_STANDARDS.md`, `Planning/CURRENT_PROJECT_STATE.md`, `Planning/MILESTONE_<n>_PLAN.md`, the CMake presets and the local CI script; until a file exists, rules that name it do not apply, and no message or plan cites it as if it did. Toolchain pin: `python3 -m tools.toolchain.check_toolchain` against `manifests/toolchain.txt`.

## The simulator

A coupled 3D nodal neutronics and thermal-hydraulics simulator of a PWR core and primary loop, developed and validated on the MIT BEAVRS benchmark. A C++ engine run from the command line; HDF5 is its only input and output.

- **Authorities**, in this order: the Captain's decisions recorded in the milestone plans; `Planning/VISION.md`; the methods of the reference codes as documented in `Resources/` (PARCS theory manual, SIMULATE-5 paper) and the textbooks there for what they leave open; `Planning/CODING_STANDARDS.md`. The physics must be equivalent to PARCS and SIMULATE-5. A method that departs from them needs a written rationale and the Captain's approval; the primary loop is the one planned extension beyond them.
- **Generality.** BEAVRS is the reference case, never a built-in assumption. Core size and layout, pitch, axial zoning, number of energy and delayed-neutron groups, branch axes and loop parameters come from input. A BEAVRS constant in the engine (2 groups, 8 delayed groups, a 15×15×17 mesh, a pitch) is a defect.
- **File contract.** Input and output HDF5 layouts are documented, and the engine reads any input and nodal library that follows them. A layout change updates the documentation and its tests in the same commit. The future Python package depends only on this contract; nothing in the engine is added for it.
- **Student controls.** Exactly four knobs: rod position, boron concentration, primary flow, steam-generator heat removal. Each is a constant or a time schedule in the input. Everything else is input, not a control.
- **Numerical evidence.** No result is reported without its convergence state (iterations, residuals, tolerances). Every comparison names its reference (analytic or manufactured solution, a published result with document and table, or a BEAVRS measurement) and a tolerance with its justification. Results are not bit-exact across compilers or presets; regression baselines compare within stated tolerances.
- **References.** `Resources/` is local, gitignored and copyrighted: never commit it or copy its text into the repository. Cite it as document, section and equation or table number in code comments, plans and reviews. Read the PDFs by section.

### Nodal data and external code

- Nodal cross sections and kinetics data come from OpenMC. `external/BEAVRS_minimal` is a git submodule (the Captain's Codeberg repository): the BEAVRS model builder, the branch-sweep scripts and the current library `beavrs_kinetics_library.hdf5`. Change it only by commits in the submodule's own repository followed by a submodule pointer update here; push the submodule only when the Captain asks.
- OpenMC runs (`sweep.py`, `run_model.py`) are long Monte Carlo jobs. Start one only when the Captain asks or the plan schedules it; never as part of a test or review gate.
- Engine and tests read the nodal library through the documented layout only, never through the generating scripts.

## Multi-model workflow

Models come from the user's session choice, otherwise `.omp/config.yml` (`task.agentModelOverrides`: `task` → `@task`, `sonic` → `@smol`, `scout` → `@smol`, `reviewer` → `@slow`). Disclose any substitution; never claim a model use that was not observed.

- **Orchestrator** (main session): owns scope, plan, contracts, integration, verification and `Planning/` updates. It understands the code it integrates; it does not relay patches.
- **Coding workers**: `task` for bounded slices, `sonic` for mechanical edits. Each gets the target files, the reference equations of the method it implements (document, section, equation numbers, with the equations restated in the brief), the relevant `CODING_STANDARDS.md` rules, the interfaces and units, non-goals, the tests and verification cases that must pass, and the DBC node IDs of its slice. It returns its concept impact (see **DBC** below). One owner per file; parallel only for independent slices; the top-level plan is never delegated.
- **Reviewers**: `reviewer`, in a context of its own, read-only, judging against the authorities above. A worker never approves its own patch.
- Workers and reviewers do not build or test. The orchestrator verifies each integrated batch; review approval is not runtime proof. Mechanical or documentation-only changes need no review panel.

### Sessions and usage limits

A session completes one sub-milestone (e.g. M2.1) and nothing more: it delivers it as defined under **Deliver** below, then stops and reports. It may run unattended (overnight), so it never stops early to wait for the user while reachable work remains.

At each batch boundary (before delegating a batch, before a review round, before a long build or CI run), and whenever a model call or agent fails on a usage limit, the orchestrator runs `python3 -m tools.agent.usage_guard --status --provider anthropic --threshold 0.95`. It reads the usage omp has already cached (`~/.omp/agent/agent.db`) and never calls a usage API itself: Anthropic ends sessions that poll its API too often, so never add a check that does. Exit 0: work continues. Exit 3: a window has less than 5 % left. The orchestrator waits at most 5 hours, whichever window it is: if every blocked window's reset time (printed by `--status`) is within 5 hours (the 5-hour window always is; the 7-day window only in its last 5 hours), it runs `python3 -m tools.agent.usage_guard --wait --provider anthropic --threshold 0.95` with bash `timeout: 18000`, which sleeps until the windows clear, and work continues; otherwise it does not wait: it reports the low window and work continues until the allowance is used up. Exit 1: omp's cache could not be read; report it and do not substitute an API call.

### Review cycle

1. Freeze the scope and acceptance criteria; every reviewer gets the same ones.
2. Review one verified snapshot: compiled in the affected presets, affected C++ and pytest tests passing, the verification cases covering the change passing (analytic and manufactured solutions, conservation and symmetry checks, reference comparisons), format, lint and tidy clean, the DBC updated for the change and its checker passing. A code change after approval re-verifies and voids the affected approvals.
3. Merge all findings into one blocker ledger: id, violated rule, evidence or counterexample, correction, regression test, status. Mark each as new defect, recurring defect or scope expansion.
4. Check each finding against the source and the reference equations; reviewers can be wrong. Reject unsupported findings with evidence. Scope expansion is the Captain's decision.
5. Fix the invariant, not the reported input. Check the whole class: every energy and delayed-neutron group, and group counts other than BEAVRS's; all six node faces and every boundary condition; interior, reflector, edge and corner nodes; unrodded, partially and fully rodded nodes; steady-state and transient paths; first and later outer, Picard and time-step iterations (state carried over); branch-table bounds and out-of-range interpolation; zero and full power, zero and full flow; units; NaN and Inf; neutron, energy and mass balance; the symmetry a symmetric core must keep; convergence under mesh, time-step and tolerance refinement. Add deterministic regressions for the failure and its symmetric and boundary cases; never weaken an assertion or loosen a tolerance to pass.
6. If a defect survives its fix, stop patching locally: re-derive the state model and give the worker an invariant-level specification.
7. Re-review: reviewers recheck their blockers and the original criteria. A new blocker must cite an original-scope rule and a concrete failure; a preferred design is not one.

**Deliver** when the scope is complete, the required reviewers approve the final snapshot, the gates run are recorded, scaffolds are removed and `Planning/` is updated. Skips are not passes (tests that need local data such as `Resources/` or an OpenMC library skip without it); one preset does not certify the others; a gate run while code changed proves nothing about the final code.

## DBC (Driven by Concepts)

`Planning/DBC/` is the Captain's (the user's) main tool for leading the project: she reasons and decides in its concepts and terms. Keep it current without exception.

It is not set up yet. Set it up with the `SetupDBC` skill before the first implementation sub-milestone is delivered; the setup fixes the evidence file (expected: `BENCHMARKS.md`, for verification and validation cases) and adjusts this section. Until then the rules below that name DBC files do not apply.

- `CONCEPTS.md` draws the project as mermaid flowcharts: L1 the whole project, one L2 diagram per L1 node, deeper levels only where the depth discipline allows (a structure node with at least three pointable parts, or a rule with steps). Each diagram has a node table (ID, meaning, terms, status) and owns 3–12 nodes. No code, file names or plan names. The Captain's edits to it are directives: turn them into a plan or contract change; never revert them.
- `CODE.md` maps every built concept to files and symbols, never line numbers. `DEFINITIONS.md` is the glossary. `TESTS.md` lists every test with the nodes it guards. `BENCHMARKS.md` lists every verification and validation case family (analytic and manufactured solutions, reference-code comparisons, BEAVRS measurements, regression baselines) with its concept nodes and a Comments column.
- The format rules are the ones `Planning/DBC/check_dbc.py` enforces; its docstring lists them. The DBC gets no meta-documents of its own.
- Statuses track the plan: `implemented` (green) is built, `partial` (yellow) is in the current submilestone, `planned` (uncolored) is not started. When a submilestone's contract is frozen, mark the nodes it will build or change `partial` in the same commit. The orchestrator owns all DBC files. After every implementation batch: add or split nodes for concepts the batch made real (respecting the depth discipline); fix `CODE.md` rows; give every new test and benchmark entry its node IDs; run `python3 Planning/DBC/check_dbc.py` and fix the DBC files (never the checker's rules) until it passes. When the submilestone closes, flip its `partial` nodes to `implemented`, or back to `planned` if they were deferred, and mark the next submilestone's nodes `partial`. A rule that looks wrong goes to the Captain.
- Any commit that introduces, renames, redefines or retires a term updates `DEFINITIONS.md` in the same commit; any commit that adds, changes or drops a test, benchmark case or baseline updates `TESTS.md` or `BENCHMARKS.md`; any commit that changes what a node means or where it lives updates `CONCEPTS.md` or `CODE.md`. No term reaches the Captain in a message, plan or receipt before it is defined there.
- Node IDs are stable; renaming one is a change of meaning. A changed meaning is rewritten in place with the date and the old meaning. Open questions are marked **Open:** in the definition's row.
- Workers and reviewers do not edit DBC files. Worker briefs name the node IDs of their slice; workers report concept impact (nodes built, code that fits no node, new terms) in their return message. The one exception: any agent that finds a problem with a benchmark entry adds a dated, signed note to its Comments.
- Decisions put to the Captain use DBC terms. Read DBC files by section. A sub-milestone is not delivered (**Deliver** above) until the checker passes on its final snapshot.

## License notices

The project is `GPL-3.0-or-later` (`LICENSE`). Every file of ours that contains code (C/C++, Python, shell, CMake, …) starts with these lines in its comment syntax, after any shebang or encoding line:

```text
SPDX-License-Identifier: GPL-3.0-or-later
Copyright (C) <year the file was created> Aurora Jahan
Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.
```

- Add the notice to any file you touch that lacks it; never rewrite existing years.
- Never add it to `external/`, `Resources/`, `build/`, data files (HDF5, nodal libraries) or formats without comments (JSON).
- Dependencies must be GPL-3.0-compatible.

## Tracking

- Only the orchestrator edits `Planning/`. When a task, sub-milestone or milestone is finished, mark it done in its plan file, record it in `CURRENT_PROJECT_STATE.md`, and bring `Planning/DBC/` in line (statuses, `CODE.md`, `TESTS.md`, `BENCHMARKS.md`, `DEFINITIONS.md`; see **DBC**) in the same commit.
- Commit when meaningful progress is achieved, and push when the repository has a remote (it has none yet). If changes by the user are detected, ask if the user wants them included in the commit.
