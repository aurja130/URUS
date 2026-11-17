# Code

Code links to [CONCEPTS](CONCEPTS.md), [DEFINITIONS](DEFINITIONS.md), [TESTS](TESTS.md) and
[BENCHMARKS](BENCHMARKS.md). Only implemented and partial nodes have rows; every physics node
is still planned and absent here.

| Node | Code | Notes |
| --- | --- | --- |
| `BuildPresets` | `CMakePresets.json` | The five presets `dev-gcc`, `dev-clang`, `asan-ubsan-clang`, `tsan-gcc`, `release-gcc`, each with configure, build and test parts. |
| `BuildPresets` | `CMakeLists.txt` `nts_warnings` `NTS_SANITIZERS` `NTS_FLOATING_POINT_TRAPS` | C++23 without extensions; warnings as errors on our targets only; sanitizer and trap switches the presets set. |
| `BuildPresets` | `src/CMakeLists.txt` `nts_core` | The engine library; publishes the trap switch as a compile definition. |
| `BuildPresets` | `tests/unit/CMakeLists.txt` `nts_unit_tests` `gtest_discover_tests` | The unit-test executable; CTest discovers each test. |
| `LocalCi` | `tools/ci/local_ci.py` `PRESETS` `Step` `select_steps` `run_steps` `tidy_sources` `main` | Run as `python3 -m tools.ci.local_ci`; stops at the first failed step. |
| `LocalCi` | `tools/ci/license_headers.py` `header_problem` `candidate_files` `check_files` | The license-header step. |
| `LocalCi` | `tools/ci/__init__.py` `repository_files` | The files of ours: tracked plus untracked, not ignored. |
| `LocalCi` | `.clang-format` | Formatting rules the clang-format step enforces. |
| `LocalCi` | `.clang-tidy` | Static-analysis checks the clang-tidy step enforces; each exclusion carries its reason. |
| `RuntimeChecks` | `src/nts/common/invariant.hpp` `NTS_ASSERT` `fail_invariant` | The invariant check. |
| `RuntimeChecks` | `src/nts/common/floating_point.hpp` `configure_floating_point_environment` `floating_point_traps_enabled` | The floating-point traps; called first in every main of ours. |
| `RuntimeChecks` | `tests/unit/main.cpp` `main` | The unit tests run with the traps of their build. |
| `BranchGrid` | `external/BEAVRS_minimal/generate_models.py` `T_fuel_grid` `mod_states` `CR_grid` | The present grid: 6 fuel temperatures × 6 moderator states × 2 rod states, no boron axis. Moderator density from IAPWS-IF97 at 15.5 MPa. |
| `FullCoreReference` | `external/BEAVRS_minimal/make_beavrs.py` `BEAVRS` | Builds the full-core BEAVRS OpenMC model at one branch state. |
| `FullCoreReference` | `external/BEAVRS_minimal/run_model.py` `mgxs_lib` `data_out` | Runs one state and turns the node-wise tallies into two-group cross sections and eight-group kinetics data. |
| `FullCoreReference` | `external/BEAVRS_minimal/sweep.py` `parse_state_from_folder` `main` | Collects every state into the present nodal library file, NaN where a tally is empty. |
| `ToolchainPin` | `tools/toolchain/check_toolchain.py` `TOOL_SPECS` `run_checks` `compare_manifest` `render_manifest` | Run as `python3 -m tools.toolchain.check_toolchain`. |
| `ToolchainPin` | `manifests/toolchain.txt` | The pinned versions. |
| `UsageGuard` | `tools/agent/usage_guard.py` `read_reports` `evaluate_reports` `main` | Reads omp's cached usage; never calls a usage API. |
