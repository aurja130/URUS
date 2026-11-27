// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Aurora Jahan
// Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

#pragma once

// SHA-256 (FIPS 180-4) for the provenance of the run record (output contract §6).

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>

namespace nts {

using Sha256Digest = std::array<std::uint8_t, 32>;

// Incremental SHA-256: update() any number of times, then finish() once.
class Sha256 {
public:
    Sha256();
    void update(std::span<const std::byte> data);
    // Pads, processes the last block and returns the digest. Precondition: not yet finished.
    [[nodiscard]] Sha256Digest finish();

private:
    std::array<std::uint32_t, 8> state_;
    std::array<std::uint8_t, 64> block_{};
    std::size_t block_size_ = 0;
    std::uint64_t message_bytes_ = 0;
    bool finished_ = false;
};

// 64 lower-case hexadecimal digits.
[[nodiscard]] std::string to_hex(const Sha256Digest& digest);

[[nodiscard]] std::string sha256_hex(std::span<const std::byte> data);

// The digest of the file's bytes, read in chunks; an error message naming the file when it
// cannot be opened or read.
[[nodiscard]] std::expected<std::string, std::string>
sha256_file_hex(const std::filesystem::path& file);

} // namespace nts
