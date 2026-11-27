// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Aurora Jahan
// Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

#include "nts/cmfd/cmfd_system.hpp"
#include "nts/common/build_info.hpp"
#include "nts/io/nodal_library.hpp"
#include "nts/io/output.hpp"
#include "nts/io/problem.hpp"
#include "nts/solve/eigenvalue.hpp"
#include "nts/solve/steady_state.hpp"
#include "nts/xs/cross_section_model.hpp"

#include <gtest/gtest.h>

#include <H5Cpp.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core_fixture.hpp"

namespace {

// Test reactor: 3 x 2 x 2 positions, position 11 = [z=1][y=1][x=2] excluded, 11 nodes.
constexpr std::size_t excluded_position = 11;
constexpr std::size_t position_count = 12;

// A fresh directory named after the running test, empty on return.
std::filesystem::path test_directory() {
    const ::testing::TestInfo* info = ::testing::UnitTest::GetInstance()->current_test_info();
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / "urus_output_test" /
        std::format("{}.{}", info->test_suite_name(), info->name());
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    return directory;
}

nts::ReactorDescription test_reactor() {
    std::array<nts::BoundaryCondition, nts::face_count> faces{};
    faces.fill(nts::test::boundary(nts::BoundaryKind::reflective));
    return nts::test::make_reactor({1.0, 1.0, 1.0}, {1.0, 1.0}, {1.0, 1.0}, faces, {},
                                   {excluded_position});
}

nts::ConvergenceState converged_state() {
    nts::ConvergenceState convergence;
    convergence.converged = true;
    convergence.outer_iterations = 12;
    convergence.inner_iterations = 34;
    convergence.nodal_updates = 2;
    convergence.eigenvalue_change = 2.5e-7;
    convergence.fission_source_l2_change = 3.0e-6;
    convergence.fission_source_linf_change = 4.0e-5;
    convergence.residual = 5.0e-9;
    convergence.coupling_correction_change = 0.0;
    return convergence;
}

nts::SteadyState test_state(const nts::ReactorDescription& reactor) {
    nts::SteadyState state;
    state.k_eff = 1.2345678901;
    const std::size_t nodes = reactor.node_count();
    state.flux.assign(nts::energy_group_count * nodes, 0.0);
    state.power_w.assign(nodes, 0.0);
    state.volume_cm3.assign(nodes, 0.0);
    for (std::size_t node = 0; node < nodes; ++node) {
        const auto index = static_cast<double>(node);
        state.power_w[node] = 10.0 + index;
        state.volume_cm3[node] = 2.0 + index;
        for (std::size_t group = 0; group < nts::energy_group_count; ++group) {
            state.flux[nts::unknown_index(node, group)] =
                (100.0 * static_cast<double>(group + 1)) + index;
        }
    }
    state.convergence = converged_state();
    return state;
}

nts::RunRecord test_record(const nts::SteadyState& state, const nts::ClampCounts& clamp_counts) {
    nts::RunRecord record;
    record.options = nts::test::verification_options();
    record.allow_table_clamps = false;
    record.convergence = state.convergence;
    record.clamp_counts = clamp_counts;
    record.classification =
        nts::classify_run(state.convergence, record.options.outer_iteration_limit, clamp_counts);
    record.problem_input = {.file = "/cases/problem.yaml", .sha256 = std::string(64, 'b')};
    for (std::size_t document = 0; document < nts::input_document_count; ++document) {
        record.documents[document] = {.file =
                                          "/cases/document_" + std::to_string(document) + ".yaml",
                                      .sha256 = std::string(64, static_cast<char>('c' + document))};
    }
    record.library = {.file = "/cases/library.h5", .sha256 = std::string(64, 'd')};
    record.build = nts::build_info();
    record.wall_time_s = 1.5;
    return record;
}

std::int64_t int_attribute(const H5::H5Object& object, const char* name) {
    std::int64_t value = 0;
    object.openAttribute(name).read(H5::PredType::NATIVE_INT64, &value);
    return value;
}

std::string string_attribute(const H5::H5Object& object, const char* name) {
    const H5::Attribute attribute = object.openAttribute(name);
    H5std_string value;
    attribute.read(attribute.getDataType(), value);
    return value;
}

std::int64_t int_dataset(const H5::Group& group, const char* name) {
    std::int64_t value = 0;
    group.openDataSet(name).read(&value, H5::PredType::NATIVE_INT64);
    return value;
}

double double_dataset(const H5::Group& group, const char* name) {
    double value = 0.0;
    group.openDataSet(name).read(&value, H5::PredType::NATIVE_DOUBLE);
    return value;
}

std::string string_dataset(const H5::Group& group, const char* name) {
    const H5::DataSet dataset = group.openDataSet(name);
    H5std_string value;
    dataset.read(value, dataset.getDataType());
    return value;
}

std::vector<std::string> string_array(const H5::Group& group, const char* name, std::size_t count) {
    const H5::DataSet dataset = group.openDataSet(name);
    const H5::StrType type(H5::PredType::C_S1, H5T_VARIABLE);
    type.setCset(H5T_CSET_UTF8); // the contract's character set; HDF5 will not convert ASCII
    std::vector<char*> pointers(count, nullptr);
    dataset.read(static_cast<void*>(pointers.data()), type);
    std::vector<std::string> values;
    values.reserve(count);
    for (const char* pointer : pointers) {
        values.emplace_back(pointer);
    }
    H5Dvlen_reclaim(type.getId(), dataset.getSpace().getId(), H5P_DEFAULT,
                    static_cast<void*>(pointers.data()));
    return values;
}

std::vector<double> double_array(const H5::Group& group, const char* name, std::size_t count) {
    std::vector<double> values(count, 0.0);
    group.openDataSet(name).read(values.data(), H5::PredType::NATIVE_DOUBLE);
    return values;
}

std::vector<std::int64_t> int_array(const H5::Group& group, const char* name, std::size_t count) {
    std::vector<std::int64_t> values(count, 0);
    group.openDataSet(name).read(values.data(), H5::PredType::NATIVE_INT64);
    return values;
}

std::vector<hsize_t> dims_of_space(const H5::DataSpace& space) {
    const int rank = space.getSimpleExtentNdims();
    std::vector<hsize_t> dims(static_cast<std::size_t>(rank));
    if (rank > 0) {
        space.getSimpleExtentDims(dims.data());
    }
    return dims;
}

std::vector<hsize_t> dims_of(const H5::DataSet& dataset) {
    return dims_of_space(dataset.getSpace());
}

// One member of the file: a group, a dataset or an attribute. A `units` attribute is not a
// member of its own; it is the `units` of the object it belongs to.
struct Member {
    std::string kind;
    std::string type; // "int64", "float64", "utf8", "other"; empty for a group
    std::vector<hsize_t> dims;
    std::optional<std::string> units;
};
using Members = std::map<std::string, Member>;

std::string child_path(const std::string& parent, const std::string& name) {
    return parent == "/" ? "/" + name : parent + "/" + name;
}

std::string type_label(const H5::DataType& type) {
    const H5T_class_t klass = type.getClass();
    if (klass == H5T_INTEGER && type.getSize() == 8 && H5Tget_sign(type.getId()) == H5T_SGN_2) {
        return "int64";
    }
    if (klass == H5T_FLOAT && type.getSize() == 8) {
        return "float64";
    }
    if (klass == H5T_STRING && H5Tis_variable_str(type.getId()) > 0 &&
        H5Tget_cset(type.getId()) == H5T_CSET_UTF8) {
        return "utf8";
    }
    return "other";
}

// Records the attributes of `object` (path `path`): `units` into `owner`, others as members.
void add_attributes(const H5::H5Object& object, const std::string& path, Member& owner,
                    Members& members) {
    const int count = object.getNumAttrs();
    for (int index = 0; index < count; ++index) {
        const H5::Attribute attribute = object.openAttribute(static_cast<unsigned int>(index));
        const std::string name = attribute.getName();
        if (name == "units") {
            owner.units = string_attribute(object, "units");
            continue;
        }
        Member member;
        member.kind = "attribute";
        member.type = type_label(attribute.getDataType());
        member.dims = dims_of_space(attribute.getSpace());
        members.emplace(std::format("{}@{}", path, name), std::move(member));
    }
}

void walk_group(const H5::Group& group, const std::string& path, Members& members);

void walk_dataset(const H5::DataSet& dataset, const std::string& path, Members& members) {
    Member member;
    member.kind = "dataset";
    member.type = type_label(dataset.getDataType());
    member.dims = dims_of(dataset);
    add_attributes(dataset, path, member, members);
    members.emplace(path, std::move(member));
}

// NOLINTNEXTLINE(misc-no-recursion): the HDF5 group tree is walked by recursion; its depth is 2.
void walk_group(const H5::Group& group, const std::string& path, Members& members) {
    Member member;
    member.kind = "group";
    add_attributes(group, path, member, members);
    if (path != "/") {
        members.emplace(path, std::move(member));
    }
    for (hsize_t index = 0; index < group.getNumObjs(); ++index) {
        const std::string name = group.getObjnameByIdx(index);
        const std::string child = child_path(path, name);
        const H5G_obj_t type = group.getObjTypeByIdx(index);
        if (type == H5G_GROUP) {
            walk_group(group.openGroup(name), child, members);
        } else if (type == H5G_DATASET) {
            walk_dataset(group.openDataSet(name), child, members);
        } else {
            Member other;
            other.kind = "other";
            members.emplace(child, std::move(other));
        }
    }
}

void add_member(Members& table, std::string path, std::string kind, std::string type,
                std::vector<hsize_t> dims = {}, std::optional<std::string> units = std::nullopt) {
    table.emplace(std::move(path), Member{.kind = std::move(kind),
                                          .type = std::move(type),
                                          .dims = std::move(dims),
                                          .units = std::move(units)});
}

// Output contract §3-§6, format version 2: every member, both ways. Written for the test
// reactor (nz = 2, ny = 2, nx = 3).
Members contract_members() {
    Members table;
    add_member(table, "/@format_version", "attribute", "int64");

    add_member(table, "/eigenvalue", "group", "");
    add_member(table, "/eigenvalue/k_eff", "dataset", "float64", {}, "1");

    add_member(table, "/fields", "group", "");
    add_member(table, "/fields/flux", "dataset", "float64", {2, 2, 2, 3}, "cm^-2 s^-1");
    add_member(table, "/fields/power", "dataset", "float64", {2, 2, 3}, "W");
    add_member(table, "/fields/power_density", "dataset", "float64", {2, 2, 3}, "W cm^-3");

    add_member(table, "/run_record", "group", "");
    add_member(table, "/run_record@valid", "attribute", "int64");
    add_member(table, "/run_record@exit_status", "attribute", "int64");
    add_member(table, "/run_record@reason", "attribute", "utf8");

    add_member(table, "/run_record/convergence", "group", "");
    add_member(table, "/run_record/convergence/converged", "dataset", "int64");
    add_member(table, "/run_record/convergence/outer_iterations", "dataset", "int64");
    add_member(table, "/run_record/convergence/inner_iterations", "dataset", "int64");
    add_member(table, "/run_record/convergence/nodal_updates", "dataset", "int64");
    add_member(table, "/run_record/convergence/eigenvalue_change", "dataset", "float64", {}, "1");
    add_member(table, "/run_record/convergence/fission_source_l2_change", "dataset", "float64", {},
               "1");
    add_member(table, "/run_record/convergence/fission_source_linf_change", "dataset", "float64",
               {}, "1");
    add_member(table, "/run_record/convergence/residual", "dataset", "float64", {}, "1");
    add_member(table, "/run_record/convergence/coupling_correction_change", "dataset", "float64",
               {}, "1");

    add_member(table, "/run_record/table_clamps", "group", "");
    add_member(table, "/run_record/table_clamps/counts", "dataset", "int64", {4, 2});

    // Settings: no units.
    add_member(table, "/run_record/options", "group", "");
    add_member(table, "/run_record/options/kernel", "dataset", "utf8");
    add_member(table, "/run_record/options/kernel_switch_threshold", "dataset", "float64");
    add_member(table, "/run_record/options/wielandt_shift", "dataset", "float64");
    add_member(table, "/run_record/options/eigenvalue_tolerance", "dataset", "float64");
    add_member(table, "/run_record/options/fission_source_l2_tolerance", "dataset", "float64");
    add_member(table, "/run_record/options/fission_source_linf_tolerance", "dataset", "float64");
    add_member(table, "/run_record/options/inner_reduction", "dataset", "float64");
    add_member(table, "/run_record/options/inner_iteration_limit", "dataset", "int64");
    add_member(table, "/run_record/options/outer_iteration_limit", "dataset", "int64");
    add_member(table, "/run_record/options/nodal_update_period", "dataset", "int64");
    add_member(table, "/run_record/options/coupling_relaxation", "dataset", "float64");
    add_member(table, "/run_record/options/adjoint", "dataset", "int64");
    add_member(table, "/run_record/options/allow_table_clamps", "dataset", "int64");

    add_member(table, "/run_record/methods", "group", "");
    add_member(table, "/run_record/methods/kernel", "dataset", "utf8");
    add_member(table, "/run_record/methods/eigenvalue_iteration", "dataset", "utf8");
    add_member(table, "/run_record/methods/linear_solver", "dataset", "utf8");
    add_member(table, "/run_record/methods/preconditioner", "dataset", "utf8");

    add_member(table, "/run_record/provenance", "group", "");
    add_member(table, "/run_record/provenance/problem_input", "dataset", "utf8");
    add_member(table, "/run_record/provenance/problem_input_sha256", "dataset", "utf8");
    add_member(table, "/run_record/provenance/document_files", "dataset", "utf8", {4});
    add_member(table, "/run_record/provenance/document_sha256", "dataset", "utf8", {4});
    add_member(table, "/run_record/provenance/library", "dataset", "utf8");
    add_member(table, "/run_record/provenance/library_sha256", "dataset", "utf8");
    add_member(table, "/run_record/provenance/engine_version", "dataset", "utf8");
    add_member(table, "/run_record/provenance/engine_commit", "dataset", "utf8");
    add_member(table, "/run_record/provenance/build_preset", "dataset", "utf8");
    add_member(table, "/run_record/provenance/build_type", "dataset", "utf8");
    add_member(table, "/run_record/provenance/wall_time", "dataset", "float64", {}, "s");
    return table;
}

} // namespace

