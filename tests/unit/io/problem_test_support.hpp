// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Aurora Jahan
// Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

#pragma once

// The test fixture and diagnostic matchers shared by the problem input tests
// (docs/contracts/problem_input.md). Each test owns a temporary directory holding the library
// of make_valid_fixture() as `library.h5` and the problem input as `problem.h5`, which binds the
// library by the relative path `library.h5`.

#include "nts/common/diagnostic.hpp"
#include "nts/io/problem.hpp"

#include <gtest/gtest.h>

#include <H5Cpp.h>

#include <algorithm>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unistd.h>
#include <utility>
#include <vector>

#include "library_fixture.hpp"
#include "problem_fixture.hpp"

namespace nts::test {

// Every diagnostic, one per line, for failure messages.
inline std::string all_text(const Diagnostics& diagnostics) {
    std::string text;
    for (const auto& diagnostic : diagnostics) {
        text += to_string(diagnostic) + "\n";
    }
    return text;
}

// Makes `directory` the working directory for as long as the object lives.
class ScopedWorkingDirectory {
public:
    explicit ScopedWorkingDirectory(const std::filesystem::path& directory)
        : previous_(std::filesystem::current_path()) {
        std::filesystem::current_path(directory);
    }
    ~ScopedWorkingDirectory() {
        std::error_code ignored;
        std::filesystem::current_path(previous_, ignored);
    }
    ScopedWorkingDirectory(const ScopedWorkingDirectory&) = delete;
    ScopedWorkingDirectory& operator=(const ScopedWorkingDirectory&) = delete;

private:
    std::filesystem::path previous_;
};

class ProblemTest : public testing::Test {
protected:
    void TearDown() override {
        std::error_code ignored;
        std::filesystem::remove_all(directory_, ignored);
    }

    // The library and the problem input; a test changes them before loading.
    FixtureLibrary& library() { return library_; }
    FixtureGroup& problem() { return problem_; }

    // The documents of the problem input, by member name. Each is created in place with its
    // document attributes when absent; a test edits the document before it links the document.
    FixtureGroup& document(const std::string& name) {
        FixtureGroup& group = problem_.groups[name];
        group.attributes.try_emplace("document", name);
        group.attributes.try_emplace("format_version", std::int64_t{2});
        return group;
    }
    FixtureGroup& reactor() { return document("reactor"); }
    FixtureGroup& initial_state() { return document("initial_state"); }
    FixtureGroup& settings() { return document("settings"); }
    FixtureGroup& control_signals() { return document("control_signals"); }

    // Sets `allow_table_clamps` in the settings document.
    void allow_table_clamps(std::int64_t value) {
        settings().datasets["allow_table_clamps"] = integer_scalar(value);
    }

    // The reactor's geometry group, in place.
    FixtureGroup& geometry() { return reactor().groups["geometry"]; }

    // Sets or removes one option of the settings document.
    void set_option(std::string_view name, FixtureDataset value) {
        settings().datasets[std::string(name)] = std::move(value);
    }
    void clear_option(std::string_view name) { settings().datasets.erase(std::string(name)); }

    // Moves the document `member` out of the problem input into the file `file`, and makes the
    // member an external link to the root of that file. A relative `file` resolves against the
    // directory of the problem input; the file is written by the next load.
    void link(const std::string& member, const std::filesystem::path& file) {
        auto node = problem_.groups.extract(member);
        if (node.empty()) {
            throw std::logic_error("ProblemTest::link: no in-place document '" + member + "'");
        }
        linked_.emplace_back(file, std::move(node.mapped()));
        problem_.external_links[member] = FixtureExternalLink{.file = file.string()};
    }

    // The directory of the problem input; a relative link resolves against it.
    [[nodiscard]] const std::filesystem::path& directory() const { return directory_; }

    // Writes the library and the problem input, then loads the problem.
    std::expected<Problem, Diagnostics> load() {
        write_files();
        return load_problem(problem_file());
    }

    // As load(), but `edit` changes the written problem input's root group through the HDF5
    // API first, for defects the fixture cannot describe.
    template <typename Edit> std::expected<Problem, Diagnostics> load_edited(Edit edit) {
        write_files();
        {
            const H5::H5File file(problem_file().string(), H5F_ACC_RDWR);
            edit(file.openGroup("/"));
        }
        return load_problem(problem_file());
    }

    // Loads a valid problem; the load must succeed.
    Problem accepted() {
        auto result = load();
        EXPECT_TRUE(result.has_value()) << (result ? std::string{} : all_text(result.error()));
        return result ? std::move(*result) : Problem{};
    }

    // Loads a defective problem; the load must fail.
    Diagnostics rejected() { return failure(load()); }
    template <typename Edit> Diagnostics rejected_edited(Edit edit) {
        return failure(load_edited(edit));
    }

    [[nodiscard]] std::filesystem::path problem_file() const { return directory_ / "problem.h5"; }
    [[nodiscard]] std::filesystem::path library_file() const { return directory_ / "library.h5"; }

    // Writes the library, the problem input and its linked document files without loading.
    void write_files() {
        std::filesystem::create_directories(directory_);
        write_fixture(library_, library_file());
        write_problem(problem_, problem_file());
        for (const auto& [file, linked_document] : linked_) {
            const std::filesystem::path path = directory_ / file;
            std::filesystem::create_directories(path.parent_path());
            write_problem(linked_document, path);
        }
    }

private:
    static Diagnostics failure(const std::expected<Problem, Diagnostics>& result) {
        EXPECT_FALSE(result.has_value()) << "a defective problem input was accepted";
        return result.has_value() ? Diagnostics{} : result.error();
    }

    FixtureLibrary library_ = make_valid_fixture();
    FixtureGroup problem_ = make_valid_problem("library.h5");
    std::vector<std::pair<std::filesystem::path, FixtureGroup>> linked_;
    std::filesystem::path directory_ = [] {
        std::string name =
            std::format("nts_problem_{}_{}",
                        testing::UnitTest::GetInstance()->current_test_info()->name(), getpid());
        std::ranges::replace(name, '/', '_'); // parameterized test names hold '/'
        return std::filesystem::temp_directory_path() / name;
    }();
};

// A diagnostic at `location` whose message contains `message`.
inline bool has_diagnostic(const Diagnostics& diagnostics, std::string_view location,
                           std::string_view message) {
    return std::ranges::any_of(diagnostics, [&](const Diagnostic& d) {
        return d.location == location && d.message.contains(message);
    });
}

// A diagnostic at `location` with entry `entry` whose message contains `message`.
inline bool has_entry_diagnostic(const Diagnostics& diagnostics, std::string_view location,
                                 std::string_view entry, std::string_view message) {
    return std::ranges::any_of(diagnostics, [&](const Diagnostic& d) {
        return d.location == location && d.entry == entry && d.message.contains(message);
    });
}

inline bool has_location(const Diagnostics& diagnostics, std::string_view location) {
    return std::ranges::any_of(diagnostics,
                               [&](const Diagnostic& d) { return d.location == location; });
}

// Exactly one diagnostic, at `location`, whose message contains `message`.
inline void expect_one(const Diagnostics& diagnostics, std::string_view location,
                       std::string_view message) {
    EXPECT_EQ(diagnostics.size(), 1U) << all_text(diagnostics);
    EXPECT_TRUE(has_diagnostic(diagnostics, location, message))
        << "expected '" << message << "' at " << location << "\n"
        << all_text(diagnostics);
}

} // namespace nts::test
