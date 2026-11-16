# Agent Context

The user is the Captain: address her as Captain and refer to her with she/her. She sometimes calls the agent Deputy.

Environment: @LOCAL_ENVIRONMENT.md

What we build: `Planning/VISION.md`. The first milestone creates `Planning/CODING_STANDARDS.md`, `Planning/CURRENT_PROJECT_STATE.md`, `Planning/MILESTONE_<n>_PLAN.md`, the CMake presets and the local CI script; rules naming a file apply once it exists. Toolchain pin: `python3 -m tools.toolchain.check_toolchain`.

## Ground rules

- A method that departs from PARCS or SIMULATE-5 needs a written rationale and the Captain's approval.
- Report no result without its convergence state (iterations, residuals, tolerances); every comparison names its reference and a justified tolerance.
- `Resources/` is copyrighted and gitignored: never commit or copy it; cite document, section and equation or table.
- `external/BEAVRS_minimal` changes only through commits in its own repository, then a pointer update here. OpenMC runs only when the Captain asks or the plan schedules one, never in a gate.

## Workflow

Models: the session's choice, else `.omp/config.yml`. Disclose substitutions.

- **Orchestrator** (main session) owns scope, plan, contracts, integration, verification and `Planning/`; it understands what it integrates.
- **Workers** (`task`; `sonic` for mechanical edits) get target files, the reference equations (cited and restated), standards, interfaces with units, non-goals, the tests that must pass and their DBC node IDs; they return concept impact. One owner per file; the top-level plan is never delegated.
- **Reviewers** (`reviewer`) are read-only, in their own context. Nobody approves their own patch. Workers and reviewers do not build or test; the orchestrator verifies each batch. Docs-only or mechanical changes skip review.
- A session delivers one sub-milestone, then stops and reports. It may run unattended: never stop early while work remains.

**Usage limits.** At each batch boundary and on any usage-limit failure, run `python3 -m tools.agent.usage_guard --status --provider anthropic --threshold 0.95` (reads omp's cache; never call a usage API, never add a check that does). Exit 0: continue. Exit 3: if every blocked window resets within 5 hours, run the same with `--wait` and bash `timeout: 18000`, then continue; otherwise report it and continue until the allowance runs out. Exit 1: report; no API fallback.

**Review.**
1. Freeze scope and acceptance criteria; all reviewers get the same ones.
2. Review one verified snapshot: affected presets build; affected tests and verification cases pass; format, lint and tidy clean; DBC updated and its checker passing. Any later code change voids the affected approvals.
3. Keep one blocker ledger: id, violated rule, evidence, correction, regression test, status; mark each new, recurring or scope expansion (the Captain's call).
4. Check findings against source and references; reject unsupported ones with evidence.
5. Fix the invariant, not the input. Sweep the class: both energy groups and all eight delayed groups; non-BEAVRS core layouts; all faces and boundary conditions; interior, reflector, edge and corner nodes; rodded states; steady and transient; first and later iterations; table bounds; zero/full power and flow; units; NaN/Inf; balances; symmetry; refinement. Add deterministic regressions; never weaken an assertion or loosen a tolerance.
6. A defect that survives its fix: re-derive the state model and respecify at invariant level.
7. Re-review: new blockers must cite an original-scope rule and a concrete failure.

**Deliver** when scope is complete, reviewers approve the final snapshot, gates are recorded, scaffolds removed and `Planning/` updated. Skips are not passes; one preset does not certify another; a gate run on changing code proves nothing.

## DBC (Driven by Concepts)

`Planning/DBC/` is the Captain's main tool for leading the project: she reasons and decides in its concepts and terms. Keep it current without exception.

- `CONCEPTS.md`: mermaid flowcharts, L1 the whole project, one L2 per L1 node, deeper only per the depth discipline (a structure node with at least three pointable parts, or a rule with steps); each diagram has a node table (ID, meaning, terms, status) and owns 3–12 nodes. No code, file names or plan names. The Captain's edits are directives: turn them into a plan or contract change; never revert them.
- `CODE.md` maps every built concept to files and symbols, never line numbers. `DEFINITIONS.md` is the glossary. `TESTS.md` lists every test with the nodes it guards. `BENCHMARKS.md` lists every verification and validation case family with its nodes and `Comments`, `Reference` and `Tolerance` lines. Format: the docstring of `Planning/DBC/check_dbc.py`; no meta-documents.
- Statuses track the plan: `implemented` built, `partial` in the current sub-milestone, `planned` not started. When a sub-milestone's contract freezes, mark the nodes it builds or changes `partial` in the same commit; when it closes, flip them to `implemented`, or back to `planned` if deferred. The orchestrator owns all DBC files. After every batch: add or split nodes for concepts the batch made real, fix `CODE.md` rows, give every new test and benchmark entry its node IDs, run `python3 Planning/DBC/check_dbc.py` and fix the DBC (never the checker or its rules) until it passes. Rules that look wrong go to the Captain.
- A commit that introduces, renames, redefines or retires a term updates `DEFINITIONS.md`; one that adds, changes or drops a test or benchmark updates `TESTS.md` or `BENCHMARKS.md`; one that changes what a node means or where it lives updates `CONCEPTS.md` or `CODE.md`; all in the same commit. No term reaches the Captain in a message, plan or receipt before it is defined. Node IDs are stable; renaming one is a change of meaning. Changed meanings are rewritten in place with the date and the old meaning; open questions are marked **Open:** in the definition's row.
- Workers never edit DBC files; they report concept impact (nodes built, code that fits no node, new terms). Exception: any agent that finds a problem with a benchmark entry adds a dated, signed note to its Comments. Decisions go to the Captain in DBC terms. Read DBC files by section.

## License

`GPL-3.0-or-later` (`LICENSE`). Every code file of ours starts, after any shebang, with:

```text
SPDX-License-Identifier: GPL-3.0-or-later
Copyright (C) <year the file was created> Aurora Jahan
Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.
```

Add it to files you touch that lack it; never change years. Not in `external/`, `Resources/`, `build/`, data files or JSON. Dependencies must be GPL-3.0-compatible.

## Tracking

Only the orchestrator edits `Planning/`. Finished work is marked in its plan, recorded in `CURRENT_PROJECT_STATE.md` and reflected in the DBC in the same commit. Commit at meaningful progress; push once a remote exists. Ask before including the Captain's own uncommitted changes.