TEST(FieldOutput, ScattersNodeValuesToPositionsAndLeavesExcludedNaN) {
    const nts::ReactorDescription reactor = test_reactor();
    const nts::SteadyState state = test_state(reactor);
    const std::filesystem::path file = test_directory() / "output.h5";
    ASSERT_TRUE(nts::write_output(file, reactor, state, test_record(state, {})).has_value());

    const H5::H5File h5(file.string(), H5F_ACC_RDONLY);
    const H5::Group fields = h5.openGroup("fields");
    const std::vector<double> flux =
        double_array(fields, "flux", nts::energy_group_count * position_count);
    const std::vector<double> power = double_array(fields, "power", position_count);
    const std::vector<double> density = double_array(fields, "power_density", position_count);

    for (std::size_t node = 0; node < reactor.node_count(); ++node) {
        const std::size_t position = reactor.node_positions[node];
        for (std::size_t group = 0; group < nts::energy_group_count; ++group) {
            EXPECT_EQ(flux[(group * position_count) + position],
                      state.flux[nts::unknown_index(node, group)]);
        }
        EXPECT_EQ(power[position], state.power_w[node]);
        EXPECT_DOUBLE_EQ(density[position], state.power_w[node] / state.volume_cm3[node]);
    }

    EXPECT_TRUE(std::isnan(flux[excluded_position]));
    EXPECT_TRUE(std::isnan(flux[position_count + excluded_position]));
    EXPECT_TRUE(std::isnan(power[excluded_position]));
    EXPECT_TRUE(std::isnan(density[excluded_position]));
}

