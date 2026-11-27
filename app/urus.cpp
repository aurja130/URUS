// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Aurora Jahan
// Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

// urus: solves the steady state of a problem input and writes the output file (output contract §2).

#include "nts/common/build_info.hpp"
#include "nts/common/diagnostic.hpp"
#include "nts/common/floating_point.hpp"
#include "nts/common/sha256.hpp"
#include "nts/io/output.hpp"
#include "nts/io/problem.hpp"
#include "nts/solve/steady_state.hpp"
#include "nts/xs/cross_section_model.hpp"

#include <chrono>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <expected>
#include <filesystem>
#include <print>
#include <string>
#include <string_view>
#include <utility>

namespace {

constexpr int exit_usage = 1;
constexpr int exit_rejected = 2;

void print_diagnostics(const nts::Diagnostics& diagnostics) {
    for (const nts::Diagnostic& diagnostic : diagnostics) {
        std::println(stderr, "{}", nts::to_string(diagnostic));
    }
}

// Output contract §6 provenance: absolute path and SHA-256 of an input file; empty when absent.
std::expected<nts::FileProvenance, std::string> file_provenance(const std::filesystem::path& file) {
    nts::FileProvenance provenance;
    if (file.empty()) {
        return provenance;
    }
    provenance.file = std::filesystem::absolute(file);
    std::expected<std::string, std::string> hash = nts::sha256_file_hex(provenance.file);
    if (!hash) {
        return std::unexpected(hash.error());
    }
    provenance.sha256 = std::move(*hash);
    return provenance;
}

std::expected<void, std::string> record_inputs(const nts::Problem& problem,
                                               nts::RunRecord& record) {
    std::expected<nts::FileProvenance, std::string> input = file_provenance(problem.input.file);
    if (!input) {
        return std::unexpected(input.error());
    }
    record.problem_input = std::move(*input);
    for (std::size_t document = 0; document < nts::input_document_count; ++document) {
        std::expected<nts::FileProvenance, std::string> provenance =
            file_provenance(problem.input.document_files[document]);
        if (!provenance) {
            return std::unexpected(provenance.error());
        }
        record.documents[document] = std::move(*provenance);
    }
    std::expected<nts::FileProvenance, std::string> library =
        file_provenance(problem.input.binding.library_file);
    if (!library) {
        return std::unexpected(library.error());
    }
    record.library = std::move(*library);
    return {};
}

int run_urus(int argc, char** argv) {
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    if (argc != 4 || std::string_view(argv[2]) != "-o") {
        std::println(stderr, "usage: urus <problem input> -o <output>");
        return exit_usage;
    }
    const std::filesystem::path input_file = argv[1];
    const std::filesystem::path output_file = argv[3];

    std::expected<nts::Problem, nts::Diagnostics> loaded = nts::load_problem(input_file);
    if (!loaded) {
        print_diagnostics(loaded.error());
        return exit_rejected;
    }
    const nts::Problem problem = std::move(*loaded);
    const nts::SolverOptions& options = problem.input.solver_options;

    nts::RunRecord record;
    std::expected<void, std::string> inputs = record_inputs(problem, record);
    if (!inputs) {
        std::println(stderr, "urus: {}", inputs.error());
        return exit_rejected;
    }

    const nts::CrossSectionField field = nts::evaluate_cross_sections(problem);
    const nts::Diagnostics solvable = nts::check_solvable(problem.input, options, field.nodes);
    if (!solvable.empty()) {
        print_diagnostics(solvable);
        return exit_rejected;
    }

    const nts::SteadyState state = nts::solve_steady_state(
        problem.input.reactor, field.nodes, options, problem.input.operating_state.thermal_power_w);
    const double wall_time_s =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

    record.classification =
        nts::classify_run(state.convergence, options.outer_iteration_limit, field.clamp_counts);
    record.convergence = state.convergence;
    record.clamp_counts = field.clamp_counts;
    record.options = options;
    record.allow_table_clamps = problem.input.allow_table_clamps;
    record.build = nts::build_info();
    record.wall_time_s = wall_time_s;

    const std::expected<void, std::string> written =
        nts::write_output(output_file, problem.input.reactor, state, record);
    if (!written) {
        std::println(stderr, "urus: {}", written.error());
        return exit_usage;
    }

    std::println("k_eff = {:.10f}", state.k_eff);
    std::println("converged = {}, outer iterations = {}, eigenvalue change = {:.3e}",
                 state.convergence.converged, state.convergence.outer_iterations,
                 state.convergence.eigenvalue_change);
    std::println("status = {} ({})", static_cast<int>(record.classification.exit_status),
                 record.classification.reason);
    return static_cast<int>(record.classification.exit_status);
}

} // namespace

int main(int argc, char** argv) {
    // The handlers print with C stdio, which does not throw, so no exception leaves main.
    try {
        nts::configure_floating_point_environment();
        return run_urus(argc, argv);
    } catch (const std::exception& failure) {
        std::fputs("urus: ", stderr);
        std::fputs(failure.what(), stderr);
        std::fputs("\n", stderr);
    } catch (...) {
        std::fputs("urus: unknown error\n", stderr);
    }
    return exit_usage;
}
