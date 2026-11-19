# Coding Standards

The rules every change to the engine and its tooling follows. Written 2026-11-17 in
sub-milestone 1.1 from [the milestone 1 plan](MILESTONE_1_PLAN.md) §2, which the Captain
approved; a change to a rule here is a plan change (milestone plan §9). Terms are defined in
[DEFINITIONS](DBC/DEFINITIONS.md).

## 1. Language and build

- C++23, compiler extensions off (`CMAKE_CXX_EXTENSIONS OFF`). Pinned compilers: GCC 16 and
  Clang 22 (`manifests/toolchain.txt`); a student build needs GCC ≥ 14 or Clang ≥ 18.
- CMake ≥ 3.28 with presets, Ninja generator. Binary directories `build/<preset>`.
- Dependencies, all GPL-3.0-compatible and found through CMake imported targets, so that
  their headers are system headers and warnings-as-errors applies to our code only:
  HDF5 C++ API 1.14.6 (BSD-style), Eigen 5.0.1 (MPL-2.0), GoogleTest 1.17 (BSD-3-Clause).
  A dependency is added in the sub-milestone that first uses it.
- Warnings: `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror` on every target of ours
  (interface target `nts_warnings`). Never `-ffast-math` or any flag that relaxes IEEE 754.
- `std::print` formats output; no `fmt` dependency.

### 1.1 Build presets

| Preset | Compiler | Build | Floating-point traps | Sanitizers |
|---|---|---|---|---|
| `dev-gcc` | GCC | Debug | on | none |
| `dev-clang` | Clang | Debug | on | none; writes `build/dev-clang/compile_commands.json` for clangd and clang-tidy |
| `asan-ubsan-clang` | Clang | Debug | on | address and undefined behaviour, no recovery |
| `tsan-gcc` | GCC | Debug | on | thread (never combined with address) |
| `release-gcc` | GCC | Release (`-O3`) | off | none; invariant checks stay on |

`cmake --preset P`, `cmake --build --preset P`, `ctest --preset P`. One preset does not
certify another.

## 2. Names and layout

- Library target `nts_core`, namespace `nts`; executable `urus`.
- Types `PascalCase`; functions, variables, parameters and namespaces `snake_case`; private
  and protected members end in `_`; macros `UPPER_CASE` with the prefix `NTS_`.
- Physics symbols follow TM §2. Every variable with a unit names it where its type does not
  (`width_cm`, `power_w`).
- Directories: `src/nts/<area>/` (`common`, `io`, `xs`, `cmfd`, `nodal`, `solve`), included
  as `"nts/<area>/<file>.hpp"`; `app/` (the `urus` main); `tests/unit/`,
  `tests/verification/`, `tests/end_to_end/`; `tools/inputs/` (Python problem-input writer);
  `tools/references/` (reference scripts); `tools/ci/` (local CI); `docs/contracts/`
  (problem input, nodal library, output). C++ headers `.hpp`, sources `.cpp`, tests
  `*_test.cpp`.

## 3. Units and ordering

- Inside the engine: cm, s, K, kg m⁻³, ppm, W, J. Conversions happen only when input files
  are read; each contract names one accepted unit per quantity and rejects the rest.
- Node ordering: x fastest, then y, then z. Energy group 0 is fast, 1 is thermal; delayed
  groups are 0–7. The counts two and eight are fixed; any other count is rejected on input.
- `double` throughout.

## 4. Errors and invariants

- **Input and data errors** (anything a file can get wrong) are collected as diagnostics in
  `std::expected` and reported together, each naming the file, dataset and entry. HDF5
  exceptions are caught at the I/O boundary. Input errors are never asserted.
- **Invariant check** `NTS_ASSERT(condition, message)` (`nts/common/invariant.hpp`) guards
  conditions the code itself guarantees. It is on in every build including release,
  evaluates the condition once and the message only on failure, and on failure writes one
  line with expression, message, file, line and function to stderr and aborts.
- **Floating-point traps.** Every build without `NDEBUG` (Debug: the four development and
  sanitizer presets) traps invalid operations, division by zero and overflow, so a NaN or an
  infinity stops the run with SIGFPE where it is made; release builds do not trap. The switch
  follows the build type and has no separate option. Every `main` of ours calls
  `nts::configure_floating_point_environment()` first. Underflow and inexact are not trapped.
  The program tells the sanitizer runtimes not to catch SIGFPE, so a trap stops it under a
  sanitizer too. Invariant checks and floating-point traps together are the runtime checks.
- No raw `new`/`delete`; no mutable globals.

## 5. Equations and departures

- Every function that implements a theory-manual equation cites it in a comment, e.g.
  `// TM Eq. (5.23)`.
- A method that departs from PARCS or SIMULATE-5 needs a written rationale and the Captain's
  approval before any code.

## 6. Formatting and static analysis

- `clang-format` (`.clang-format`): LLVM base, 4-space indent, 100 columns (as ruff),
  pointer to the left, own headers first, then third-party, then the standard library.
- `clang-tidy` (`.clang-tidy`): families bugprone, performance, modernize, readability, misc,
  clang-analyzer; every warning is an error. Each excluded check and each check option that
  narrows a check carries its reason in the file. A `NOLINT` names its check and its reason.
- Python tooling: ruff (line length 100, rules in `pyproject.toml`), `ruff format`,
  basedpyright strict, pytest.

## 7. Tests

- C++ tests use GoogleTest; CTest discovers each test. Value-parameterized tests carry the
  sweeps of AGENTS.md Review rule 5 (both energy groups, all eight delayed groups, every face
  and boundary kind, …).
- A test that an invariant check or a floating-point trap stops the program is a death test.
- Unit tests build their HDF5 fixtures in C++ and need no Python. End-to-end tests run
  `urus` on inputs written by `tools/inputs/`. Benchmark references come from committed
  reference scripts that share no code with the engine.
- A test checks a consumer-visible behaviour; tolerances are justified in the milestone plan
  and never loosened to pass.

## 8. Local CI

`python3 -m tools.ci.local_ci` runs every gate, cheapest first, and stops at the first
failure: toolchain check, license headers, `clang-format --dry-run -Werror`, `ruff check`,
`ruff format --check`, basedpyright, pytest, the concept-map check, each preset (configure,
build, test), and last clang-tidy on the `dev-clang` database (which the `dev-clang` step
writes). clang-tidy fails when the database lacks any engine source of ours; a check given no
file fails. `--list` names the steps; `--only STEP` runs a subset; when it leaves a step out it
prints `SKIP` for it and does not end with "local CI passed". A skipped step is never reported
as passed.

## 9. License

Every code file of ours (`.cpp`, `.hpp`, `.h`, `.py`, `.cmake`, `CMakeLists.txt`) starts,
after any shebang, with the three-line license header of AGENTS.md; the year is the year the
file was created and is never changed. Not in `external/`, `Resources/`, `build/`, data files,
JSON or YAML configuration. `Planning/DBC/check_dbc.py` is the DBC skill's checker, kept
verbatim, and is exempt.

## 10. Branches and commits

- One branch per milestone (`milestone-<n>`, from `main`) and one per sub-milestone
  (`milestone-<n>.<k>`, from the milestone branch). A sub-milestone merges into its milestone
  branch when it closes, and a milestone into `main` when its gate passes, both with
  `git merge --no-ff`.
- Commit at meaningful progress; a commit that changes a term, test, benchmark or concept
  updates the DBC in the same commit. Push to `origin` after each commit.
