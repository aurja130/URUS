// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Aurora Jahan
// Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

#pragma once

// Checked access to the groups, datasets and attributes of the HDF5 input files (the nodal
// library and the problem input). Problems are collected as diagnostics; an HDF5 failure in a
// read becomes a diagnostic at the location of that read.

#include "nts/common/diagnostic.hpp"

#include <H5Cpp.h>

#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace nts::detail {

inline constexpr std::size_t max_entry_diagnostics = 20;
inline constexpr std::size_t float64_size = 8;

// "[a, b, c]".
std::string join(std::span<const std::size_t> values);

// Collects the diagnostics of one file.
class Reporter {
public:
    explicit Reporter(std::string file) : file_(std::move(file)) {}

    void add(std::string location, std::string entry, std::string message) {
        diagnostics_.push_back(Diagnostic{.file = file_,
                                          .location = std::move(location),
                                          .entry = std::move(entry),
                                          .message = std::move(message)});
    }
    void add(std::string location, std::string message) {
        add(std::move(location), std::string{}, std::move(message));
    }
    // Adds diagnostics collected elsewhere, after the ones already held.
    void append(Diagnostics diagnostics) {
        for (Diagnostic& diagnostic : diagnostics) {
            diagnostics_.push_back(std::move(diagnostic));
        }
    }
    [[nodiscard]] std::size_t count() const { return diagnostics_.size(); }
    [[nodiscard]] Diagnostics take() { return std::move(diagnostics_); }

private:
    std::string file_;
    Diagnostics diagnostics_;
};

// Runs `read`; an HDF5 failure becomes a diagnostic at `location`, so the file's other checks
// still run. False on failure.
template <typename Read>
bool guarded_read(Reporter& reporter, const std::string& location, Read read) {
    try {
        read();
        return true;
    } catch (const H5::Exception& exception) {
        reporter.add(location, std::format("HDF5 cannot read it: {} failed: {}",
                                           exception.getFuncName(), exception.getDetailMsg()));
        return false;
    }
}

// Reports the entry diagnostics of one dataset: at most max_entry_diagnostics of them, then the
// number left out.
class EntryReporter {
public:
    EntryReporter(Reporter& reporter, std::string location)
        : reporter_(reporter), location_(std::move(location)) {}

    // `entry_text` builds the entry part; `message` is the text or a callable building it.
    // The callables run only for a listed diagnostic.
    template <typename EntryText, typename Message>
    void add(EntryText entry_text, Message message) {
        if (reported_ < max_entry_diagnostics) {
            if constexpr (std::is_invocable_v<Message>) {
                reporter_.add(location_, entry_text(), message());
            } else {
                reporter_.add(location_, entry_text(), std::string(std::move(message)));
            }
            ++reported_;
        } else {
            ++left_out_;
        }
    }
    void finish() {
        if (left_out_ > 0) {
            reporter_.add(location_, std::format("{} more diagnostics are not listed", left_out_));
        }
    }

private:
    Reporter& reporter_;
    std::string location_;
    std::size_t reported_ = 0;
    std::size_t left_out_ = 0;
};

// True when `name` is a member of `group` of the type `type`. An external link is followed to
// classify it; callers that must not follow one check is_external_link first.
bool is_child(const H5::Group& group, const std::string& name, H5O_type_t type);

// The target of an external link as written in the link: the file name and the object path.
struct ExternalLinkTarget {
    std::string file;
    std::string object;
};

// True when `name` is an external link of `group`. The link is classified, never followed.
bool is_external_link(const H5::Group& group, const std::string& name);

// The target of `name` when it is an external link of `group`; nullopt otherwise. The link is
// classified, never followed.
std::optional<ExternalLinkTarget> external_link_target(const H5::Group& group,
                                                       const std::string& name);

// The sizes of the dimensions of `dataset`; empty for a scalar.
std::vector<std::size_t> dimensions(const H5::DataSet& dataset);

// True when `dataset` holds 64-bit floating-point values.
bool is_float64(const H5::DataSet& dataset);

// The values of a float64 dataset in row-major order.
std::vector<double> read_doubles(const H5::DataSet& dataset);

// The strings of a fixed- or variable-length string dataset, in row-major order.
std::vector<std::string> read_strings(const H5::DataSet& dataset);

// The value of an attribute of `object`, which is `object_path` for the locations of the
// diagnostics. A diagnostic (and nullopt) when it is missing, of another type, or not one
// value.
std::optional<std::int64_t> read_integer_attribute(Reporter& reporter, const H5::H5Object& object,
                                                   const std::string& object_path,
                                                   const std::string& name);
std::optional<double> read_float_attribute(Reporter& reporter, const H5::H5Object& object,
                                           const std::string& object_path, const std::string& name);
std::optional<std::string> read_string_attribute(Reporter& reporter, const H5::H5Object& object,
                                                 const std::string& object_path,
                                                 const std::string& name);

// Checks that `object` carries the units `expected`.
bool check_units(Reporter& reporter, const H5::H5Object& object, const std::string& object_path,
                 std::string_view expected);

// The elements of an integer dataset as signed 64-bit values, in row-major order. The caller
// checks that the datatype is an integer. Nullopt when an element exceeds the signed 64-bit
// range.
[[nodiscard]] std::optional<std::vector<std::int64_t>> read_integers(const H5::DataSet& dataset);

// The names of the members of `group`, in index order.
[[nodiscard]] std::vector<std::string> member_names(const H5::Group& group);

} // namespace nts::detail
