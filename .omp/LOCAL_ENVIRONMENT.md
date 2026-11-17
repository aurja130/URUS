# Development Environment

## Machine

- Machine name: `fedora-work`
- Hostname: `fedora-work`
- Architecture: `x86_64`
- Kernel: `Linux 7.3.3-200.fc44.x86_64`
- Operating system: `Fedora Linux 44 (KDE Plasma Desktop Edition)`
- Shell: `/bin/bash`
- Repository root: `/home/aurja/Programming/CPP_neutronic_thermalhydraulic_simulator`

## OMP

- OMP
  - Command: `omp` (a shell function that runs `omp --session-dir <repo>/.omp/sessions` inside a git repository)
  - Path: `/home/aurja/.local/bin/omp`
  - Version: `omp/18.20.3`
- Project OMP directory: `.omp/`
- Project LSP configuration: `.omp/lsp.json` (clangd reads `build/dev-clang/compile_commands.json`)
- Project session directory: `.omp/sessions/`
- Local environment file: `.omp/LOCAL_ENVIRONMENT.md`

## Python

`.python-version` selects the pyenv virtualenv `openmc_venv`, so `python3` in the
repository is that environment. The repository tooling (`tools/`) runs on it.

- Python
  - Command: `python3`
  - Path: `/home/aurja/.pyenv/versions/openmc_venv/bin/python3`
  - Version: `Python 3.13.12`
  - Packages used here: `openmc 0.15.4.dev94`, `h5py 3.16.0` (HDF5 2.0.0 bundled),
    `numpy 2.4.5`, `scipy 1.17.1`, `pandas 3.0.3`, `matplotlib 3.10.9`, `pytest 9.2.0`
  - `import beavrs` resolves to `~/Programming/OpenMC_inputs/BEAVRS/models/openmc/beavrs/`
    (a separate checkout), not to `external/BEAVRS_minimal/beavrs/`. Scripts run inside
    `external/BEAVRS_minimal/` import the local package first.

- pytest
  - Command: `pytest`
  - Path: `/home/aurja/.pyenv/versions/openmc_venv/bin/pytest`
  - Version: `pytest 9.2.0`
  - A third-party plugin (`anyio`) prints a `PytestRemovedIn10Warning` on every run; harmless.

- basedpyright
  - Command: `basedpyright`
  - Path: `/home/aurja/.local/bin/basedpyright`
  - Version: `basedpyright 1.39.10`

- Ruff
  - Command: `ruff`
  - Path: `/home/aurja/.local/bin/ruff`
  - Version: `ruff 0.16.6`

## C and C++

- GCC
  - Command: `gcc`
  - Path: `/usr/bin/gcc`
  - Version: `gcc (GCC) 16.2.1 20261029 (Red Hat 16.2.1-3)`

- G++
  - Command: `g++`
  - Path: `/usr/bin/g++`
  - Version: `g++ (GCC) 16.2.1 20261029 (Red Hat 16.2.1-3)`

- Clang
  - Command: `clang`
  - Path: `/usr/bin/clang`
  - Version: `clang version 22.1.8 (Fedora 22.1.8-4.fc44)`

- Clang++
  - Command: `clang++`
  - Path: `/usr/bin/clang++`
  - Version: `clang version 22.1.8 (Fedora 22.1.8-4.fc44)`

- clangd
  - Command: `clangd`
  - Path: `/usr/bin/clangd`
  - Version: `clangd version 22.1.8 (Fedora 22.1.8-4.fc44)`

- clang-tidy
  - Command: `clang-tidy`
  - Path: `/usr/bin/clang-tidy`
  - Version: `LLVM version 22.1.8`

- clang-format
  - Command: `clang-format`
  - Path: `/usr/bin/clang-format`
  - Version: `clang-format version 22.1.8 (Fedora 22.1.8-4.fc44)`

- CMake
  - Command: `cmake`
  - Path: `/usr/bin/cmake`
  - Version: `cmake version 4.3.0`

- CTest
  - Command: `ctest`
  - Path: `/usr/bin/ctest`
  - Version: `ctest version 4.3.0`

- Ninja
  - Command: `ninja`
  - Path: `/usr/bin/ninja`
  - Version: `1.13.2`

- Universal Ctags
  - Command: `ctags`
  - Path: `/usr/bin/ctags`
  - Version: `Universal Ctags 6.2.1, Copyright (C) 2015-2025 Universal Ctags Team`

- GDB
  - Command: `gdb`
  - Path: `/usr/bin/gdb`
  - Version: `GNU gdb (Fedora Linux) 18.1-1.fc44`

- GNU patch
  - Command: `patch`
  - Path: `/usr/bin/patch`
  - Version: `GNU patch 2.8`

- glibc
  - Version: `ldd (GNU libc) 2.43`

### Libraries

- HDF5 (serial, with the C++ API)
  - Packages: `hdf5-1.14.6-7.fc44`, `hdf5-devel-1.14.6-7.fc44`
  - Header: `/usr/include/H5Cpp.h`; libraries: `-lhdf5_cpp -lhdf5` (`/usr/lib64/libhdf5_cpp.so.310`)
  - Wrappers and tools: `h5c++`, `h5cc`, `h5dump`, `h5diff` in `/usr/bin`
    (`h5c++ -showconfig` reports `HDF5 Version: 1.14.6`, `Parallel HDF5: no`)
