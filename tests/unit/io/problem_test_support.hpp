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
#include <expected>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <system_error>
#include <unistd.h>
#include <utility>

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

class ProblemTest : public testing::Test {
protected:
    void TearDown() override {
        std::error_code ignored;
        std::filesystem::remove_all(directory_, ignored);
    }

    // The library and the problem input; a test changes them before loading.
    FixtureLibrary& library() { return library_; }
    FixtureGroup& problem() { return problem_; }

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

private:
    void write_files() {
        std::filesystem::create_directories(directory_);
        write_fixture(library_, library_file());
        write_problem(problem_, problem_file());
    }

    static Diagnostics failure(const std::expected<Problem, Diagnostics>& result) {
        EXPECT_FALSE(result.has_value()) << "a defective problem input was accepted";
        return result.has_value() ? Diagnostics{} : result.error();
    }

    FixtureLibrary library_ = make_valid_fixture();
    FixtureGroup problem_ = make_valid_problem("library.h5");
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