TEST(FieldOutput, WritesFieldDimensionsAndUnits) {
    const nts::ReactorDescription reactor = test_reactor();
    const nts::SteadyState state = test_state(reactor);
    const std::filesystem::path file = test_directory() / "output.h5";
    ASSERT_TRUE(nts::write_output(file, reactor, state, test_record(state, {})).has_value());

    const H5::H5File h5(file.string(), H5F_ACC_RDONLY);
    const H5::Group fields = h5.openGroup("fields");
    const H5::DataSet flux = fields.openDataSet("flux");
    const std::vector<hsize_t> flux_dims = dims_of(flux);
    const std::vector<hsize_t> expected_flux_dims{2, 2, 2, 3};
    EXPECT_EQ(flux_dims, expected_flux_dims);
    EXPECT_EQ(string_attribute(flux, "units"), "cm^-2 s^-1");
    EXPECT_EQ(string_attribute(fields.openDataSet("power"), "units"), "W");
    EXPECT_EQ(string_attribute(fields.openDataSet("power_density"), "units"), "W cm^-3");
}

TEST(FieldOutput, WritesEigenvalueAndFormatVersion) {
    const nts::ReactorDescription reactor = test_reactor();
    const nts::SteadyState state = test_state(reactor);
    const std::filesystem::path file = test_directory() / "output.h5";
    ASSERT_TRUE(nts::write_output(file, reactor, state, test_record(state, {})).has_value());

    const H5::H5File h5(file.string(), H5F_ACC_RDONLY);
    EXPECT_EQ(int_attribute(h5.openGroup("/"), "format_version"), 2);
    const H5::Group eigenvalue = h5.openGroup("eigenvalue");
    EXPECT_EQ(double_dataset(eigenvalue, "k_eff"), state.k_eff);
    EXPECT_EQ(string_attribute(eigenvalue.openDataSet("k_eff"), "units"), "1");
}

