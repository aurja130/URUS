// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Aurora Jahan
// Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

#pragma once

// The output of a run (docs/contracts/output.md, format version 1): eigenvalue, solution
// fields and the run record.

#include "nts/common/build_info.hpp"
#include "nts/io/problem.hpp"
#include "nts/solve/eigenvalue.hpp"
#include "nts/solve/steady_state.hpp"
#include "nts/xs/cross_section_model.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>

namespace nts {

inline constexpr std::int64_t output_format_version = 1;

// Output contract §2.
enum class ExitStatus : std::uint8_t {
    valid = 0,
    usage_or_write_error = 1,
    rejected = 2,
    not_converged = 3,
    clamped = 4
};

// Output contract §2, §6: not converged gives not_converged (also when clamped); otherwise any
// clamp count > 0 gives clamped; otherwise valid.
struct RunClassification {
    ExitStatus exit_status = ExitStatus::valid;
    bool valid = false;
    // "converged"; "not converged: outer iteration limit <n> reached";
    // "converged with table clamps: <n> node-axis clamps" (n the sum of all counts).
    std::string reason;
};

[[nodiscard]] RunClassification classify_run(const ConvergenceState& convergence,
                                             std::int64_t outer_iteration_limit,
                                             const ClampCounts& clamp_counts);

// One input file of the run: absolute path and SHA-256 (64 lower-case hex digits); both empty
// for an absent optional document.
struct FileProvenance {
    std::filesystem::path file;
    std::string sha256;
};

// Everything the run record of output contract §6 holds.
struct RunRecord {
    RunClassification classification;
    ConvergenceState convergence;
    ClampCounts clamp_counts{};
    SolverOptions options;
    bool allow_table_clamps = false;
    FileProvenance problem_input;
    std::array<FileProvenance, input_document_count> documents; // InputDocument order
    FileProvenance library;
    BuildInfo build;
    double wall_time_s = 0.0;
};

// Writes output contract §3-§6 for `reactor` and `state` to `file`: first to a temporary file
// in the same directory, renamed onto `file` when complete. Returns an error message naming
// the file on failure, after removing the temporary file; `file` is then untouched.
// Preconditions: state.flux.size() == 2 * reactor.node_count(), state.power_w.size() ==
// state.volume_cm3.size() == reactor.node_count(); record.classification.exit_status is
// valid, not_converged or clamped.
[[nodiscard]] std::expected<void, std::string> write_output(const std::filesystem::path& file,
                                                            const ReactorDescription& reactor,
                                                            const SteadyState& state,
                                                            const RunRecord& record);

} // namespace nts
