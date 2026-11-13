// SPDX-License-Identifier: GPL-3.0-or-later
#include "annokit/update/archive.hpp"

#include <algorithm>

#include "annokit/core/byte_reader.hpp"
#include "annokit/core/compress.hpp"
#include "annokit/core/zip_archive.hpp"

namespace annokit::update {

namespace {

using core::ErrorCode;
using core::make_error;

constexpr std::size_t kBlock = 512;

core::Error damaged(const std::string& what, const std::string& why) {
    return make_error(ErrorCode::InvalidFormat, what + ": " + why);
}

/// An octal number of a tar header field (spaces and NULs around it), or base-256 for large sizes.
std::optional<std::uint64_t> tar_number(std::span<const std::uint8_t> field) {
    if (!field.empty() && (field[0] & 0x80) != 0) {
        std::uint64_t value = field[0] & 0x7F;
        for (std::size_t i = 1; i < field.size(); ++i) {
            if (value > (std::uint64_t{1} << 55)) {
                return std::nullopt;
            }
            value = (value << 8) | field[i];
        }
        return value;
    }
    std::size_t at = 0;
    while (at < field.size() && (field[at] == ' ' || field[at] == 0)) {
        ++at;
    }
    std::uint64_t value = 0;
    std::size_t digits = 0;
    while (at < field.size() && field[at] >= '0' && field[at] <= '7') {
        if (++digits > 21) {
            return std::nullopt;
        }
        value = value * 8 + static_cast<std::uint64_t>(field[at] - '0');
        ++at;
    }
    while (at < field.size() && (field[at] == ' ' || field[at] == 0)) {
        ++at;
    }
    if (at != field.size()) {
        return std::nullopt;
    }
    return value;
}

std::string tar_text(std::span<const std::uint8_t> field) {
    const auto end = std::find(field.begin(), field.end(), std::uint8_t{0});
    return std::string(field.begin(), end);
}

/// The value of "path" in a pax extended header ("<length> path=<value>\n" records), or empty.
std::string pax_path(std::span<const std::uint8_t> data) {
    const std::string_view text(reinterpret_cast<const char*>(data.data()), data.size());
    std::string path;
    std::size_t at = 0;
    while (at < text.size()) {
        const std::size_t space = text.find(' ', at);
        if (space == std::string_view::npos) {
            break;
        }
        std::size_t length = 0;
        for (std::size_t i = at; i < space; ++i) {
            if (text[i] < '0' || text[i] > '9' || length > text.size()) {
                return path;
            }
            length = length * 10 + static_cast<std::size_t>(text[i] - '0');
        }
        if (length <= space - at || at + length > text.size()) {
            break;
        }
        const std::string_view record = text.substr(space + 1, at + length - space - 1);
        if (record.starts_with("path=") && record.ends_with('\n')) {
            path = std::string(record.substr(5, record.size() - 6));
        }
        at += length;
    }
    return path;
}

}  // namespace

bool is_safe_path(std::string_view path) noexcept {
    if (path.empty() || path.size() > 1024 || path.front() == '/') {
        return false;
    }
    for (const char c : path) {
        if (c == '\\' || c == ':' || static_cast<unsigned char>(c) < 0x20) {
            return false;
        }
    }
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t slash = std::min(path.find('/', start), path.size());
        const std::string_view part = path.substr(start, slash - start);
        if (part.empty() || part == "." || part == "..") {
            return false;
        }
        start = slash + 1;
    }
    return true;
}

core::Result<std::vector<std::uint8_t>> gunzip(std::span<const std::uint8_t> file, std::uint64_t max_size) {
    constexpr std::uint8_t kText = 1, kHeaderCrc = 2, kExtra = 4, kName = 8, kComment = 16;
    if (file.size() < 18 || file[0] != 0x1F || file[1] != 0x8B) {
        return damaged("gzip", "not a gzip file");
    }
    if (file[2] != 8) {
        return make_error(ErrorCode::Unsupported, "gzip: not packed with deflate");
    }
    const std::uint8_t flags = file[3];
    if ((flags & ~(kText | kHeaderCrc | kExtra | kName | kComment)) != 0) {
        return damaged("gzip", "unknown header flags");
    }
    std::size_t at = 10;
    const std::size_t end = file.size() - 8;
    if ((flags & kExtra) != 0) {
        if (at + 2 > end) {
            return damaged("gzip", "cut off");
        }
        at += 2 + (file[at] | (std::size_t{file[at + 1]} << 8));
    }
    for (const std::uint8_t text : {kName, kComment}) {
        if ((flags & text) != 0) {
            while (at < end && file[at] != 0) {
                ++at;
            }
            ++at;
        }
    }
    if ((flags & kHeaderCrc) != 0) {
        at += 2;
    }
    if (at > end) {
        return damaged("gzip", "cut off");
    }
    const std::uint32_t crc = core::ByteReader::load_u32(file, end);
    const std::uint32_t size = core::ByteReader::load_u32(file, end + 4);
    if (size > max_size) {
        return make_error(ErrorCode::LimitExceeded,
                          "gzip: its data is larger than " + std::to_string(max_size >> 20) + " MiB");
    }
    auto data = core::raw_inflate(file.subspan(at, end - at), size);
    if (!data) {
        return core::Error{data.error().code, "gzip: " + data.error().message};
    }
    if (core::crc32(*data) != crc) {
        return damaged("gzip", "damaged (checksum)");
    }
    return data;
}

