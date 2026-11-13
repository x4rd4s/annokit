// SPDX-License-Identifier: GPL-3.0-or-later
//
// The update manifest: which release is the latest and where its package for each platform lies.
// A data file (annokit/core/data_file.hpp), signed as a whole (signature.hpp):
//
//   format = 1
//   version = "1.2.0"
//   notes = "https://example.org/releases/1.2.0"     the release notes for people (optional)
//
//   [windows-x64]                                    one table per platform
//   url = "https://example.org/files/game-1.2.0-windows-x64.zip"
//   size = 4657286
//   sha256 = "2ad39a5e..."
//
// Only https addresses are taken. A platform without a table has no package in this release.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "annokit/core/result.hpp"
#include "annokit/update/signature.hpp"
#include "annokit/update/version.hpp"

namespace annokit::update {

/// The format this code reads; a manifest of a later one is refused (the program needs an update
/// by hand).
inline constexpr std::int64_t kManifestFormat = 1;

struct ManifestPackage {
    std::string platform;  ///< the table's name ("windows-x64")
    std::string url;
    std::uint64_t size = 0;
    Sha256 sha256{};
};

struct Manifest {
    Version version;
    std::string notes;  ///< https address of the release notes, or empty
    std::vector<ManifestPackage> packages;

    /// The package of `platform`, or nullptr.
    [[nodiscard]] const ManifestPackage* package(std::string_view platform) const noexcept;
};

/// Reads a manifest; fails on another format, a missing or malformed value and an address that is
/// not https. Does not check the signature: verify the bytes first (read_signed_manifest).
[[nodiscard]] core::Result<Manifest> parse_manifest(std::string_view text);

/// The manifest of `bytes` if `signature_text` (the hexadecimal signature, as the .sig file holds
/// it) is a signature of exactly these bytes by one of `keys`.
[[nodiscard]] core::Result<Manifest> read_signed_manifest(std::span<const std::uint8_t> bytes,
                                                          std::string_view signature_text,
                                                          std::span<const PublicKey> keys);

}  // namespace annokit::update