TEST(RunRecord, FileMembersMatchOutputContract) {
    const nts::ReactorDescription reactor = test_reactor();
    const nts::SteadyState state = test_state(reactor);
    const std::filesystem::path file = test_directory() / "output.h5";
    ASSERT_TRUE(nts::write_output(file, reactor, state, test_record(state, {})).has_value());

    const H5::H5File h5(file.string(), H5F_ACC_RDONLY);
    Members actual;
    walk_group(h5.openGroup("/"), "/", actual);
    const Members expected = contract_members();

    for (const auto& [path, want] : expected) {
        const auto found = actual.find(path);
        if (found == actual.end()) {
            ADD_FAILURE() << "missing member " << path;
            continue;
        }
        const Member& got = found->second;
        EXPECT_EQ(got.kind, want.kind) << path;
        EXPECT_EQ(got.type, want.type) << path;
        EXPECT_EQ(got.dims, want.dims) << path;
        EXPECT_EQ(got.units, want.units) << path;
    }
    for (const auto& [path, got] : actual) {
        if (!expected.contains(path)) {
            ADD_FAILURE() << "unexpected member " << path << " (" << got.kind << ")";
        }
    }
}

TEST(RunRecord, WritesConvergenceOptionsMethodsAndProvenance) {
    const nts::ReactorDescription reactor = test_reactor();
    const nts::SteadyState state = test_state(reactor);
    const nts::RunRecord record = test_record(state, {});
    const std::filesystem::path file = test_directory() / "output.h5";
    ASSERT_TRUE(nts::write_output(file, reactor, state, record).has_value());

    const H5::H5File h5(file.string(), H5F_ACC_RDONLY);
    const H5::Group run_record = h5.openGroup("run_record");
    EXPECT_EQ(int_attribute(run_record, "valid"), 1);
    EXPECT_EQ(int_attribute(run_record, "exit_status"), 0);
    EXPECT_EQ(string_attribute(run_record, "reason"), "converged");

    const H5::Group convergence = run_record.openGroup("convergence");
    EXPECT_EQ(int_dataset(convergence, "converged"), 1);
    EXPECT_EQ(int_dataset(convergence, "outer_iterations"), 12);
    EXPECT_EQ(int_dataset(convergence, "inner_iterations"), 34);
    EXPECT_EQ(double_dataset(convergence, "eigenvalue_change"), 2.5e-7);
    EXPECT_EQ(double_dataset(convergence, "residual"), 5.0e-9);

    const H5::Group clamps = run_record.openGroup("table_clamps");
    const std::vector<hsize_t> clamp_dims = dims_of(clamps.openDataSet("counts"));
    const std::vector<hsize_t> expected_clamp_dims{4, 2};
    EXPECT_EQ(clamp_dims, expected_clamp_dims);
    EXPECT_EQ(int_array(clamps, "counts", 8), std::vector<std::int64_t>(8, 0));

    const H5::Group options = run_record.openGroup("options");
    const std::string kernel(nts::kernel_choice_name(record.options.kernel));
    EXPECT_EQ(string_dataset(options, "kernel"), kernel);
    EXPECT_EQ(double_dataset(options, "eigenvalue_tolerance"), record.options.eigenvalue_tolerance);
    EXPECT_EQ(int_dataset(options, "outer_iteration_limit"), record.options.outer_iteration_limit);
    EXPECT_EQ(int_dataset(options, "allow_table_clamps"), 0);

    const H5::Group methods = run_record.openGroup("methods");
    EXPECT_EQ(string_dataset(methods, "kernel"), kernel);
    EXPECT_EQ(string_dataset(methods, "linear_solver"), "BiCGSTAB (Eigen)");

    const H5::Group provenance = run_record.openGroup("provenance");
    EXPECT_EQ(string_dataset(provenance, "problem_input"), "/cases/problem.yaml");
    EXPECT_EQ(string_dataset(provenance, "problem_input_sha256"), std::string(64, 'b'));
    EXPECT_EQ(string_dataset(provenance, "library"), "/cases/library.h5");
    EXPECT_EQ(string_dataset(provenance, "library_sha256"), std::string(64, 'd'));
    EXPECT_EQ(string_dataset(provenance, "engine_version"),
              std::string(record.build.engine_version));
    EXPECT_EQ(string_dataset(provenance, "build_preset"), std::string(record.build.build_preset));
    EXPECT_EQ(double_dataset(provenance, "wall_time"), 1.5);

    const std::vector<std::string> document_files =
        string_array(provenance, "document_files", nts::input_document_count);
    const std::vector<std::string> document_hashes =
        string_array(provenance, "document_sha256", nts::input_document_count);
    for (std::size_t document = 0; document < nts::input_document_count; ++document) {
        EXPECT_EQ(document_files[document], record.documents[document].file.string());
        EXPECT_EQ(document_hashes[document], record.documents[document].sha256);
    }
}

