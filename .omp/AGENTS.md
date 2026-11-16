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

`Planning/DBC/` is the Captain's main tool for leading the project; keep it current without exception. Not set up yet: run the `SetupDBC` skill before the first sub-milestone is delivered (evidence file expected: `BENCHMARKS.md`), then trim this note.

- `CONCEPTS.md`: mermaid diagrams (L1 whole project, L2 per L1 node, deeper only per the depth discipline), each with a node table and 3–12 nodes; no code or file names. The Captain's edits are directives; never revert them. `CODE.md` maps concepts to files and symbols; `DEFINITIONS.md` is the glossary; `TESTS.md` and `BENCHMARKS.md` map tests and verification/validation cases to nodes. Format: `check_dbc.py`'s docstring; no meta-documents.
- Statuses: `implemented` built, `partial` current sub-milestone (set when its contract freezes), `planned` not started. The orchestrator owns the DBC; after each batch update nodes, `CODE.md`, test and benchmark rows, then run `python3 Planning/DBC/check_dbc.py` and fix the DBC (never the checker) until it passes. Rules that look wrong go to the Captain.
- A commit that changes a term, test, benchmark or node updates its DBC file in the same commit. No term reaches the Captain before it is defined. Node IDs are stable; changed meanings are rewritten in place with date and old meaning; open questions are marked **Open:**.
- Workers report concept impact and never edit the DBC, except a dated, signed note on a faulty benchmark entry. Decisions go to the Captain in DBC terms.

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
