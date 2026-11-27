// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Aurora Jahan
// Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

// End-to-end tests of the `urus` executable. The problem inputs are written by
// tools/inputs/cases.py against the C++ library fixture, run through the binary, and the
// output is read back. Exit statuses and output members follow docs/contracts/output.md
// (sections 2 and 6).

#include <gtest/gtest.h>

#include <H5Cpp.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <iterator>
#include <numeric>
#include <string>
#include <string_view>
#include <system_error>
#include <unistd.h>
#include <vector>

#include "../unit/io/library_fixture.hpp"

namespace nts::test {
namespace {

namespace fs = std::filesystem;

constexpr int status_ok = 0;
constexpr int status_usage_or_write = 1;
constexpr int status_rejected = 2;
constexpr int status_not_converged = 3;
constexpr int status_clamped = 4;

// The grid of tools/inputs/cases.py: [nz][ny][nx] nodes, widths in cm.
constexpr std::size_t nx = 3;
constexpr std::size_t ny = 3;
constexpr std::size_t nz = 2;
constexpr std::size_t nodes = nx * ny * nz;
constexpr std::array<double, nx> width_x_cm{10.0, 10.0, 10.0};
constexpr std::array<double, ny> width_y_cm{10.0, 10.0, 10.0};
constexpr std::array<double, nz> width_z_cm{15.0, 15.0};
constexpr double thermal_power_w = 1.0e8;
constexpr std::size_t input_documents = 4; // reactor, initial_state, control_signals, settings
constexpr std::size_t clamp_axes = 4;      // fuel_temperature, density, temperature, boron
constexpr std::size_t clamp_sides = 2;     // below the first point, above the last
constexpr std::string_view problem_file = "problem.h5";

std::string shell_quote(const std::string& text) {
    std::string quoted{"'"};
    for (const char c : text) {
        if (c == '\'') {
            quoted += R"('\'')";
        } else {
            quoted += c;
        }
    }
    quoted += '\'';
    return quoted;
}

std::string read_text(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void write_text(const fs::path& file, const std::string& text) {
    std::ofstream out(file, std::ios::binary);
    out << text;
}

bool is_sha256(const std::string& text) {
    if (text.size() != 64) {
        return false;
    }
    return std::ranges::all_of(
        text, [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

// True when `recorded` names the file `expected`, after resolving symbolic links.
bool same_location(const std::string& recorded, const fs::path& expected) {
    std::error_code ignored;
    return fs::weakly_canonical(fs::path(recorded), ignored) ==
           fs::weakly_canonical(expected, ignored);
}

// Variable-length UTF-8, the string type of the output contract; HDF5 refuses to convert an
// ASCII string to it, so a read also checks the character set.
H5::StrType utf8_string_type() {
    const H5::StrType type(H5::PredType::C_S1, H5T_VARIABLE);
    type.setCset(H5T_CSET_UTF8);
    return type;
}

std::int64_t int_attribute(const H5::H5Object& object, const std::string& name) {
    const H5::Attribute attribute = object.openAttribute(name);
    std::int64_t value = 0;
    attribute.read(H5::PredType::NATIVE_INT64, &value);
    return value;
}

std::string string_attribute(const H5::H5Object& object, const std::string& name) {
    const H5::Attribute attribute = object.openAttribute(name);
    const H5::StrType type = utf8_string_type();
    char* text = nullptr;
    attribute.read(type, static_cast<void*>(&text));
    std::string value = text == nullptr ? std::string{} : std::string(text);
    H5free_memory(text);
    return value;
}

std::size_t element_count(const H5::DataSet& dataset) {
    return static_cast<std::size_t>(dataset.getSpace().getSimpleExtentNpoints());
}

std::vector<double> doubles(const H5::Group& group, const std::string& name) {
    const H5::DataSet dataset = group.openDataSet(name);
    std::vector<double> values(element_count(dataset));
    dataset.read(values.data(), H5::PredType::NATIVE_DOUBLE);
    return values;
}

std::vector<std::int64_t> integers(const H5::Group& group, const std::string& name) {
    const H5::DataSet dataset = group.openDataSet(name);
    std::vector<std::int64_t> values(element_count(dataset));
    dataset.read(values.data(), H5::PredType::NATIVE_INT64);
    return values;
}

std::vector<std::string> strings(const H5::Group& group, const std::string& name) {
    const H5::DataSet dataset = group.openDataSet(name);
    std::vector<char*> raw(element_count(dataset), nullptr);
    const H5::StrType type = utf8_string_type();
    dataset.read(static_cast<void*>(raw.data()), type);
    std::vector<std::string> values;
    values.reserve(raw.size());
    for (char* text : raw) {
        values.emplace_back(text == nullptr ? "" : text);
        H5free_memory(text);
    }
    return values;
}

double scalar_double(const H5::Group& group, const std::string& name) {
    return doubles(group, name).at(0);
}

std::int64_t scalar_int(const H5::Group& group, const std::string& name) {
    return integers(group, name).at(0);
}

std::string scalar_string(const H5::Group& group, const std::string& name) {
    return strings(group, name).at(0);
}

struct RunResult {
    int status = -1;
    std::string out;
    std::string err;
};

class UrusEndToEnd : public testing::Test {
protected:
    void SetUp() override {
        const std::string test_name = testing::UnitTest::GetInstance()->current_test_info()->name();
        root_ = fs::temp_directory_path() / std::format("nts_urus_{}_{}", getpid(), test_name);
        std::error_code ignored;
        static_cast<void>(fs::remove_all(root_, ignored));
        static_cast<void>(fs::create_directories(root_, ignored));
        library_ = root_ / "library.h5";
        write_fixture(make_valid_fixture(), library_);
    }

    void TearDown() override {
        std::error_code ignored;
        static_cast<void>(fs::remove_all(root_, ignored));
    }

    // Writes case `name` into root_/<name> with tools/inputs/cases.py, against the library.
    // Returns the problem input's path relative to root_.
    std::string write_case(const std::string& name) {
        const std::string command =
            std::format("cd {} && {} -m tools.inputs.cases {} {} --library {}",
                        shell_quote(NTS_SOURCE_DIR), shell_quote(NTS_PYTHON_EXECUTABLE), name,
                        shell_quote((root_ / name).string()), shell_quote(library_.string()));
        const RunResult written = run_shell(command, "write_" + name);
        EXPECT_EQ(written.status, status_ok) << written.err;
        return name + "/" + std::string(problem_file);
    }

    // Runs urus from root_, the working directory, with `arguments` as given.
    RunResult run_urus(const std::string& arguments, const std::string& tag) {
        const std::string command = std::format("cd {} && {} {}", shell_quote(root_.string()),
                                                shell_quote(NTS_URUS_EXECUTABLE), arguments);
        return run_shell(command, tag);
    }

    // Runs a shell command with its stdout and stderr captured in root_/<tag>.{stdout,stderr}.
    RunResult run_shell(const std::string& command, const std::string& tag) {
        const fs::path out = root_ / (tag + ".stdout");
        const fs::path err = root_ / (tag + ".stderr");
        const std::string full = std::format("{} > {} 2> {}", command, shell_quote(out.string()),
                                             shell_quote(err.string()));
        // The test drives the command line as a user would; every path in `full` is shell-quoted
        // and comes from the test itself.
        // NOLINTNEXTLINE(bugprone-command-processor): see above.
        const int wait_status = std::system(full.c_str());
        RunResult result;
        // POSIX defines WIFEXITED and WEXITSTATUS in <stdlib.h>, reached through <cstdlib>.
        // NOLINTNEXTLINE(misc-include-cleaner): see above.
        result.status = WIFEXITED(wait_status) ? WEXITSTATUS(wait_status) : -1;
        result.out = read_text(out);
        result.err = read_text(err);
        return result;
    }

    std::string sha256_of(const fs::path& file) {
        const RunResult result =
            run_shell(std::format("{} {}", shell_quote(NTS_SHA256SUM), shell_quote(file.string())),
                      "sha256_" + file.filename().string());
        EXPECT_EQ(result.status, status_ok) << result.err;
        return result.out.substr(0, 64);
    }

    // The solution members of docs/contracts/output.md §3-§5 for the 3 x 3 x 2 grid.
    static void expect_solution(const H5::Group& root) {
        EXPECT_EQ(int_attribute(root, "format_version"), 2);
        const double k_eff = scalar_double(root.openGroup("eigenvalue"), "k_eff");
        EXPECT_TRUE(std::isfinite(k_eff));
        EXPECT_GT(k_eff, 0.0);

        const H5::Group fields = root.openGroup("fields");
        const H5::DataSet flux = fields.openDataSet("flux");
        const std::vector<double> fluxes = doubles(fields, "flux");
        ASSERT_EQ(fluxes.size(), 2 * nodes);
        EXPECT_EQ(string_attribute(flux, "units"), "cm^-2 s^-1");
        for (const double value : fluxes) {
            EXPECT_TRUE(std::isfinite(value));
            EXPECT_GT(value, 0.0);
        }

        const std::vector<double> power = doubles(fields, "power");
        const std::vector<double> density = doubles(fields, "power_density");
        ASSERT_EQ(power.size(), nodes);
        ASSERT_EQ(density.size(), nodes);
        EXPECT_EQ(string_attribute(fields.openDataSet("power"), "units"), "W");
        EXPECT_EQ(string_attribute(fields.openDataSet("power_density"), "units"), "W cm^-3");
        const double total_power = std::accumulate(power.begin(), power.end(), 0.0);
        EXPECT_NEAR(total_power, thermal_power_w, 1e-10 * thermal_power_w);
        for (std::size_t k = 0; k < nz; ++k) {
            for (std::size_t j = 0; j < ny; ++j) {
                for (std::size_t i = 0; i < nx; ++i) {
                    const std::size_t node = (((k * ny) + j) * nx) + i;
                    const double volume_cm3 = width_x_cm[i] * width_y_cm[j] * width_z_cm[k];
                    // The cases put the fission-free reflector at [z 1][y 2][x 2].
                    if (k == 1 && j == 2 && i == 2) {
                        EXPECT_EQ(power[node], 0.0);
                    } else {
                        EXPECT_GT(power[node], 0.0);
                    }
                    EXPECT_NEAR(density[node], power[node] / volume_cm3,
                                1e-12 * (power[node] / volume_cm3));
                }
            }
        }
    }

    // Provenance of docs/contracts/output.md §6. `documents` holds the file expected for each
    // input document, empty for an absent one.
    void expect_provenance(const H5::Group& root, const fs::path& input,
                           const std::array<fs::path, input_documents>& documents) {
        const H5::Group provenance = root.openGroup("provenance");
        const std::string recorded_input = scalar_string(provenance, "problem_input");
        EXPECT_TRUE(fs::path(recorded_input).is_absolute());
        EXPECT_TRUE(same_location(recorded_input, input));
        const std::string input_sha = scalar_string(provenance, "problem_input_sha256");
        EXPECT_TRUE(is_sha256(input_sha));
        EXPECT_EQ(input_sha, sha256_of(input));

        const std::vector<std::string> files = strings(provenance, "document_files");
        const std::vector<std::string> hashes = strings(provenance, "document_sha256");
        ASSERT_EQ(files.size(), input_documents);
        ASSERT_EQ(hashes.size(), input_documents);
        for (std::size_t d = 0; d < input_documents; ++d) {
            if (documents[d].empty()) {
                EXPECT_TRUE(files[d].empty());
                EXPECT_TRUE(hashes[d].empty());
                continue;
            }
            EXPECT_TRUE(fs::path(files[d]).is_absolute());
            EXPECT_TRUE(same_location(files[d], documents[d]));
            EXPECT_EQ(hashes[d], sha256_of(documents[d]));
        }

        const std::string recorded_library = scalar_string(provenance, "library");
        EXPECT_TRUE(fs::path(recorded_library).is_absolute());
        EXPECT_TRUE(same_location(recorded_library, library_));
        EXPECT_EQ(scalar_string(provenance, "library_sha256"), sha256_of(library_));
        EXPECT_FALSE(scalar_string(provenance, "engine_version").empty());
        EXPECT_FALSE(scalar_string(provenance, "engine_commit").empty());
        EXPECT_FALSE(scalar_string(provenance, "build_preset").empty());
        EXPECT_FALSE(scalar_string(provenance, "build_type").empty());
        const H5::DataSet wall_time = provenance.openDataSet("wall_time");
        EXPECT_EQ(string_attribute(wall_time, "units"), "s");
        EXPECT_GT(scalar_double(provenance, "wall_time"), 0.0);
    }

    // Exit status 2 writes nothing: a previous output stays as it was. The diagnostic must
    // start with the prefix from diagnostic_prefix (the file and location check_solvable names,
    // problem input contract §9).
    void expect_rejected(const std::string& case_name, const std::string& diagnostic) {
        const std::string input = write_case(case_name);
        const std::string output_name = case_name + ".h5";
        const std::string previous = "previous output, kept as it was\n";
        write_text(root_ / output_name, previous);

        const RunResult run = run_urus(
            std::format("{} -o {}", shell_quote(input), shell_quote(output_name)), case_name);
        EXPECT_EQ(run.status, status_rejected) << run.err;
        EXPECT_NE(run.err.find(diagnostic), std::string::npos) << run.err;
        EXPECT_EQ(run.out.find("k_eff = "), std::string::npos);
        EXPECT_EQ(read_text(root_ / output_name), previous);
    }

    // The diagnostic prefix urus prints for `location` in `file`. Diagnostics name files as urus
    // was given them, so `file` is relative to root_, where urus runs.
    static std::string diagnostic_prefix(const fs::path& file, const std::string& location) {
        return std::format("{}: {}:", file.string(), location);
    }

    // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes): TEST_F bodies are subclasses
    fs::path root_;
    // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes): TEST_F bodies are subclasses
    fs::path library_;
};

TEST_F(UrusEndToEnd, ValidCaseWritesASolutionAndAValidRecord) {
    // The input is in a subdirectory and urus runs from the root: the working directory
    // differs from the input's directory.
    const std::string input = write_case("valid");
    const RunResult run = run_urus(std::format("{} -o valid.h5", shell_quote(input)), "valid");
    ASSERT_EQ(run.status, status_ok) << run.err;
    EXPECT_NE(run.out.find("k_eff = "), std::string::npos);
    ASSERT_TRUE(fs::exists(root_ / "valid.h5"));

    const H5::H5File file((root_ / "valid.h5").string(), H5F_ACC_RDONLY);
    const H5::Group root = file.openGroup("/");
    expect_solution(root);

    const H5::Group record = root.openGroup("run_record");
    EXPECT_EQ(int_attribute(record, "valid"), 1);
    EXPECT_EQ(int_attribute(record, "exit_status"), status_ok);
    EXPECT_EQ(string_attribute(record, "reason"), "converged");

    const H5::Group convergence = record.openGroup("convergence");
    EXPECT_EQ(scalar_int(convergence, "converged"), 1);
    EXPECT_GT(scalar_int(convergence, "outer_iterations"), 0);
    EXPECT_GT(scalar_int(convergence, "inner_iterations"), 0);
    EXPECT_EQ(scalar_int(convergence, "nodal_updates"), 0);
    for (const std::string name :
         {"eigenvalue_change", "fission_source_l2_change", "fission_source_linf_change", "residual",
          "coupling_correction_change"}) {
        EXPECT_TRUE(std::isfinite(scalar_double(convergence, name))) << name;
        EXPECT_GE(scalar_double(convergence, name), 0.0) << name;
    }
    EXPECT_EQ(scalar_double(convergence, "coupling_correction_change"), 0.0);

    const std::vector<std::int64_t> counts = integers(record.openGroup("table_clamps"), "counts");
    ASSERT_EQ(counts.size(), clamp_axes * clamp_sides);
    for (const std::int64_t count : counts) {
        EXPECT_EQ(count, 0);
    }

    const H5::Group options = record.openGroup("options");
    EXPECT_EQ(scalar_string(options, "kernel"), "finite_difference");
    EXPECT_EQ(scalar_int(options, "adjoint"), 0);
    EXPECT_EQ(scalar_int(options, "allow_table_clamps"), 0);
    EXPECT_EQ(scalar_int(options, "outer_iteration_limit"), 1000);
    EXPECT_EQ(scalar_int(options, "nodal_update_period"), 5);
    EXPECT_DOUBLE_EQ(scalar_double(options, "eigenvalue_tolerance"), 5e-6);
    EXPECT_DOUBLE_EQ(scalar_double(options, "fission_source_l2_tolerance"), 5e-5);
    EXPECT_DOUBLE_EQ(scalar_double(options, "fission_source_linf_tolerance"), 5e-4);

    const H5::Group methods = record.openGroup("methods");
    EXPECT_EQ(scalar_string(methods, "kernel"), "finite_difference");
    EXPECT_EQ(scalar_string(methods, "eigenvalue_iteration"),
              "Wielandt-shifted inverse iteration (TM Eqs. (4.8)-(4.9))");
    EXPECT_EQ(scalar_string(methods, "linear_solver"), "BiCGSTAB (Eigen)");
    EXPECT_EQ(scalar_string(methods, "preconditioner"),
              "incomplete LU with dual threshold (Eigen IncompleteLUT)");

    // All three documents are written in place: each is the problem input itself.
    expect_provenance(record, root_ / input,
                      {root_ / input, root_ / input, fs::path{}, root_ / input});
}

TEST_F(UrusEndToEnd, LinkedCaseReadsItsDocumentFilesAndRecordsThem) {
    const std::string input = write_case("linked");
    const RunResult run = run_urus(std::format("{} -o linked.h5", shell_quote(input)), "linked");
    ASSERT_EQ(run.status, status_ok) << run.err;
    ASSERT_TRUE(fs::exists(root_ / "linked.h5"));

    const H5::H5File file((root_ / "linked.h5").string(), H5F_ACC_RDONLY);
    const H5::Group root = file.openGroup("/");
    expect_solution(root);
    EXPECT_EQ(int_attribute(root.openGroup("run_record"), "valid"), 1);

    const fs::path directory = root_ / "linked";
    expect_provenance(
        root.openGroup("run_record"), root_ / input,
        {root_ / input, directory / "initial_state.h5", fs::path{}, directory / "settings.h5"});
}

TEST_F(UrusEndToEnd, DefaultKernelIsRejectedWithExitStatus2AndNoOutput) {
    expect_rejected("default_kernel", diagnostic_prefix(fs::path{"default_kernel"} / problem_file,
                                                        "/settings/kernel"));
}

TEST_F(UrusEndToEnd, AdjointIsRejectedWithExitStatus2AndNoOutput) {
    expect_rejected("adjoint",
                    diagnostic_prefix(fs::path{"adjoint"} / problem_file, "/settings/adjoint"));
}

TEST_F(UrusEndToEnd, LinkedAdjointIsRejectedNamingTheLinkedSettingsFile) {
    expect_rejected("linked_adjoint",
                    diagnostic_prefix(fs::path{"linked_adjoint"} / "settings.h5", "/adjoint"));
}

TEST_F(UrusEndToEnd, WielandtShiftThatCannotMoveKIsRejectedWithExitStatus2AndNoOutput) {
    expect_rejected("wielandt_shift", diagnostic_prefix(fs::path{"wielandt_shift"} / problem_file,
                                                        "/settings/wielandt_shift"));
}

TEST_F(UrusEndToEnd, InvalidInputIsRejectedWithExitStatus2AndNoOutput) {
    expect_rejected("invalid", "thermal_power");
}

TEST_F(UrusEndToEnd, NotConvergedWritesTheOutputWithAnInvalidRecordAndExitsWith3) {
    const std::string input = write_case("not_converged");
    const RunResult run =
        run_urus(std::format("{} -o not_converged.h5", shell_quote(input)), "not_converged");
    ASSERT_EQ(run.status, status_not_converged) << run.err;
    EXPECT_NE(run.out.find("k_eff = "), std::string::npos);
    ASSERT_TRUE(fs::exists(root_ / "not_converged.h5"));

    const H5::H5File file((root_ / "not_converged.h5").string(), H5F_ACC_RDONLY);
    const H5::Group root = file.openGroup("/");
    expect_solution(root);

    const H5::Group record = root.openGroup("run_record");
    EXPECT_EQ(int_attribute(record, "valid"), 0);
    EXPECT_EQ(int_attribute(record, "exit_status"), status_not_converged);
    EXPECT_EQ(string_attribute(record, "reason"), "not converged: outer iteration limit 1 reached");
    const H5::Group convergence = record.openGroup("convergence");
    EXPECT_EQ(scalar_int(convergence, "converged"), 0);
    EXPECT_EQ(scalar_int(convergence, "outer_iterations"), 1);
    EXPECT_GT(scalar_int(convergence, "inner_iterations"), 0);
}

TEST_F(UrusEndToEnd, ClampedStateWritesTheOutputWithCountsAndExitsWith4) {
    const std::string input = write_case("clamped");
    const RunResult run = run_urus(std::format("{} -o clamped.h5", shell_quote(input)), "clamped");
    ASSERT_EQ(run.status, status_clamped) << run.err;
    ASSERT_TRUE(fs::exists(root_ / "clamped.h5"));

    const H5::H5File file((root_ / "clamped.h5").string(), H5F_ACC_RDONLY);
    const H5::Group root = file.openGroup("/");
    expect_solution(root);

    const H5::Group record = root.openGroup("run_record");
    EXPECT_EQ(int_attribute(record, "valid"), 0);
    EXPECT_EQ(int_attribute(record, "exit_status"), status_clamped);
    EXPECT_EQ(string_attribute(record, "reason"),
              "converged with table clamps: 1 node-axis clamps");
    EXPECT_EQ(scalar_int(record.openGroup("convergence"), "converged"), 1);

    // One node (x 0, y 0, z 0) above the last fuel_temperature point: row 0, column 1.
    const std::vector<std::int64_t> counts = integers(record.openGroup("table_clamps"), "counts");
    ASSERT_EQ(counts.size(), clamp_axes * clamp_sides);
    for (std::size_t axis = 0; axis < clamp_axes; ++axis) {
        for (std::size_t side = 0; side < clamp_sides; ++side) {
            const std::int64_t expected = (axis == 0 && side == 1) ? 1 : 0;
            EXPECT_EQ(counts[(axis * clamp_sides) + side], expected)
                << "axis " << axis << " side " << side;
        }
    }
    EXPECT_EQ(scalar_int(record.openGroup("options"), "allow_table_clamps"), 1);
}

TEST_F(UrusEndToEnd, NoArgumentsIsAUsageErrorWithExitStatus1) {
    const RunResult run = run_urus("", "no_arguments");
    EXPECT_EQ(run.status, status_usage_or_write) << run.err;
    EXPECT_FALSE(run.err.empty());
    EXPECT_TRUE(run.out.empty());
}

TEST_F(UrusEndToEnd, OutputInANonExistentDirectoryIsAWriteErrorWithExitStatus1) {
    const std::string input = write_case("valid");
    const RunResult run = run_urus(
        std::format("{} -o {}", shell_quote(input), shell_quote("missing/out.h5")), "missing_dir");
    EXPECT_EQ(run.status, status_usage_or_write) << run.err;
    EXPECT_FALSE(fs::exists(root_ / "missing"));
}

TEST_F(UrusEndToEnd, MalformedCommandLinesAreUsageErrorsWithExitStatus1AndNoOutput) {
    const std::string quoted_input = shell_quote(write_case("valid"));
    const std::array<std::string, 3> command_lines{
        quoted_input + " -x out.h5",                // unknown flag
        quoted_input + " -o",                       // missing output
        quoted_input + " -o out.h5 extra_argument", // extra argument
    };
    for (std::size_t n = 0; n < command_lines.size(); ++n) {
        SCOPED_TRACE(command_lines[n]);
        const RunResult run = run_urus(command_lines[n], std::format("usage_{}", n));
        EXPECT_EQ(run.status, status_usage_or_write) << run.err;
        EXPECT_FALSE(fs::exists(root_ / "out.h5"));
    }
}

} // namespace
} // namespace nts::test
