# Tests

Tests are mapped to [CONCEPTS](CONCEPTS.md), [CODE](CODE.md), [DEFINITIONS](DEFINITIONS.md)
and [BENCHMARKS](BENCHMARKS.md). Verification and validation cases of the physics are in
BENCHMARKS; this file lists the automated tests.

| Test | Checks | Concepts |
| --- | --- | --- |
| `tools/toolchain/tests/test_check_toolchain.py` | Version parsing, manifest comparison and rendering of the toolchain check; GoogleTest and Eigen read through pkg-config, an unknown package fails with its install hint | `ToolchainPin` |
| `tests/unit/common/invariant_test.cpp` | A violated invariant check aborts with expression, message, file and line on stderr; the condition is evaluated once; the message is not evaluated when the condition holds. Runs in every preset, release included | `RuntimeChecks` |
| `tests/unit/common/floating_point_test.cpp` | An invalid operation, a division by zero and an overflow each stop the program with SIGFPE in the debug and sanitizer presets, and yield NaN, +∞ and +∞ in release | `RuntimeChecks`, `BuildPresets` |
| `tools/ci/tests/test_license_headers.py` | Header accepted after an optional shebang with either comment prefix; missing, short, wrong-prefix, wrong-text and out-of-range-year headers reported; file selection keeps our code files and skips `external/`, JSON, data and the concept-map checker | `LocalCi` |
| `tools/ci/tests/test_local_ci.py` | Step order; each preset configured, built and tested; `--only` and `--list`; a failing or missing command fails its step without a crash; the run stops at the first failure and prints its tail; clang-tidy file selection from the compile database | `LocalCi` |
