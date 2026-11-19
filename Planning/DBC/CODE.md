# Code

Code links to [CONCEPTS](CONCEPTS.md), [DEFINITIONS](DEFINITIONS.md), [TESTS](TESTS.md) and
[BENCHMARKS](BENCHMARKS.md). Only implemented and partial nodes have rows; every physics node
is still planned and absent here.

| Node | Code | Notes |
| --- | --- | --- |
| `BuildPresets` | `CMakePresets.json` | The five presets `dev-gcc`, `dev-clang`, `asan-ubsan-clang`, `tsan-gcc`, `release-gcc`, each with configure, build and test parts. |
| `BuildPresets` | `CMakeLists.txt` `nts_warnings` `NTS_SANITIZERS` `CMAKE_CXX_SCAN_FOR_MODULES` | C++23 without extensions, no module scanning; warnings as errors on our targets only; the sanitizer switch the presets set. |
| `BuildPresets` | `src/CMakeLists.txt` `nts_core` | The engine library. |
| `BuildPresets` | `tests/unit/CMakeLists.txt` `nts_unit_tests` `gtest_discover_tests` | The unit-test executable; CTest discovers each test. |
| `BuildPresets` | `tests/end_to_end/CMakeLists.txt` `nts_end_to_end_tests` `NTS_PRESENT_LIBRARY` | The end-to-end test executable; reads the library committed in the submodule. |
| `LocalCi` | `tools/ci/local_ci.py` `STEPS` `PRESETS` `Step` `select_steps` `run_steps` `clang_tidy_check` `clang_format_check` `main` | Run as `python3 -m tools.ci.local_ci`; cheapest step first, clang-tidy last; stops at the first failed step; an `--only` run that leaves a step out prints `SKIP` for it and never reports a full pass. |
| `LocalCi` | `tools/ci/license_headers.py` `header_problem` `candidate_files` `check_files` | The license-header step. |
| `LocalCi` | `tools/ci/__init__.py` `repository_files` | The files of ours: tracked plus untracked, not ignored. |
| `LocalCi` | `.clang-format` | Formatting rules the clang-format step enforces. |
| `LocalCi` | `.clang-tidy` | Static-analysis checks the clang-tidy step enforces; each exclusion carries its reason. |
| `RuntimeChecks` | `src/nts/common/invariant.hpp` `NTS_ASSERT` `fail_invariant` | The invariant check. |
| `RuntimeChecks` | `src/nts/common/floating_point.hpp` `configure_floating_point_environment` `floating_point_traps_enabled` | The floating-point traps, on in every build without `NDEBUG`; called first in every main of ours. |
| `RuntimeChecks` | `src/nts/common/floating_point.cpp` `__asan_default_options` `__tsan_default_options` `__ubsan_default_options` | Tells the sanitizer runtimes not to catch SIGFPE, so a trap stops the program under a sanitizer too. |
| `RuntimeChecks` | `tests/unit/main.cpp` `main` | The unit tests run with the traps of their build. |
| `BranchGrid` | `external/BEAVRS_minimal/generate_models.py` `T_fuel_grid` `mod_states` `CR_grid` | The present grid: 6 fuel temperatures × 6 moderator states × 2 rod states, no boron axis. Moderator density from IAPWS-IF97 at 15.5 MPa. |
| `FullCoreReference` | `external/BEAVRS_minimal/make_beavrs.py` `BEAVRS` | Builds the full-core BEAVRS OpenMC model at one branch state. |
| `FullCoreReference` | `external/BEAVRS_minimal/run_model.py` `run_model` `BATCHES` `INACTIVE_BATCHES` `PARTICLES` | Transports one state's full-core model with the node-wise tallies; the statepoint it writes is the run's output. |
| `FullCoreReference` | `external/BEAVRS_minimal/sweep.py` `main` | Runs every state without a statepoint, then the library writer. |
| `FullCoreReference` | `external/BEAVRS_minimal/nodal_extraction.py` `build_mgxs_library` `extract_run` `RunData` `Estimate` | The one extraction of node-wise means and standard deviations from a statepoint, shared by the run and the library writer. |
| `FullCoreReference` | `external/BEAVRS_minimal/library_writer.py` `run_quantities` `restore_infinities` `effective_downscatter` `delayed_fractions` `delayed_fraction_std` `kappa_to_joule` | The library writer's quantities: infinities restored, effective down-scatter, delayed fractions, standard deviations propagated to first order, κΣf in joules. |
| `FullCoreReference` | `external/BEAVRS_minimal/library_writer.py` `rule_violations` `valid_flags` `branch_state` `composition_names` `assemble_tables` `write_library` | Entry rules and validity flags, branch states read from the run's materials, composition names, the composition set and its provenance, written atomically. |
| `FullCoreReference` | `external/BEAVRS_minimal/library_writer.py` `reproduce` `raw_datasets` | The reproduction check against the library it replaces. |
| `LibraryContract` | `docs/contracts/nodal_library.md` | The library contract, format version 1. |
| `LibraryContract` | `external/BEAVRS_minimal/beavrs_kinetics_library.hdf5` | The present library under the contract: set `beavrs_full_core`, 7650 compositions, 36 branch states. |
| `LibraryReader` | `src/nts/io/nodal_library.hpp` `read_nodal_library` `NodalLibrary` `CompositionSet` `QuantityTable` `AxisPoints` `StateAxis` `Quantity` `QuantitySpec` `EntryRule` `quantity_specs` `quantity_spec` `state_axis_name` `state_axis_units` | The library in memory and the contract's vocabulary: state axes, quantities with their units, shapes and entry rules. |
| `LibraryReader` | `src/nts/io/nodal_library.cpp` `read_root_attributes` `read_axes` `read_names` `read_quantity` `read_valid` `check_valid_entries` `component_rule_message` `delayed_sum_message` `std_dev_message` `EntryReporter` `guarded_read` `fixed_length_text` | Reading and every check of contract §4; all problems collected, an HDF5 failure on one dataset or attribute reported there; at most 20 diagnostics per dataset, then the number not listed. |
| `LibraryReader` | `src/nts/io/nodal_library.cpp` `breaks_entry_rule` `count_rule_violations` `check_compositions_usable` `invalid_entry_message` `describe_entry` `describe_component` | Entry rules applied to entries of any flag; the check of the compositions a problem uses, naming each invalid entry and the rules it breaks. |
| `LibraryReader` | `src/nts/common/diagnostic.hpp` `Diagnostic` `Diagnostics` `to_string` | One problem: file, dataset or attribute, entry, message. |
| `ToolchainPin` | `tools/toolchain/check_toolchain.py` `TOOL_SPECS` `run_checks` `compare_manifest` `render_manifest` | Run as `python3 -m tools.toolchain.check_toolchain`. |
| `ToolchainPin` | `manifests/toolchain.txt` | The pinned versions. |
| `UsageGuard` | `tools/agent/usage_guard.py` `read_reports` `evaluate_reports` `main` | Reads omp's cached usage; never calls a usage API. |