core::Result<std::vector<PackageFile>> read_tar(std::span<const std::uint8_t> archive, const PackageLimits& limits) {
    std::vector<PackageFile> out;
    std::uint64_t total = 0;
    std::string long_name;
    std::size_t at = 0;
    while (true) {
        if (at + kBlock > archive.size()) {
            return damaged("tar archive", "cut off");
        }
        const auto header = archive.subspan(at, kBlock);
        if (std::all_of(header.begin(), header.end(), [](std::uint8_t b) { return b == 0; })) {
            break;  // the end (two zero blocks; the second is not needed)
        }
        std::uint32_t sum = 0;
        for (std::size_t i = 0; i < kBlock; ++i) {
            sum += (i >= 148 && i < 156) ? std::uint32_t{' '} : std::uint32_t{header[i]};
        }
        const auto stored_sum = tar_number(header.subspan(148, 8));
        const auto size = tar_number(header.subspan(124, 12));
        const auto mode = tar_number(header.subspan(100, 8));
        if (!stored_sum || *stored_sum != sum) {
            return damaged("tar archive", "a damaged header (checksum)");
        }
        if (!size || !mode || *size > archive.size() - at - kBlock) {
            return damaged("tar archive", "a damaged header or cut off");
        }
        const auto data = archive.subspan(at + kBlock, static_cast<std::size_t>(*size));
        at += kBlock + static_cast<std::size_t>((*size + kBlock - 1) / kBlock * kBlock);
        const char type = static_cast<char>(header[156]);
        if (type == 'L') {  // GNU: the next entry's long name
            long_name = tar_text(data);
            continue;
        }
        if (type == 'x') {  // pax: the next entry's attributes
            long_name = pax_path(data);
            continue;
        }
        if (type == 'g') {  // pax: attributes of the whole archive
            continue;
        }
        std::string path = std::move(long_name);
        long_name.clear();
        if (path.empty()) {
            path = tar_text(header.subspan(0, 100));
            const std::string magic = tar_text(header.subspan(257, 6));
            if (magic.starts_with("ustar")) {
                if (std::string prefix = tar_text(header.subspan(345, 155)); !prefix.empty()) {
                    prefix += '/';
                    path.insert(0, prefix);
                }
            }
        }
        while (path.starts_with("./")) {
            path.erase(0, 2);
        }
        if (type == '5' || path.empty() || path == ".") {
            continue;  // a folder
        }
        if (path.ends_with('/')) {
            path.pop_back();
        }
        if (type != '0' && type != 0 && type != '7') {
            return make_error(ErrorCode::Unsupported,
                              "tar archive: " + path + " is not a regular file (type " + std::string(1, type) + ")");
        }
        if (!is_safe_path(path)) {
            return damaged("tar archive", "the path \"" + path + "\" leaves the folder");
        }
        total += *size;
        if (out.size() >= limits.max_files || total > limits.max_total) {
            return make_error(ErrorCode::LimitExceeded, "tar archive: more files or bytes than a package may hold");
        }
        out.push_back(
            PackageFile{std::move(path), std::vector<std::uint8_t>(data.begin(), data.end()), (*mode & 0111) != 0});
    }
    return out;
}

core::Result<std::vector<PackageFile>> read_package(std::span<const std::uint8_t> bytes, std::string_view file_name,
                                                    const PackageLimits& limits) {
    if (file_name.ends_with(".tar.gz") || file_name.ends_with(".tgz")) {
        auto tar = gunzip(bytes, limits.max_total + limits.max_files * 3 * kBlock);
        if (!tar) {
            return tar.error();
        }
        return read_tar(*tar, limits);
    }
    if (!file_name.ends_with(".zip")) {
        return make_error(ErrorCode::Unsupported,
                          "package: " + std::string(file_name) + " is neither .zip nor .tar.gz");
    }
    auto entries = core::read_zip(bytes, core::ZipLimits{limits.max_files, limits.max_total});
    if (!entries) {
        return entries.error();
    }
    std::vector<PackageFile> out;
    out.reserve(entries->size());
    for (core::ZipEntry& e : *entries) {
        constexpr std::uint32_t kKind = 0170000;
        const std::uint32_t kind = e.unix_mode & kKind;
        if (e.unix_mode != 0 && kind != 0100000) {
            return make_error(ErrorCode::Unsupported, "zip archive: " + e.path + " is not a regular file");
        }
        if (!is_safe_path(e.path)) {
            return damaged("zip archive", "the path \"" + e.path + "\" leaves the folder");
        }
        out.push_back(PackageFile{std::move(e.path), std::move(e.bytes), (e.unix_mode & 0111) != 0});
    }
    return out;
}

core::Result<std::vector<PackageFile>> files_below(std::vector<PackageFile> files, std::string_view folder) {
    std::string prefix(folder);
    if (!prefix.empty() && !prefix.ends_with('/')) {
        prefix += '/';
    }
    std::vector<PackageFile> out;
    for (PackageFile& f : files) {
        if (f.path.starts_with(prefix) && f.path.size() > prefix.size()) {
            f.path.erase(0, prefix.size());
            out.push_back(std::move(f));
        }
    }
    if (out.empty()) {
        return make_error(ErrorCode::NotFound, "package: nothing in " + std::string(folder));
    }
    return out;
}

std::string top_folder(std::span<const PackageFile> files) {
    std::string top;
    for (const PackageFile& f : files) {
        const std::size_t slash = f.path.find('/');
        if (slash == std::string::npos) {
            return {};
        }
        const std::string_view first = std::string_view(f.path).substr(0, slash);
        if (top.empty()) {
            top = first;
        } else if (top != first) {
            return {};
        }
    }
    return top;
}

}  // namespace annokit::update
