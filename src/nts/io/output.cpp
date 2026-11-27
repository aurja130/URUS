// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Aurora Jahan
// Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

#include "nts/io/output.hpp"

#include "nts/cmfd/cmfd_system.hpp"
#include "nts/common/invariant.hpp"
#include "nts/io/nodal_library.hpp"
#include "nts/io/problem.hpp"
#include "nts/solve/eigenvalue.hpp"
#include "nts/solve/steady_state.hpp"
#include "nts/xs/cross_section_model.hpp"

#include <H5Cpp.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <unistd.h>
#include <vector>

namespace nts {
namespace {

// Output contract §1, §5, §6: the units, exactly as tabled.
constexpr std::string_view flux_units = "cm^-2 s^-1";
constexpr std::string_view power_units = "W";
constexpr std::string_view power_density_units = "W cm^-3";
constexpr std::string_view dimensionless_units = "1";
constexpr std::string_view time_units = "s";

// Output contract §6, methods/.
constexpr std::string_view eigenvalue_iteration_method =
    "Wielandt-shifted inverse iteration (TM Eqs. (4.8)-(4.9))";
constexpr std::string_view linear_solver_method = "BiCGSTAB (Eigen)";
constexpr std::string_view preconditioner_method =
    "incomplete LU with dual threshold (Eigen IncompleteLUT)";

H5::StrType utf8_string_type() {
    H5::StrType const type(H5::PredType::C_S1, H5T_VARIABLE);
    type.setCset(H5T_CSET_UTF8);
    return type;
}

// `Location` is H5::H5File or a group or dataset (anything that creates attributes).
template <typename Location>
void write_int_attribute(const Location& location, const std::string& name, std::int64_t value) {
    location.createAttribute(name, H5::PredType::STD_I64LE, H5::DataSpace(H5S_SCALAR))
        .write(H5::PredType::NATIVE_INT64, &value);
}

template <typename Location>
void write_string_attribute(const Location& location, const std::string& name,
                            std::string_view value) {
    const H5::StrType type = utf8_string_type();
    const std::string text(value);
    const char* pointer = text.c_str();
    location.createAttribute(name, type, H5::DataSpace(H5S_SCALAR))
        .write(type, static_cast<const void*>(&pointer));
}

void write_scalar_int(const H5::Group& group, const std::string& name, std::int64_t value) {
    group.createDataSet(name, H5::PredType::STD_I64LE, H5::DataSpace(H5S_SCALAR))
        .write(&value, H5::PredType::NATIVE_INT64);
}

// A float64 scalar; the `units` attribute is written only when `units` is not empty.
void write_scalar_double(const H5::Group& group, const std::string& name, double value,
                         std::string_view units = {}) {
    const H5::DataSet dataset =
        group.createDataSet(name, H5::PredType::IEEE_F64LE, H5::DataSpace(H5S_SCALAR));
    dataset.write(&value, H5::PredType::NATIVE_DOUBLE);
    if (!units.empty()) {
        write_string_attribute(dataset, "units", units);
    }
}

void write_scalar_string(const H5::Group& group, const std::string& name, std::string_view value) {
    const H5::StrType type = utf8_string_type();
    const std::string text(value);
    const char* pointer = text.c_str();
    group.createDataSet(name, type, H5::DataSpace(H5S_SCALAR))
        .write(static_cast<const void*>(&pointer), type);
}

H5::DataSet create_array(const H5::Group& group, const std::string& name, const H5::DataType& type,
                         const std::vector<hsize_t>& dims) {
    const H5::DataSpace space(static_cast<int>(dims.size()), dims.data());
    return group.createDataSet(name, type, space);
}

void write_double_array(const H5::Group& group, const std::string& name,
                        std::span<const double> values, const std::vector<hsize_t>& dims,
                        std::string_view units) {
    const H5::DataSet dataset = create_array(group, name, H5::PredType::IEEE_F64LE, dims);
    dataset.write(values.data(), H5::PredType::NATIVE_DOUBLE);
    write_string_attribute(dataset, "units", units);
}

void write_int_array(const H5::Group& group, const std::string& name,
                     std::span<const std::int64_t> values, const std::vector<hsize_t>& dims) {
    create_array(group, name, H5::PredType::STD_I64LE, dims)
        .write(values.data(), H5::PredType::NATIVE_INT64);
}

void write_string_array(const H5::Group& group, const std::string& name,
                        const std::vector<std::string>& values) {
    const H5::StrType type = utf8_string_type();
    const std::vector<hsize_t> dims{static_cast<hsize_t>(values.size())};
    std::vector<const char*> pointers;
    pointers.reserve(values.size());
    for (const std::string& value : values) {
        pointers.push_back(value.c_str());
    }
    create_array(group, name, type, dims).write(static_cast<const void*>(pointers.data()), type);
}

// Dimensions [nz][ny][nx] of an array over positions (output contract §1).
std::vector<hsize_t> position_dims(const ReactorDescription& reactor) {
    return {static_cast<hsize_t>(reactor.nz()), static_cast<hsize_t>(reactor.ny()),
            static_cast<hsize_t>(reactor.nx())};
}

// Output contract §4.
void write_eigenvalue(const H5::H5File& file, const SteadyState& state) {
    const H5::Group group = file.createGroup("eigenvalue");
    write_scalar_double(group, "k_eff", state.k_eff, dimensionless_units);
}

// Output contract §5: node values scattered to positions, excluded positions NaN.
void write_fields(const H5::H5File& file, const ReactorDescription& reactor,
                  const SteadyState& state) {
    constexpr double not_a_number = std::numeric_limits<double>::quiet_NaN();
    const std::size_t positions = reactor.position_count();
    const std::size_t nodes = reactor.node_count();
    NTS_ASSERT(state.flux.size() == energy_group_count * nodes,
               "write_fields: flux is not in unknown_index layout");
    NTS_ASSERT(state.power_w.size() == nodes && state.volume_cm3.size() == nodes,
               "write_fields: node arrays do not match the reactor");

    std::vector<double> flux(energy_group_count * positions, not_a_number);
    std::vector<double> power(positions, not_a_number);
    std::vector<double> density(positions, not_a_number);
    for (std::size_t node = 0; node < nodes; ++node) {
        const std::size_t position = reactor.node_positions[node];
        for (std::size_t group = 0; group < energy_group_count; ++group) {
            flux[(group * positions) + position] = state.flux[unknown_index(node, group)];
        }
        // TM Eq. (3.10) gives the node power; the density is P^m / V^m (output contract §5).
        power[position] = state.power_w[node];
        density[position] = state.power_w[node] / state.volume_cm3[node];
    }

    std::vector<hsize_t> flux_dims = position_dims(reactor);
    flux_dims.insert(flux_dims.begin(), static_cast<hsize_t>(energy_group_count));
    const H5::Group group = file.createGroup("fields");
    write_double_array(group, "flux", flux, flux_dims, flux_units);
    write_double_array(group, "power", power, position_dims(reactor), power_units);
    write_double_array(group, "power_density", density, position_dims(reactor),
                       power_density_units);
}

// Output contract §6, convergence/.
void write_convergence(const H5::Group& run_record, const ConvergenceState& convergence) {
    const H5::Group group = run_record.createGroup("convergence");
    write_scalar_int(group, "converged", convergence.converged ? 1 : 0);
    write_scalar_int(group, "outer_iterations", convergence.outer_iterations);
    write_scalar_int(group, "inner_iterations", convergence.inner_iterations);
    write_scalar_int(group, "nodal_updates", convergence.nodal_updates);
    write_scalar_double(group, "eigenvalue_change", convergence.eigenvalue_change,
                        dimensionless_units);
    write_scalar_double(group, "fission_source_l2_change", convergence.fission_source_l2_change,
                        dimensionless_units);
    write_scalar_double(group, "fission_source_linf_change", convergence.fission_source_linf_change,
                        dimensionless_units);
    write_scalar_double(group, "residual", convergence.residual, dimensionless_units);
    write_scalar_double(group, "coupling_correction_change", convergence.coupling_correction_change,
                        dimensionless_units);
}

// Output contract §6, table_clamps/: rows in the state axis order of the library contract.
void write_table_clamps(const H5::Group& run_record, const ClampCounts& clamp_counts) {
    const H5::Group group = run_record.createGroup("table_clamps");
    std::vector<std::int64_t> counts;
    counts.reserve(state_axis_count * 2);
    for (const auto& axis : clamp_counts) {
        for (const std::size_t count : axis) {
            counts.push_back(static_cast<std::int64_t>(count));
        }
    }
    const std::vector<hsize_t> dims{static_cast<hsize_t>(state_axis_count), 2};
    write_int_array(group, "counts", counts, dims);
}

// Output contract §6, options/: every setting as used, names of problem input contract §7.
void write_options(const H5::Group& run_record, const RunRecord& record) {
    const H5::Group group = run_record.createGroup("options");
    const SolverOptions& options = record.options;
    write_scalar_string(group, "kernel", kernel_choice_name(options.kernel));
    write_scalar_double(group, "kernel_switch_threshold", options.kernel_switch_threshold);
    write_scalar_double(group, "wielandt_shift", options.wielandt_shift);
    write_scalar_double(group, "eigenvalue_tolerance", options.eigenvalue_tolerance);
    write_scalar_double(group, "fission_source_l2_tolerance", options.fission_source_l2_tolerance);
    write_scalar_double(group, "fission_source_linf_tolerance",
                        options.fission_source_linf_tolerance);
    write_scalar_double(group, "inner_reduction", options.inner_reduction);
    write_scalar_int(group, "inner_iteration_limit", options.inner_iteration_limit);
    write_scalar_int(group, "outer_iteration_limit", options.outer_iteration_limit);
    write_scalar_int(group, "nodal_update_period", options.nodal_update_period);
    write_scalar_double(group, "coupling_relaxation", options.coupling_relaxation);
    write_scalar_int(group, "adjoint", options.adjoint ? 1 : 0);
    write_scalar_int(group, "allow_table_clamps", record.allow_table_clamps ? 1 : 0);
}

// Output contract §6, methods/.
void write_methods(const H5::Group& run_record, KernelChoice kernel) {
    const H5::Group group = run_record.createGroup("methods");
    write_scalar_string(group, "kernel", kernel_choice_name(kernel));
    write_scalar_string(group, "eigenvalue_iteration", eigenvalue_iteration_method);
    write_scalar_string(group, "linear_solver", linear_solver_method);
    write_scalar_string(group, "preconditioner", preconditioner_method);
}

// Output contract §6, provenance/.
void write_provenance(const H5::Group& run_record, const RunRecord& record) {
    const H5::Group group = run_record.createGroup("provenance");
    write_scalar_string(group, "problem_input", record.problem_input.file.string());
    write_scalar_string(group, "problem_input_sha256", record.problem_input.sha256);
    std::vector<std::string> document_files;
    std::vector<std::string> document_hashes;
    for (const FileProvenance& document : record.documents) {
        document_files.push_back(document.file.string());
        document_hashes.push_back(document.sha256);
    }
    write_string_array(group, "document_files", document_files);
    write_string_array(group, "document_sha256", document_hashes);
    write_scalar_string(group, "library", record.library.file.string());
    write_scalar_string(group, "library_sha256", record.library.sha256);
    write_scalar_string(group, "engine_version", record.build.engine_version);
    write_scalar_string(group, "engine_commit", record.build.engine_commit);
    write_scalar_string(group, "build_preset", record.build.build_preset);
    write_scalar_string(group, "build_type", record.build.build_type);
    write_scalar_double(group, "wall_time", record.wall_time_s, time_units);
}

// Output contract §6.
void write_run_record(const H5::H5File& file, const RunRecord& record) {
    const H5::Group group = file.createGroup("run_record");
    const RunClassification& classification = record.classification;
    write_int_attribute(group, "valid", classification.valid ? 1 : 0);
    write_int_attribute(group, "exit_status",
                        static_cast<std::int64_t>(classification.exit_status));
    write_string_attribute(group, "reason", classification.reason);
    write_convergence(group, record.convergence);
    write_table_clamps(group, record.clamp_counts);
    write_options(group, record);
    write_methods(group, record.options.kernel);
    write_provenance(group, record);
}

// Removes a temporary file left by a failed write; a file that is already gone is not an error.
void remove_temporary(const std::filesystem::path& temporary) {
    std::error_code error;
    std::filesystem::remove(temporary, error);
}

} // namespace

RunClassification classify_run(const ConvergenceState& convergence,
                               std::int64_t outer_iteration_limit,
                               const ClampCounts& clamp_counts) {
    // Output contract §2, §6; TM Eq. (4.10) gives `converged`.
    if (!convergence.converged) {
        return {.exit_status = ExitStatus::not_converged,
                .valid = false,
                .reason = std::format("not converged: outer iteration limit {} reached",
                                      outer_iteration_limit)};
    }
    std::size_t clamps = 0;
    for (const auto& axis : clamp_counts) {
        for (const std::size_t count : axis) {
            clamps += count;
        }
    }
    if (clamps > 0) {
        return {.exit_status = ExitStatus::clamped,
                .valid = false,
                .reason = std::format("converged with table clamps: {} node-axis clamps", clamps)};
    }
    return {.exit_status = ExitStatus::valid, .valid = true, .reason = "converged"};
}

std::expected<void, std::string> write_output(const std::filesystem::path& file,
                                              const ReactorDescription& reactor,
                                              const SteadyState& state, const RunRecord& record) {
    NTS_ASSERT(state.flux.size() == energy_group_count * reactor.node_count(),
               "write_output: flux is not in unknown_index layout");
    NTS_ASSERT(state.power_w.size() == reactor.node_count() &&
                   state.volume_cm3.size() == reactor.node_count(),
               "write_output: node arrays do not match the reactor");
    const ExitStatus status = record.classification.exit_status;
    NTS_ASSERT(status == ExitStatus::valid || status == ExitStatus::not_converged ||
                   status == ExitStatus::clamped,
               "write_output: the run has no output to write");
    NTS_ASSERT(record.classification.valid == (status == ExitStatus::valid),
               "write_output: classification valid flag disagrees with the exit status");

    const std::filesystem::path directory =
        file.has_parent_path() ? file.parent_path() : std::filesystem::path(".");
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error)) {
        return std::unexpected(std::format("cannot write '{}': directory '{}' does not exist",
                                           file.string(), directory.string()));
    }

    const std::filesystem::path temporary =
        directory / std::format(".{}.tmp-{}", file.filename().string(), ::getpid());
    try {
        // Closed explicitly before the rename, inside the try: a failure while HDF5 flushes the
        // file (ENOSPC, EIO, quota) must fail the write. The destructor would only report it.
        H5::H5File output(temporary.string(), H5F_ACC_TRUNC);
        write_int_attribute(output, "format_version", output_format_version);
        write_eigenvalue(output, state);
        write_fields(output, reactor, state);
        write_run_record(output, record);
        output.close();
        std::filesystem::rename(temporary, file);
    } catch (const H5::Exception& exception) {
        remove_temporary(temporary);
        return std::unexpected(std::format("cannot write '{}': HDF5 {} failed: {}", file.string(),
                                           exception.getFuncName(), exception.getDetailMsg()));
    } catch (const std::filesystem::filesystem_error& failure) {
        remove_temporary(temporary);
        return std::unexpected(std::format("cannot write '{}': {}", file.string(), failure.what()));
    }
    return {};
}

} // namespace nts
