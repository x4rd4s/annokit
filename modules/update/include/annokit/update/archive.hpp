// SPDX-License-Identifier: GPL-3.0-or-later
//
// The files of a release package: a zip archive (annokit/core/zip_archive.hpp) or a gzip-packed
// tar archive (ustar, GNU and pax long names), read whole into memory. Only regular files and
// folders: a link of any kind is refused, as is a path that would leave the folder it is unpacked
// into.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "annokit/core/result.hpp"

namespace annokit::update {

struct PackageFile {
    std::string path;  ///< relative, '/' between folders, never "..", never absolute
    std::vector<std::uint8_t> bytes;
    bool executable = false;  ///< the archive marks it executable (a Unix mode with an x bit)
};

/// What a package may hold at most, against archives made to cost memory.
struct PackageLimits {
    std::size_t max_files = 4096;
    std::uint64_t max_total = std::uint64_t{512} << 20;  ///< every file unpacked together
};

/// The data of a gzip file (one member), checked against its CRC and size.
[[nodiscard]] core::Result<std::vector<std::uint8_t>> gunzip(std::span<const std::uint8_t> file,
                                                             std::uint64_t max_size);

/// The regular files of a tar archive; folders are left out.
[[nodiscard]] core::Result<std::vector<PackageFile>> read_tar(std::span<const std::uint8_t> archive,
                                                              const PackageLimits& limits = {});

/// The files of a package by its file name: "*.zip" or "*.tar.gz" / "*.tgz".
[[nodiscard]] core::Result<std::vector<PackageFile>> read_package(std::span<const std::uint8_t> bytes,
                                                                  std::string_view file_name,
                                                                  const PackageLimits& limits = {});

/// Whether a path from an archive may be written below a folder: relative, '/' between parts,
/// none of them empty, "." or "..", no '\\' or ':' and no control characters.
[[nodiscard]] bool is_safe_path(std::string_view path) noexcept;

/// The files below `folder` ("Game-1.2.0/" or "Game-1.2.0/Game.app"), with that folder taken off
/// their paths; fails if there are none.
[[nodiscard]] core::Result<std::vector<PackageFile>> files_below(std::vector<PackageFile> files,
                                                                 std::string_view folder);

/// The one folder every file lies in ("Game-1.2.0"), or nothing if they do not share one.
[[nodiscard]] std::string top_folder(std::span<const PackageFile> files);

}  // namespace annokit::update
