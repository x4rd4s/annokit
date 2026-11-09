// SPDX-License-Identifier: GPL-3.0-or-later
#include "annokit/core/resource_archive.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <functional>
#include <span>
#include <system_error>
#include <utility>

#include "annokit/core/compress.hpp"
#include "annokit/core/file.hpp"

namespace annokit::core {

namespace {

constexpr std::string_view kMagic = "Resource File V2.2";
constexpr std::uint64_t kFirstBlockAt = 0x310;  ///< u64: where the first block header is
constexpr std::uint64_t kBlockHeaderSize = 32;
constexpr std::uint64_t kEntrySize = 560;  ///< 260 UTF-16 characters, five u64
constexpr std::size_t kNameChars = 260;
constexpr std::uint32_t kFlagZlib = 1U;
constexpr std::uint32_t kFlagEncrypted = 2U;
constexpr std::uint32_t kFlagOneBuffer = 4U;
constexpr std::uint32_t kFlagDeleted = 8U;
/// A directory larger than this is refused (some 120 000 entries).
constexpr std::uint64_t kMaxDirectory = std::uint64_t{64} << 20;
constexpr std::size_t kMaxBlocks = 65536;

std::uint32_t u32_at(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8U) |
           (static_cast<std::uint32_t>(p[2]) << 16U) | (static_cast<std::uint32_t>(p[3]) << 24U);
}

std::uint64_t u64_at(const std::uint8_t* p) {
    return static_cast<std::uint64_t>(u32_at(p)) | (static_cast<std::uint64_t>(u32_at(p + 4)) << 32U);
}

void append_utf8(std::string& out, std::uint32_t c) {
    if (c < 0x80) {
        out.push_back(static_cast<char>(c));
    } else if (c < 0x800) {
        out.push_back(static_cast<char>(0xC0U | (c >> 6U)));
        out.push_back(static_cast<char>(0x80U | (c & 0x3FU)));
    } else if (c < 0x10000) {
        out.push_back(static_cast<char>(0xE0U | (c >> 12U)));
        out.push_back(static_cast<char>(0x80U | ((c >> 6U) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | (c & 0x3FU)));
    } else {
        out.push_back(static_cast<char>(0xF0U | (c >> 18U)));
        out.push_back(static_cast<char>(0x80U | ((c >> 12U) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | ((c >> 6U) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | (c & 0x3FU)));
    }
}

/// UTF-16LE up to its first 0 (at most kNameChars) as UTF-8, '\\' as '/'. A lone surrogate
/// becomes U+FFFD.
std::string name_at(const std::uint8_t* p) {
    const auto unit = [p](std::size_t i) {
        return static_cast<std::uint32_t>(p[i * 2]) | (static_cast<std::uint32_t>(p[i * 2 + 1]) << 8U);
    };
    std::string out;
    for (std::size_t i = 0; i < kNameChars; ++i) {
        std::uint32_t c = unit(i);
        if (c == 0) {
            break;
        }
        if (c >= 0xD800 && c <= 0xDFFF) {
            const std::uint32_t low = i + 1 < kNameChars ? unit(i + 1) : 0;
            if (c <= 0xDBFF && low >= 0xDC00 && low <= 0xDFFF) {
                c = 0x10000 + ((c - 0xD800) << 10U) + (low - 0xDC00);
                ++i;
            } else {
                c = 0xFFFD;
            }
        }
        append_utf8(out, c == '\\' ? '/' : c);
    }
    return out;
}

/// Reads `count` bytes at `offset` of the archive, from the file or from memory.
using ReadAt = std::function<bool(std::uint64_t offset, std::size_t count, std::uint8_t* out)>;

struct Directory {
    std::vector<ResourceEntry> entries;
    std::size_t skipped = 0;
};

Result<Directory> read_directories(std::uint64_t size, const ReadAt& read_at) {
    std::array<std::uint8_t, kMagic.size()> magic{};
    if (size < kFirstBlockAt + 8 || !read_at(0, magic.size(), magic.data()) ||
        std::memcmp(magic.data(), kMagic.data(), kMagic.size()) != 0) {
        return make_error(ErrorCode::InvalidFormat, "not a resource archive of version 2.2");
    }
    std::array<std::uint8_t, 8> word{};
    if (!read_at(kFirstBlockAt, word.size(), word.data())) {
        return make_error(ErrorCode::InvalidFormat, "resource archive header cut off");
    }
    Directory out;
    std::uint64_t block = u64_at(word.data());
    for (std::size_t n = 0; block < size; ++n) {
        if (n == kMaxBlocks || block > size - kBlockHeaderSize) {
            return make_error(ErrorCode::InvalidFormat, "resource archive block chain is broken");
        }
        std::array<std::uint8_t, kBlockHeaderSize> header{};
        if (!read_at(block, header.size(), header.data())) {
            return make_error(ErrorCode::InvalidFormat, "resource archive block header cut off");
        }
        const std::uint32_t flags = u32_at(header.data());
        const std::uint32_t files = u32_at(header.data() + 4);
        const std::uint64_t stored = u64_at(header.data() + 8);
        const std::uint64_t unpacked = u64_at(header.data() + 16);
        const std::uint64_t next = u64_at(header.data() + 24);
        if (next <= block) {
            return make_error(ErrorCode::InvalidFormat, "resource archive block chain runs backwards");
        }
        const bool zlib = (flags & kFlagZlib) != 0;
        if ((flags & kFlagDeleted) != 0 || files == 0) {
            block = next;
            continue;
        }
        if ((flags & (kFlagEncrypted | kFlagOneBuffer)) != 0 || (zlib && !has_zlib())) {
            ++out.skipped;
            block = next;
            continue;
        }
        if (unpacked != std::uint64_t{files} * kEntrySize || unpacked > kMaxDirectory || stored > kMaxDirectory ||
            stored > block || (!zlib && stored != unpacked)) {
            return make_error(ErrorCode::InvalidFormat, "resource archive directory has an invalid size");
        }
        std::vector<std::uint8_t> directory(static_cast<std::size_t>(stored));
        if (!read_at(block - stored, directory.size(), directory.data())) {
            return make_error(ErrorCode::InvalidFormat, "resource archive directory cut off");
        }
        if (zlib) {
            auto inflated = zlib_inflate(directory, static_cast<std::size_t>(unpacked));
            if (!inflated) {
                return make_error(ErrorCode::InvalidFormat,
                                  "resource archive directory does not unpack: " + inflated.error().message);
            }
            directory = std::move(inflated).value();
        }
        for (std::uint32_t i = 0; i < files; ++i) {
            const std::uint8_t* e = directory.data() + std::size_t{i} * kEntrySize;
            ResourceEntry entry;
            entry.path = name_at(e);
            entry.offset = u64_at(e + kNameChars * 2);
            entry.stored = u64_at(e + kNameChars * 2 + 8);
            entry.size = u64_at(e + kNameChars * 2 + 16);
            entry.compressed = zlib;
            if (entry.offset > size || entry.stored > size - entry.offset || (!zlib && entry.stored != entry.size)) {
                return make_error(ErrorCode::InvalidFormat, "resource archive entry " + entry.path + " lies outside");
            }
            out.entries.push_back(std::move(entry));
        }
        block = next;
    }
    return out;
}

/// The same path, ignoring the case of ASCII letters and with '\\' as '/'.
bool same_path(std::string_view a, std::string_view b) {
    const auto plain = [](char c) {
        return c == '\\' ? '/' : c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
    };
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [&](char x, char y) { return plain(x) == plain(y); });
}

}  // namespace

Result<ResourceArchive> ResourceArchive::open(const std::filesystem::path& path) {
    std::error_code ec;
    const std::uintmax_t size = std::filesystem::file_size(path, ec);
    if (ec) {
        return make_error(ErrorCode::NotFound, path_to_utf8(path) + ": " + ec.message());
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return make_error(ErrorCode::IoError, path_to_utf8(path) + " cannot be read");
    }
    const ReadAt read_at = [&](std::uint64_t offset, std::size_t count, std::uint8_t* out) {
        if (offset > size || count > size - offset) {
            return false;
        }
        in.seekg(static_cast<std::streamoff>(offset));
        in.read(reinterpret_cast<char*>(out), static_cast<std::streamsize>(count));  // NOLINT
        return static_cast<bool>(in);
    };
    auto directory = read_directories(size, read_at);
    if (!directory) {
        return make_error(directory.error().code, path_to_utf8(path) + ": " + directory.error().message);
    }
    ResourceArchive archive;
    archive.m_path = path;
    archive.m_entries = std::move(directory->entries);
    archive.m_skipped = directory->skipped;
    return archive;
}

Result<ResourceArchive> ResourceArchive::from_bytes(std::vector<std::uint8_t> bytes) {
    const std::uint64_t size = bytes.size();
    const ReadAt read_at = [&](std::uint64_t offset, std::size_t count, std::uint8_t* out) {
        if (offset > size || count > size - offset) {
            return false;
        }
        std::memcpy(out, bytes.data() + offset, count);
        return true;
    };
    auto directory = read_directories(size, read_at);
    if (!directory) {
        return directory.error();
    }
    ResourceArchive archive;
    archive.m_bytes = std::move(bytes);
    archive.m_entries = std::move(directory->entries);
    archive.m_skipped = directory->skipped;
    return archive;
}

const ResourceEntry* ResourceArchive::find(std::string_view path) const {
    const auto it = std::find_if(m_entries.begin(), m_entries.end(),
                                 [&](const ResourceEntry& e) { return same_path(e.path, path); });
    return it == m_entries.end() ? nullptr : &*it;
}

Result<std::vector<std::uint8_t>> ResourceArchive::read(std::string_view path) const {
    const ResourceEntry* entry = find(path);
    if (entry == nullptr) {
        return make_error(ErrorCode::NotFound, std::string(path) + " is not in the archive");
    }
    if (entry->size > kMaxReadSize || entry->stored > kMaxReadSize) {
        return make_error(ErrorCode::LimitExceeded, std::string(path) + " is larger than 1 GiB");
    }
    std::vector<std::uint8_t> stored(static_cast<std::size_t>(entry->stored));
    if (m_path.empty()) {
        std::copy_n(m_bytes.begin() + static_cast<std::ptrdiff_t>(entry->offset), stored.size(), stored.begin());
    } else {
        std::ifstream in(m_path, std::ios::binary);
        in.seekg(static_cast<std::streamoff>(entry->offset));
        in.read(reinterpret_cast<char*>(stored.data()), static_cast<std::streamsize>(stored.size()));  // NOLINT
        if (!in) {
            return make_error(ErrorCode::IoError, std::string(path) + ": the archive cannot be read");
        }
    }
    if (!entry->compressed) {
        return stored;
    }
    auto inflated = zlib_inflate(stored, static_cast<std::size_t>(entry->size));
    if (!inflated) {
        return make_error(ErrorCode::InvalidFormat,
                          std::string(path) + " does not unpack: " + inflated.error().message);
    }
    return inflated;
}

}  // namespace annokit::core