- Eigen: `eigen3-devel-5.0.1-3.fc44` (headers in `/usr/include/eigen3`)
- fmt: `fmt-devel-11.2.0-4.fc44`
- Open MPI: `openmpi-devel-5.0.9-4.fc44`, not on `PATH`; `module load mpi/openmpi-x86_64`
  puts `mpicxx` and `mpirun` (Open MPI 5.0.9) from `/usr/lib64/openmpi/bin` on it.
  `mpi/mpich-x86_64` is also available as a module.
- GoogleTest: `gtest-devel-1.17.0-2.fc44` (`pkg-config --modversion gtest`; CMake config in
  `/usr/lib64/cmake/GTest/`); GoogleMock is not installed.
- Not installed: Catch2.

### Compiler/runtime validation

Each facility below was verified when this file was generated: a small C++20 program was
compiled with the flags and run, with no diagnostic. The TSan probe runs two threads.

- Compiler warnings:
  - `-Wall -Wextra -Wpedantic -Werror`
  - Verified with: `g++` `clang++`
- AddressSanitizer:
  - `-fsanitize=address -fno-omit-frame-pointer`
  - Verified with: `g++` `clang++`
- UndefinedBehaviorSanitizer:
  - `-fsanitize=undefined -fno-omit-frame-pointer`
  - Verified with: `g++` `clang++`
- ThreadSanitizer:
  - `-fsanitize=thread -fno-omit-frame-pointer`
  - Verified with: `g++` `clang++`
- HDF5 C++ API and Eigen:
  - A program writing a dataset through `H5::H5File` and using `Eigen::Matrix2d`, built with
    `-std=c++20 -Wall -Wextra -Wpedantic -Werror -I/usr/include/eigen3 -lhdf5_cpp -lhdf5`
  - Verified with: `g++` `clang++`
- `-std=c++23` compiles with both compilers.
- Debug-friendly sanitizer option:
  - `-fno-omit-frame-pointer`
- TSan cannot be combined with ASan in one build; use separate builds.

## OpenMC (nodal data generation)

- OpenMC executable
  - Command: `openmc`
  - Path: `/home/aurja/.local/bin/openmc`
  - Version: `OpenMC version 0.15.4-dev94` (commit `417343c9203c9337547d1eeeefb8a9fd0ec536fd`)
  - Build: `RelWithDebInfo`, GNU 15.2.1, MPI enabled, parallel HDF5 enabled, PNG support;
    no DAGMC, libMesh or MCPL
- Python API: `openmc` in `openmc_venv` (see **Python**)
- Nuclear data: `OPENMC_CROSS_SECTIONS=/home/aurja/Nuclear_Software/openmc-data/jeff-4.0-hdf5/cross_sections.xml`
  (JEFF-4.0; the only library in `/home/aurja/Nuclear_Software/openmc-data/`)

## Reference material outside the repository

- `Resources/` (gitignored): reference PDFs (PARCS theory manual, SIMULATE-5, BEAVRS 2.0.2
  specification, nodal-method, kinetics and thermal-hydraulics texts).
- `~/Programming/OpenMC_inputs/BEAVRS/` (`github.com/aurja130/MIT-BEAVRS`): full BEAVRS
  repository with `measurements/`, `docs/`, the specification and the UQ report.

## SQL

- sqls
  - Command: `sqls`
  - Path: `/home/aurja/go/bin/sqls`
  - Version: `sqls version Version:0.2.48, Revision:HEAD`

## Go

- Go
  - Command: `go`
  - Path: `/usr/bin/go`
  - Version: `go version go1.26.10-X:nodwarf5 linux/amd64`

## Git

- Git
  - Command: `git`
  - Path: `/usr/bin/git`
  - Version: `git version 2.56.1`
- Remotes: `origin https://github.com/aurja130/URUS.git`. The submodule `external/BEAVRS_minimal` has
  `origin https://codeberg.org/aurja130/BEAVRS_minimal.git`.

## Executable search path

```text
/home/aurja/go/bin
/home/aurja/.pyenv/plugins/pyenv-virtualenv/shims
/home/aurja/.pyenv/shims
/home/aurja/.pyenv/bin
/home/aurja/go/bin
/home/aurja/.pyenv/plugins/pyenv-virtualenv/shims
/home/aurja/.pyenv/bin
/home/aurja/go/bin
/home/aurja/.pyenv/plugins/pyenv-virtualenv/shims
/home/aurja/.pyenv/bin
/home/aurja/.local/bin
/home/aurja/bin
/usr/share/Modules/bin
/usr/local/bin
/usr/bin
/usr/local/sbin
/usr/sbin
/home/aurja/.local/bin/
/home/aurja/.lmstudio/bin
/home/aurja/.lmstudio/bin
/home/aurja/.local/bin/
/home/aurja/.lmstudio/bin
/home/aurja/.local/bin/
/home/aurja/.lmstudio/bin
```

## Notes

- Prefer commands available through `PATH` rather than hard-coded absolute paths.
- `python3 -m tools.toolchain.check_toolchain` checks the pinned tool versions against
  `manifests/toolchain.txt`.
