// SPDX-License-Identifier: GPL-3.0-or-later
//
// Reading resource archives of version 2.2 (magic "Resource File V2.2"): a chain of blocks, each
// with a directory of files right before its header. Blocks stored as they are and blocks packed
// with zlib are read; encrypted blocks, blocks that pack all their files into one buffer and
// deleted blocks are skipped. All numbers little-endian.
//
//   0x000  char[18] magic, then zeros
//   0x310  u64      offset of the first block header
//   block header (32 bytes): u32 flags (1 zlib, 2 encrypted, 4 one buffer, 8 deleted), u32 files,
//                            u64 directory bytes stored, u64 directory bytes unpacked, u64 next
//                            block (the last one's points at the end of the archive)
//   directory entry (560 bytes): char16_t name[260] (UTF-16LE, 0-terminated), u64 offset,
//                                u64 bytes stored, u64 bytes unpacked, u64 time, u64 reserved
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "annokit/core/result.hpp"

namespace annokit::core {

/// A file of a resource archive.
struct ResourceEntry {
    std::string path;          ///< UTF-8, '/' between folders, e.g. "data/texts.xml"
    std::uint64_t offset = 0;  ///< of its bytes in the archive
    std::uint64_t stored = 0;  ///< bytes in the archive
    std::uint64_t size = 0;    ///< bytes of the file
    bool compressed = false;   ///< a zlib stream
};

class ResourceArchive {
public:
    /// Reads the directories of the archive at `path`; the files stay on disk until read().
    static Result<ResourceArchive> open(const std::filesystem::path& path);
    /// The same from the archive's bytes.
    static Result<ResourceArchive> from_bytes(std::vector<std::uint8_t> bytes);

    /// Every file of the readable blocks, in block and directory order.
    [[nodiscard]] const std::vector<ResourceEntry>& entries() const noexcept { return m_entries; }
    /// Blocks with files that were skipped: encrypted, packed into one buffer, or packed with zlib
    /// in a build without zlib.
    [[nodiscard]] std::size_t skipped_blocks() const noexcept { return m_skipped; }
    /// The entry named `path` (ASCII letters in any case, '/' or '\\'), nullptr if there is none.
    [[nodiscard]] const ResourceEntry* find(std::string_view path) const;
    /// The bytes of the file `path`.
    [[nodiscard]] Result<std::vector<std::uint8_t>> read(std::string_view path) const;

private:
    std::filesystem::path m_path;  ///< empty: the archive is m_bytes
    std::vector<std::uint8_t> m_bytes;
    std::vector<ResourceEntry> m_entries;
    std::size_t m_skipped = 0;
};

}  // namespace annokit::core
