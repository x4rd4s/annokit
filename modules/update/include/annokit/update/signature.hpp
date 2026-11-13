// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ed25519 signatures (RFC 8032, as OpenSSL's "pkeyutl -sign -rawin" makes them) over update
// manifests, and the hexadecimal text keys, signatures and SHA-256 sums are written in.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace annokit::update {

using PublicKey = std::array<std::uint8_t, 32>;
using Signature = std::array<std::uint8_t, 64>;
using Sha256 = std::array<std::uint8_t, 32>;

/// Whether `signature` is `key`'s signature of exactly these bytes.
[[nodiscard]] bool verify_signature(const Signature& signature, const PublicKey& key,
                                    std::span<const std::uint8_t> message) noexcept;

/// A key pair made from a 32-byte seed (the private key): for tests and tools that sign.
struct KeyPair {
    std::array<std::uint8_t, 64> secret{};
    PublicKey key{};
};
[[nodiscard]] KeyPair key_pair_from_seed(const std::array<std::uint8_t, 32>& seed) noexcept;
[[nodiscard]] Signature sign(const KeyPair& pair, std::span<const std::uint8_t> message) noexcept;

/// Lower-case hexadecimal of the bytes.
[[nodiscard]] std::string to_hex(std::span<const std::uint8_t> bytes);
/// Exactly N bytes from 2 * N hexadecimal digits (either case); white space around them is passed
/// over. Nothing for anything else.
template <std::size_t N>
[[nodiscard]] std::optional<std::array<std::uint8_t, N>> from_hex(std::string_view text) noexcept;

}  // namespace annokit::update
