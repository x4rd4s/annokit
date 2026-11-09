// SPDX-License-Identifier: GPL-3.0-or-later
//
// A file compiled into the program by annokit_embed_files (cmake/EmbedFiles.cmake).
#pragma once

#include <cstddef>
#include <string_view>

namespace annokit::core {

struct EmbeddedFile {
    const char* name = nullptr;  ///< the file name without folder and extension ("de" for de.lang)
    const unsigned char* data = nullptr;
    std::size_t size = 0;

    [[nodiscard]] std::string_view text() const noexcept { return {reinterpret_cast<const char*>(data), size}; }
};

}  // namespace annokit::core