TEST(RunRecord, ClampedRunWritesStatusFourAndClampCounts) {
    const nts::ReactorDescription reactor = test_reactor();
    const nts::SteadyState state = test_state(reactor);
    nts::ClampCounts clamps{};
    clamps[1][0] = 3;
    nts::RunRecord record = test_record(state, clamps);
    record.allow_table_clamps = true;
    const std::filesystem::path file = test_directory() / "output.h5";
    ASSERT_TRUE(nts::write_output(file, reactor, state, record).has_value());

    const H5::H5File h5(file.string(), H5F_ACC_RDONLY);
    const H5::Group run_record = h5.openGroup("run_record");
    EXPECT_EQ(int_attribute(run_record, "valid"), 0);
    EXPECT_EQ(int_attribute(run_record, "exit_status"), 4);
    EXPECT_EQ(string_attribute(run_record, "reason"),
              "converged with table clamps: 3 node-axis clamps");

    const std::vector<std::int64_t> counts =
        int_array(run_record.openGroup("table_clamps"), "counts", 8);
    std::vector<std::int64_t> expected(8, 0);
    expected[2] = 3; // row 1 (moderator_density), column 0 (low side)
    EXPECT_EQ(counts, expected);
    EXPECT_EQ(int_dataset(run_record.openGroup("options"), "allow_table_clamps"), 1);
}

