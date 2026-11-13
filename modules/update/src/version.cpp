// SPDX-License-Identifier: GPL-3.0-or-later
#include "annokit/update/version.hpp"

namespace annokit::update {

std::optional<Version> parse_version(std::string_view text) noexcept {
    if (text.starts_with('v')) {
        text.remove_prefix(1);
    }
    std::uint32_t parts[3] = {};
    std::size_t at = 0;
    for (std::size_t i = 0; i < 3; ++i) {
        if (i > 0) {
            if (at >= text.size() || text[at] != '.') {
                return std::nullopt;
            }
            ++at;
        }
        std::size_t digits = 0;
        std::uint32_t value = 0;
        while (at < text.size() && text[at] >= '0' && text[at] <= '9') {
            if (++digits > 6) {
                return std::nullopt;
            }
            value = value * 10 + static_cast<std::uint32_t>(text[at] - '0');
            ++at;
        }
        if (digits == 0) {
            return std::nullopt;
        }
        parts[i] = value;
    }
    if (at != text.size()) {
        return std::nullopt;
    }
    return Version{parts[0], parts[1], parts[2]};
}

std::string to_string(const Version& version) {
    return std::to_string(version.major) + "." + std::to_string(version.minor) + "." + std::to_string(version.patch);
}

}  // namespace annokit::update
