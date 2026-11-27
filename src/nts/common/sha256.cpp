// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Aurora Jahan
// Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

#include "nts/common/sha256.hpp"

#include "nts/common/invariant.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace nts {
namespace {

// FIPS 180-4 §4.2.2: the first 32 bits of the fractional parts of the cube roots of the first 64
// primes.
constexpr std::array<std::uint32_t, 64> round_constants = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

constexpr std::size_t block_bytes = 64;
constexpr std::size_t length_field_offset = 56;
constexpr std::size_t file_chunk_bytes = std::size_t{1} << 20;

constexpr std::uint32_t rotate_right(std::uint32_t value, unsigned int count) {
    return (value >> count) | (value << (32U - count));
}

// FIPS 180-4 §6.2.2: one compression of a 64-byte block into the chaining state.
void compress(std::array<std::uint32_t, 8>& state, const std::uint8_t* block) {
    std::array<std::uint32_t, 64> schedule{};
    for (std::size_t t = 0; t < 16; ++t) {
        schedule[t] = (static_cast<std::uint32_t>(block[(4 * t)]) << 24) |
                      (static_cast<std::uint32_t>(block[(4 * t) + 1]) << 16) |
                      (static_cast<std::uint32_t>(block[(4 * t) + 2]) << 8) |
                      static_cast<std::uint32_t>(block[(4 * t) + 3]);
    }
    for (std::size_t t = 16; t < 64; ++t) {
        const std::uint32_t early = schedule[t - 15];
        const std::uint32_t late = schedule[t - 2];
        const std::uint32_t sigma0 =
            rotate_right(early, 7) ^ rotate_right(early, 18) ^ (early >> 3);
        const std::uint32_t sigma1 = rotate_right(late, 17) ^ rotate_right(late, 19) ^ (late >> 10);
        schedule[t] = schedule[t - 16] + sigma0 + schedule[t - 7] + sigma1;
    }

    std::uint32_t a = state[0];
    std::uint32_t b = state[1];
    std::uint32_t c = state[2];
    std::uint32_t d = state[3];
    std::uint32_t e = state[4];
    std::uint32_t f = state[5];
    std::uint32_t g = state[6];
    std::uint32_t h = state[7];
    for (std::size_t t = 0; t < 64; ++t) {
        const std::uint32_t big_sigma1 =
            rotate_right(e, 6) ^ rotate_right(e, 11) ^ rotate_right(e, 25);
        const std::uint32_t choice = (e & f) ^ (~e & g);
        const std::uint32_t temp1 = h + big_sigma1 + choice + round_constants[t] + schedule[t];
        const std::uint32_t big_sigma0 =
            rotate_right(a, 2) ^ rotate_right(a, 13) ^ rotate_right(a, 22);
        const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t temp2 = big_sigma0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

} // namespace

Sha256::Sha256()
    : state_{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
             0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19} {}

void Sha256::update(std::span<const std::byte> data) {
    NTS_ASSERT(!finished_, "Sha256::update called after finish");
    NTS_ASSERT(data.size() <= std::numeric_limits<std::uint64_t>::max() - message_bytes_,
               "Sha256 message length overflows");
    message_bytes_ += data.size();
    for (const std::byte byte : data) {
        block_[block_size_] = std::to_integer<std::uint8_t>(byte);
        ++block_size_;
        if (block_size_ == block_bytes) {
            compress(state_, block_.data());
            block_size_ = 0;
        }
    }
}

Sha256Digest Sha256::finish() {
    NTS_ASSERT(!finished_, "Sha256::finish called twice");
    NTS_ASSERT(message_bytes_ <= std::numeric_limits<std::uint64_t>::max() / 8,
               "Sha256 message length overflows the bit count");
    // FIPS 180-4 §5.1.1: a 1 bit, zeros, then the 64-bit message length in bits.
    const std::uint64_t bit_length = message_bytes_ * 8;
    block_[block_size_] = 0x80;
    ++block_size_;
    if (block_size_ > length_field_offset) {
        for (std::size_t i = block_size_; i < block_bytes; ++i) {
            block_[i] = 0;
        }
        compress(state_, block_.data());
        block_size_ = 0;
    }
    for (std::size_t i = block_size_; i < length_field_offset; ++i) {
        block_[i] = 0;
    }
    for (std::size_t i = 0; i < 8; ++i) {
        block_[length_field_offset + i] = static_cast<std::uint8_t>(bit_length >> (56 - (8 * i)));
    }
    compress(state_, block_.data());
    finished_ = true;

    Sha256Digest digest{};
    for (std::size_t word = 0; word < 8; ++word) {
        for (std::size_t byte = 0; byte < 4; ++byte) {
            digest[(4 * word) + byte] =
                static_cast<std::uint8_t>(state_[word] >> (24 - (8 * byte)));
        }
    }
    return digest;
}

std::string to_hex(const Sha256Digest& digest) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string text;
    text.reserve(2 * digest.size());
    for (const std::uint8_t byte : digest) {
        text.push_back(digits[static_cast<std::size_t>(byte >> 4)]);
        text.push_back(digits[static_cast<std::size_t>(byte & 0x0F)]);
    }
    return text;
}

std::string sha256_hex(std::span<const std::byte> data) {
    Sha256 hash;
    hash.update(data);
    return to_hex(hash.finish());
}

std::expected<std::string, std::string> sha256_file_hex(const std::filesystem::path& file) {
    std::ifstream stream(file, std::ios::binary);
    if (!stream) {
        return std::unexpected(std::format("cannot open '{}' for hashing", file.string()));
    }
    Sha256 hash;
    std::vector<char> chunk(file_chunk_bytes);
    while (true) {
        stream.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        const auto count = static_cast<std::size_t>(stream.gcount());
        hash.update(std::as_bytes(std::span<const char>(chunk.data(), count)));
        if (stream.eof()) {
            break;
        }
        if (!stream) {
            return std::unexpected(std::format("cannot read '{}' for hashing", file.string()));
        }
    }
    return to_hex(hash.finish());
}

} // namespace nts