TEST(RunRecord, ClassifiesConvergedCleanRunAsValid) {
    const nts::ConvergenceState convergence = converged_state();
    const nts::RunClassification result = nts::classify_run(convergence, 10000, nts::ClampCounts{});
    EXPECT_EQ(result.exit_status, nts::ExitStatus::valid);
    EXPECT_TRUE(result.valid);
    EXPECT_EQ(result.reason, "converged");
}

TEST(RunRecord, ClassifiesClampedRunAsStatusFour) {
    nts::ClampCounts clamps{};
    clamps[0][1] = 2;
    clamps[3][0] = 1;
    const nts::RunClassification result = nts::classify_run(converged_state(), 10000, clamps);
    EXPECT_EQ(result.exit_status, nts::ExitStatus::clamped);
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(result.reason, "converged with table clamps: 3 node-axis clamps");
}

TEST(RunRecord, ClassifiesUnconvergedRunAsStatusThreeEvenWithClamps) {
    nts::ConvergenceState convergence = converged_state();
    convergence.converged = false;
    nts::ClampCounts clamps{};
    clamps[2][1] = 5;
    const nts::RunClassification result = nts::classify_run(convergence, 500, clamps);
    EXPECT_EQ(result.exit_status, nts::ExitStatus::not_converged);
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(result.reason, "not converged: outer iteration limit 500 reached");
}

