// SPDX-License-Identifier: GPL-3.0-or-later
//
// The running program's own file, and starting a program that goes on when this one ends (the
// restart after an update).
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "annokit/core/result.hpp"

namespace annokit::update {

/// The path of the running program's executable (resolved, absolute).
[[nodiscard]] core::Result<std::filesystem::path> executable_path();

/// Starts `program` with `arguments` (UTF-8) without waiting for it; it inherits no handles and
/// keeps running when this process ends.
[[nodiscard]] core::Status start_program(const std::filesystem::path& program,
                                         const std::vector<std::string>& arguments);

}  // namespace annokit::update
