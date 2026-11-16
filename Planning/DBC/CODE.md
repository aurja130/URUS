# Code

Code links to [CONCEPTS](CONCEPTS.md), [DEFINITIONS](DEFINITIONS.md), [TESTS](TESTS.md) and
[BENCHMARKS](BENCHMARKS.md). Only implemented and partial nodes have rows; the engine has no
code yet, so every physics node is planned and absent here.

| Node | Code | Notes |
| --- | --- | --- |
| `BranchGrid` | `external/BEAVRS_minimal/generate_models.py` `T_fuel_grid` `mod_states` `CR_grid` | The present grid: 6 fuel temperatures × 6 moderator states × 2 rod states, no boron axis. Moderator density from IAPWS-IF97 at 15.5 MPa. |
| `FullCoreReference` | `external/BEAVRS_minimal/make_beavrs.py` `BEAVRS` | Builds the full-core BEAVRS OpenMC model at one branch state. |
| `FullCoreReference` | `external/BEAVRS_minimal/run_model.py` `mgxs_lib` `data_out` | Runs one state and turns the node-wise tallies into two-group cross sections and eight-group kinetics data. |
| `FullCoreReference` | `external/BEAVRS_minimal/sweep.py` `parse_state_from_folder` `main` | Collects every state into the present nodal library file, NaN where a tally is empty. |
| `ToolchainPin` | `tools/toolchain/check_toolchain.py` `TOOL_SPECS` `run_checks` `compare_manifest` `render_manifest` | Run as `python3 -m tools.toolchain.check_toolchain`. |
| `ToolchainPin` | `manifests/toolchain.txt` | The pinned versions. |
| `UsageGuard` | `tools/agent/usage_guard.py` `read_reports` `evaluate_reports` `main` | Reads omp's cached usage; never calls a usage API. |
