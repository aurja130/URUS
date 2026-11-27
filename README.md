# URUS

A coupled 3D nodal neutronics and thermal-hydraulics simulator for pressurized water
reactors. It is built around the MIT BEAVRS benchmark and is meant for students exploring
reactor behaviour through a few operator controls: control rods, boron, coolant flow and
steam generator heat load.

The engine is a C++23 command-line program. Its input and output are HDF5 files with
versioned contracts in `docs/contracts/`. The physics follows the PARCS and SIMULATE-5
methods.

**Status:** work in progress (milestone 1). Today `urus` solves the steady-state two-group
eigenvalue problem by coarse-mesh finite differences (CMFD) and writes the eigenvalue, node
fluxes and powers and a run record. The nodal kernels, thermal-hydraulics and transients
come later; see `Planning/PROJECT_PLAN.md`.

## Requirements

- GCC or Clang with C++23 support, CMake ≥ 3.28 and Ninja
- HDF5 ≥ 1.14 with its C++ API, Eigen ≥ 5.0 and GoogleTest
- Python 3.13 with `h5py` and `numpy`, for writing problem inputs

`python3 -m tools.toolchain.check_toolchain` checks the pinned tool versions in
`manifests/toolchain.txt`.

## Build and test

The CMake presets are `dev-gcc`, `dev-clang`, `asan-ubsan-clang`, `tsan-gcc` and `release-gcc`:

```sh
cmake --preset release-gcc
cmake --build --preset release-gcc
ctest --preset release-gcc
```

The whole local CI runs formatting, lint, type checks, pytest, every preset and clang-tidy:

```sh
python3 -m tools.ci.local_ci
```

## Usage

1. Write a problem input with the Python writer in `tools/inputs/problem_input.py`. A
   problem input holds four documents: the reactor (geometry and nodal library), the
   initial state, the control signals and the settings (solver options). Each document is
   stored in the problem file or in its own file that the problem file links to. The
   contract is `docs/contracts/problem_input.md`.

   `tools/inputs/cases.py` shows complete examples. They are the problems the end-to-end
   tests run, written against the synthetic test library
   (`python3 -m tools.inputs.cases <case> <output_dir> --library <library file>`).

2. Run the engine:

   ```sh
   build/release-gcc/app/urus problem.h5 -o output.h5
   ```

   `urus` prints the eigenvalue and the convergence state. Diagnostics go to standard
   error. The output file follows `docs/contracts/output.md`.

| Exit status | Meaning | Output written |
|---|---|---|
| 0 | Converged, no table clamp | yes |
| 1 | Usage error, or the output could not be written | no |
| 2 | Problem input or library rejected, or an unsupported method requested | no |
| 3 | Not converged within the outer iteration limit | yes |
| 4 | Converged, but a node state was clamped to the library's tables | yes |

## Repository layout

| Path | Contents |
|---|---|
| `src/nts/` | Engine library: input, cross sections, CMFD, eigenvalue solve, output |
| `app/` | The `urus` executable |
| `tests/` | Unit, verification and end-to-end tests |
| `tools/` | Python tooling: input writer, reference solutions, toolchain pin, local CI |
| `docs/contracts/` | File-format contracts for the nodal library, the problem input and the output |
| `Planning/` | Vision, project and milestone plans, current state, concept maps |
| `external/BEAVRS_minimal` | Submodule: BEAVRS models and nodal-library generation with OpenMC |

## License

GPL-3.0-or-later; see `LICENSE`.
