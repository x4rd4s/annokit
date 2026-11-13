// SPDX-License-Identifier: GPL-3.0-or-later
//
// Puts the files of a new release in place of the running one's, keeping the replaced files so
// that the previous version can come back. Nothing is overwritten: each old file is renamed into
// the work folder and the new one renamed into its place, so a running program (whose file Windows
// lets rename but not overwrite) keeps running from its old file until it restarts.
//
//   <work>/new/        the new files while they are written
//   <work>/previous/   the replaced version: files/ or tree/, "version", "added"
//   <work>/discarded/  what restore_previous() took out, deleted by clean_up()
//
// The work folder must be on the same volume as the installation (renames only).
#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "annokit/core/result.hpp"
#include "annokit/update/archive.hpp"

namespace annokit::update {

enum class InstallLayout : std::uint8_t {
    /// The package's files go into `root` one by one; other files there stay (a folder with bin/
    /// and share/ that may hold more than the package).
    Files,
    /// `root` is replaced as a whole by the package's files (a macOS app bundle, whose signature
    /// covers every file in it).
    Tree,
};

struct Installation {
    std::filesystem::path root;
    std::filesystem::path work;
    InstallLayout layout = InstallLayout::Files;
};

/// Whether the installation's folder (for a Tree its parent) and the work folder can be written.
[[nodiscard]] core::Status check_writable(const Installation& installation);

/// Installs `files` (relative paths, is_safe_path()). The files replaced are kept as the previous
/// version, named `replaced_version` ("1.1.0"); a previous version kept before is deleted. On an
/// error everything done so far is undone.
[[nodiscard]] core::Status install_files(const Installation& installation, std::span<const PackageFile> files,
                                         std::string_view replaced_version);

/// The name of the version install_files() replaced and kept, if one is kept.
[[nodiscard]] std::optional<std::string> previous_version(const Installation& installation);

/// Puts the previous version back in place of the installed one, which goes to discarded/.
[[nodiscard]] core::Status restore_previous(const Installation& installation);

/// Deletes what an interrupted install or a restore left behind (new/, discarded/). Files still in
/// use (the program that was replaced, on Windows) stay until the next time.
void clean_up(const Installation& installation);

}  // namespace annokit::update
