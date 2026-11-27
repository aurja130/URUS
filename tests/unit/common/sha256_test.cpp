// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Aurora Jahan
// Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

#include "nts/common/sha256.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::span<const std::byte> bytes_of(std::string_view text) {
    return std::as_bytes(std::span<const char>(text.data(), text.size()));
}

// The repeating message a..z, a..z, ... of `count` bytes.
std::vector<std::byte> pattern_bytes(std::size_t count) {
    std::vector<std::byte> bytes(count);
    for (std::size_t i = 0; i < count; ++i) {
        bytes[i] = static_cast<std::byte>(static_cast<std::uint8_t>('a' + (i % 26)));
    }
    return bytes;
}

std::string hash_in_pieces(const std::vector<std::byte>& bytes,
                           const std::vector<std::size_t>& piece_sizes) {
    nts::Sha256 hash;
    std::size_t offset = 0;
    for (const std::size_t size : piece_sizes) {
        hash.update(std::span<const std::byte>(bytes).subspan(offset, size));
        offset += size;
    }
    hash.update(std::span<const std::byte>(bytes).subspan(offset));
    return nts::to_hex(hash.finish());
}

} // namespace

// FIPS 180-4 test vectors: the empty message, "abc", and the two-block and four-block messages.
TEST(RunRecord, EmptyMessageMatchesFipsVector) {
    EXPECT_EQ(nts::sha256_hex(bytes_of("")),
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST(RunRecord, AbcMatchesFipsVector) {
    EXPECT_EQ(nts::sha256_hex(bytes_of("abc")),
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST(RunRecord, TwoBlockMessageMatchesFipsVector) {
    EXPECT_EQ(nts::sha256_hex(bytes_of("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")),
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST(RunRecord, FourBlockMessageMatchesFipsVector) {
    EXPECT_EQ(
        nts::sha256_hex(bytes_of("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmno"
                                 "ijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu")),
        "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1");
}

TEST(RunRecord, MillionAMatchesFipsVector) {
    const std::vector<std::byte> bytes(1000000, std::byte{'a'});
    EXPECT_EQ(nts::sha256_hex(bytes),
              "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

// Padding boundaries: the length field needs a second block at 56 bytes and an extra block at
// 64 bytes. Digests from an independent SHA-256 implementation.
TEST(RunRecord, PaddingBoundaryLengthsMatchReference) {
    EXPECT_EQ(nts::sha256_hex(pattern_bytes(55)),
              "595615dbe4f0f407ae397d08b4c2cb870cb9b0e11937416f950c5160acf9c005");
    EXPECT_EQ(nts::sha256_hex(pattern_bytes(56)),
              "784f623b787495078e93ff28a25b581df0584055a7e71d8cd90c454716b92f51");
    EXPECT_EQ(nts::sha256_hex(pattern_bytes(63)),
              "5ca3e1ef5207490eac01a795e5cc94d59582a5118bf9534665c8668d87aa647c");
    EXPECT_EQ(nts::sha256_hex(pattern_bytes(64)),
              "2fcd5a0d60e4c941381fcc4e00a4bf8be422c3ddfafb93c809e8d1e2bfffae8e");
    EXPECT_EQ(nts::sha256_hex(pattern_bytes(65)),
              "1b3cd1877ab2f2f19f7be001722554f336cb799df0329de0bb4c118dc6abc06d");
}

// Splitting the input across update() calls, at block-unaligned points, must not change the
// digest.
TEST(RunRecord, IncrementalUpdatesMatchOneShotHash) {
    const std::vector<std::byte> bytes = pattern_bytes(1000);
    const std::string one_shot = nts::sha256_hex(bytes);
    EXPECT_EQ(hash_in_pieces(bytes, {}), one_shot);
    EXPECT_EQ(hash_in_pieces(bytes, {1, 6, 57, 64}), one_shot);
    EXPECT_EQ(hash_in_pieces(bytes, {63, 1, 200, 300}), one_shot);
}

// sha256_file_hex reads in chunks; a file larger than one chunk must hash like its bytes do.
TEST(RunRecord, FileHashMatchesHashOfItsBytesAcrossChunks) {
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / "urus_sha256_test";
    std::filesystem::create_directories(directory);
    const std::filesystem::path file = directory / "chunked.bin";
    const std::vector<std::byte> bytes = pattern_bytes((1U << 20U) + 17);
    {
        std::ofstream out(file, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
    }

    const std::expected<std::string, std::string> hash = nts::sha256_file_hex(file);
    ASSERT_TRUE(hash.has_value()) << hash.error();
    EXPECT_EQ(*hash, nts::sha256_hex(bytes));
    std::filesystem::remove(file);
}

TEST(RunRecord, MissingFileIsAnErrorNamingThePath) {
    const std::filesystem::path missing =
        std::filesystem::temp_directory_path() / "urus_sha256_test" / "no_such_file.bin";
    const std::expected<std::string, std::string> hash = nts::sha256_file_hex(missing);
    ASSERT_FALSE(hash.has_value());
    EXPECT_NE(hash.error().find(missing.string()), std::string::npos) << hash.error();
}