TEST(RunRecord, MissingDirectoryIsAnErrorAndCreatesNothing) {
    const nts::ReactorDescription reactor = test_reactor();
    const nts::SteadyState state = test_state(reactor);
    const std::filesystem::path directory = test_directory() / "absent";
    const std::expected<void, std::string> result =
        nts::write_output(directory / "output.h5", reactor, state, test_record(state, {}));
    ASSERT_FALSE(result.has_value());
    EXPECT_NE(result.error().find("output.h5"), std::string::npos) << result.error();
    EXPECT_FALSE(std::filesystem::exists(directory));
}

TEST(RunRecord, FailedRenameLeavesTheExistingDirectoryAndNoTemporary) {
    const nts::ReactorDescription reactor = test_reactor();
    const nts::SteadyState state = test_state(reactor);
    const std::filesystem::path directory = test_directory();
    // The target is a non-empty directory: the rename of the complete temporary onto it fails.
    const std::filesystem::path target = directory / "output.h5";
    std::filesystem::create_directories(target);
    {
        std::ofstream marker(target / "keep.txt");
        marker << "keep";
    }

    const std::expected<void, std::string> result =
        nts::write_output(target, reactor, state, test_record(state, {}));
    ASSERT_FALSE(result.has_value());
    EXPECT_NE(result.error().find("output.h5"), std::string::npos) << result.error();
    EXPECT_TRUE(std::filesystem::is_directory(target));
    std::string text;
    std::ifstream(target / "keep.txt") >> text;
    EXPECT_EQ(text, "keep");

    std::size_t entries = 0;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(directory)) {
        static_cast<void>(entry);
        ++entries;
    }
    EXPECT_EQ(entries, 1U);
}

TEST(RunRecord, OverwriteReplacesTheFileAndLeavesNoTemporary) {
    const nts::ReactorDescription reactor = test_reactor();
    nts::SteadyState state = test_state(reactor);
    const std::filesystem::path directory = test_directory();
    const std::filesystem::path file = directory / "output.h5";
    ASSERT_TRUE(nts::write_output(file, reactor, state, test_record(state, {})).has_value());

    state.k_eff = 0.9876543210;
    ASSERT_TRUE(nts::write_output(file, reactor, state, test_record(state, {})).has_value());

    const H5::H5File h5(file.string(), H5F_ACC_RDONLY);
    EXPECT_EQ(double_dataset(h5.openGroup("eigenvalue"), "k_eff"), 0.9876543210);

    std::size_t entries = 0;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(directory)) {
        static_cast<void>(entry);
        ++entries;
    }
    EXPECT_EQ(entries, 1U);
}
