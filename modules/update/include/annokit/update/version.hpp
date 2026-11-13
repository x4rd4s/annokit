// SPDX-License-Identifier: GPL-3.0-or-later
//
// Release versions as a program and its update manifest name them: "1.2.3", optionally with a
// leading "v" (a tag's name). Nothing else: a pre-release or build suffix ("1.2.3-rc1", "1.2.3+abc")
// is not a release the updater offers.
#pragma once

#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace annokit::update {

struct Version {
    std::uint32_t major = 0;
    std::uint32_t minor = 0;
    std::uint32_t patch = 0;

    friend constexpr auto operator<=>(const Version&, const Version&) = default;
};

/// "1.2.3" or "v1.2.3": three numbers of at most six digits each. Nothing for anything else.
[[nodiscard]] std::optional<Version> parse_version(std::string_view text) noexcept;
/// "1.2.3".
[[nodiscard]] std::string to_string(const Version& version);

}  // namespace annokit::update
