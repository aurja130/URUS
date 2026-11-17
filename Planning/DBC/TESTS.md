# Tests

Tests are mapped to [CONCEPTS](CONCEPTS.md), [CODE](CODE.md), [DEFINITIONS](DEFINITIONS.md)
and [BENCHMARKS](BENCHMARKS.md). Verification and validation cases of the physics are in
BENCHMARKS; this file lists the automated tests.

| Test | Checks | Concepts |
| --- | --- | --- |
| `tools/toolchain/tests/test_check_toolchain.py` | Version parsing, manifest comparison and rendering of the toolchain check; GoogleTest and Eigen read through pkg-config; an unknown package fails as unrecognised version output with its install hint | `ToolchainPin` |
| `tests/unit/common/invariant_test.cpp` | A violated invariant check aborts with expression, message, file, the exact line and the function on stderr; the condition is evaluated once; the message is not evaluated when the condition holds. Runs in every preset, release included | `RuntimeChecks` |
| `tests/unit/common/floating_point_test.cpp` | An invalid operation, a division by zero and an overflow each stop the program with SIGFPE in the debug and sanitizer presets, also when the binary runs without the presets' sanitizer options, and yield NaN, +∞ and +∞ in release; underflow and inexact results never trap | `RuntimeChecks`, `BuildPresets` |
| `tools/ci/tests/test_license_headers.py` | Header accepted after an optional shebang with either comment prefix; missing, short, wrong-prefix, wrong-copyright, wrong-license and out-of-range-year (before 2026 or after this year) headers reported; file selection keeps our code files and skips `external/`, JSON, data and the concept-map checker; a run that finds no code file of ours fails | `LocalCi` |
| `tools/ci/tests/test_local_ci.py` | Steps cheapest first with clang-tidy last; each preset step configures, builds and tests its own preset; each other step runs exactly its command; an `--only` run that leaves a step out prints `SKIP` for it and never claims a full pass, one naming every step is a full run; `--list`; a failing or missing command fails its step without a crash; the run stops at the first failure and prints its tail; clang-format and clang-tidy fail on an empty selection, clang-tidy also when the compile database lacks an engine source, and on any finding | `LocalCi` |
