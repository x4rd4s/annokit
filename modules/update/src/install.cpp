// SPDX-License-Identifier: GPL-3.0-or-later
#include "annokit/update/install.hpp"

#include <chrono>
#include <fstream>
#include <system_error>
#include <vector>

#include "annokit/core/file.hpp"

namespace annokit::update {

namespace fs = std::filesystem;

namespace {

using core::ErrorCode;
using core::make_error;

constexpr const char* kNew = "new";
constexpr const char* kPrevious = "previous";
constexpr const char* kDiscarded = "discarded";
constexpr const char* kFiles = "files";
constexpr const char* kTree = "tree";
constexpr const char* kVersionFile = "version";
constexpr const char* kAddedFile = "added";

core::Error fs_error(const std::string& what, const fs::path& path, const std::error_code& ec) {
    return make_error(ErrorCode::IoError, "update: " + what + " " + core::path_to_utf8(path) + ": " + ec.message());
}

fs::path relative_path(std::string_view utf8) {
    return core::path_from_utf8(utf8).make_preferred();
}

std::string read_text(const fs::path& path) {
    auto bytes = core::read_file(path);
    return bytes ? std::string(bytes->begin(), bytes->end()) : std::string();
}

core::Status write_text(const fs::path& path, const std::string& text) {
    return core::write_file(path, std::span(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
}

/// Renames `from` to `to`, making `to`'s folder first.
bool move(const fs::path& from, const fs::path& to, std::error_code& ec) {
    fs::create_directories(to.parent_path(), ec);
    if (ec) {
        return false;
    }
    fs::rename(from, to, ec);
    return !ec;
}

/// A folder in discarded/ that is not there yet.
fs::path new_discard_folder(const Installation& at) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    fs::path folder = at.work / kDiscarded / std::to_string(stamp);
    std::error_code ec;
    for (int i = 0; fs::exists(folder, ec) && i < 100; ++i) {
        folder = at.work / kDiscarded / (std::to_string(stamp) + "-" + std::to_string(i));
    }
    return folder;
}

/// Removes a folder; what cannot be removed (in use) is moved into discarded/ instead.
void remove_or_discard(const Installation& at, const fs::path& folder) {
    std::error_code ec;
    fs::remove_all(folder, ec);
    if (ec && fs::exists(folder)) {
        std::error_code moved;
        move(folder, new_discard_folder(at), moved);
    }
}

core::Status write_new_files(const fs::path& to, std::span<const PackageFile> files) {
    std::error_code ec;
    fs::remove_all(to, ec);
    for (const PackageFile& f : files) {
        if (!is_safe_path(f.path)) {
            return make_error(ErrorCode::InvalidFormat, "update: the package's path \"" + f.path + "\" is not allowed");
        }
        const fs::path path = to / relative_path(f.path);
        fs::create_directories(path.parent_path(), ec);
        if (ec) {
            return fs_error("cannot make the folder", path.parent_path(), ec);
        }
        if (auto status = core::write_file(path, f.bytes); !status) {
            return status;
        }
#ifndef _WIN32
        const fs::perms perms = f.executable ? fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec |
                                                   fs::perms::others_read | fs::perms::others_exec
                                             : fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read |
                                                   fs::perms::others_read;
        fs::permissions(path, perms, ec);
        if (ec) {
            return fs_error("cannot set the permissions of", path, ec);
        }
#endif
    }
    return core::Ok{};
}

core::Status install_by_files(const Installation& at, std::span<const PackageFile> files) {
    const fs::path staged = at.work / kNew / kFiles;
    const fs::path kept = at.work / kPrevious / kFiles;
    struct Done {
        fs::path relative;
        bool replaced = false;
    };
    std::vector<Done> done;
    std::string added;
    std::error_code ec;
    const auto undo = [&] {
        for (auto it = done.rbegin(); it != done.rend(); ++it) {
            std::error_code ignored;
            const fs::path target = at.root / it->relative;
            fs::remove(target, ignored);
            if (it->replaced) {
                fs::rename(kept / it->relative, target, ignored);
            }
        }
    };
    for (const PackageFile& f : files) {
        const fs::path relative = relative_path(f.path);
        const fs::path target = at.root / relative;
        const fs::file_status status = fs::symlink_status(target, ec);
        Done step{relative, false};
        if (fs::exists(status)) {
            if (fs::is_directory(status)) {
                undo();
                return make_error(ErrorCode::IoError, "update: " + core::path_to_utf8(target) +
                                                          " is a folder where the package has a file");
            }
            if (!move(target, kept / relative, ec)) {
                undo();
                return fs_error("cannot move aside", target, ec);
            }
            step.replaced = true;
        } else {
            added += f.path + "\n";
        }
        if (!move(staged / relative, target, ec)) {
            if (step.replaced) {
                std::error_code ignored;
                fs::rename(kept / relative, target, ignored);
            }
            undo();
            return fs_error("cannot put in place", target, ec);
        }
        done.push_back(step);
    }
    return write_text(at.work / kPrevious / kAddedFile, added);
}

core::Status install_as_tree(const Installation& at) {
    const fs::path staged = at.work / kNew / kTree;
    const fs::path kept = at.work / kPrevious / kTree;
    std::error_code ec;
    if (!move(at.root, kept, ec)) {
        return fs_error("cannot move aside", at.root, ec);
    }
    if (!move(staged, at.root, ec)) {
        std::error_code ignored;
        fs::rename(kept, at.root, ignored);
        return fs_error("cannot put in place", at.root, ec);
    }
    return core::Ok{};
}

}  // namespace

core::Status check_writable(const Installation& installation) {
    const fs::path folder =
        installation.layout == InstallLayout::Tree ? installation.root.parent_path() : installation.root;
    std::error_code ec;
    fs::create_directories(installation.work, ec);
    if (ec) {
        return fs_error("cannot make the folder", installation.work, ec);
    }
    for (const fs::path& probe : {folder / ".update-probe", installation.work / ".update-probe"}) {
        {
            std::ofstream out(probe, std::ios::binary);
            if (!out) {
                return make_error(ErrorCode::IoError,
                                  "update: cannot write in " + core::path_to_utf8(probe.parent_path()));
            }
        }
        fs::remove(probe, ec);
    }
    return core::Ok{};
}

core::Status install_files(const Installation& installation, std::span<const PackageFile> files,
                           std::string_view replaced_version) {
    if (files.empty()) {
        return make_error(ErrorCode::InvalidFormat, "update: the package holds no files");
    }
    clean_up(installation);
    const bool tree = installation.layout == InstallLayout::Tree;
    if (auto status = write_new_files(installation.work / kNew / (tree ? kTree : kFiles), files); !status) {
        clean_up(installation);
        return status;
    }
    // One previous version is kept: the one this install replaces.
    const fs::path previous = installation.work / kPrevious;
    remove_or_discard(installation, previous);
    std::error_code ec;
    fs::create_directories(previous, ec);
    if (ec) {
        clean_up(installation);
        return fs_error("cannot make the folder", previous, ec);
    }
    auto status = tree ? install_as_tree(installation) : install_by_files(installation, files);
    if (!status) {
        remove_or_discard(installation, previous);
        clean_up(installation);
        return status;
    }
    if (auto written = write_text(previous / kVersionFile, std::string(replaced_version)); !written) {
        return written;
    }
    clean_up(installation);
    return core::Ok{};
}

std::optional<std::string> previous_version(const Installation& installation) {
    const fs::path previous = installation.work / kPrevious;
    std::error_code ec;
    const bool tree = installation.layout == InstallLayout::Tree;
    if (!fs::exists(previous / (tree ? kTree : kFiles), ec) || !fs::exists(previous / kVersionFile, ec)) {
        return std::nullopt;
    }
    std::string version = read_text(previous / kVersionFile);
    while (!version.empty() && (version.back() == '\n' || version.back() == '\r' || version.back() == ' ')) {
        version.pop_back();
    }
    return version;
}

core::Status restore_previous(const Installation& installation) {
    if (!previous_version(installation)) {
        return make_error(ErrorCode::NotFound, "update: no previous version is kept");
    }
    const fs::path previous = installation.work / kPrevious;
    const fs::path discard = new_discard_folder(installation);
    std::error_code ec;
    if (installation.layout == InstallLayout::Tree) {
        if (!move(installation.root, discard / kTree, ec)) {
            return fs_error("cannot move aside", installation.root, ec);
        }
        if (!move(previous / kTree, installation.root, ec)) {
            std::error_code ignored;
            fs::rename(discard / kTree, installation.root, ignored);
            return fs_error("cannot put back", installation.root, ec);
        }
    } else {
        // The files the newer version added go; the replaced ones come back.
        const std::string added = read_text(previous / kAddedFile);
        std::size_t start = 0;
        while (start < added.size()) {
            const std::size_t end = std::min(added.find('\n', start), added.size());
            const std::string_view line = std::string_view(added).substr(start, end - start);
            start = end + 1;
            if (!is_safe_path(line)) {
                continue;
            }
            const fs::path relative = relative_path(line);
            if (fs::exists(installation.root / relative, ec) &&
                !move(installation.root / relative, discard / relative, ec)) {
                return fs_error("cannot move aside", installation.root / relative, ec);
            }
        }
        const fs::path kept = previous / kFiles;
        std::vector<fs::path> files;
        for (auto it = fs::recursive_directory_iterator(kept, ec); !ec && it != fs::recursive_directory_iterator();
             it.increment(ec)) {
            if (it->is_regular_file(ec)) {
                files.push_back(fs::relative(it->path(), kept, ec));
            }
        }
        if (ec) {
            return fs_error("cannot read", kept, ec);
        }
        for (const fs::path& relative : files) {
            const fs::path target = installation.root / relative;
            if (fs::exists(target, ec) && !move(target, discard / relative, ec)) {
                return fs_error("cannot move aside", target, ec);
            }
            if (!move(kept / relative, target, ec)) {
                return fs_error("cannot put back", target, ec);
            }
        }
    }
    remove_or_discard(installation, previous);
    clean_up(installation);
    return core::Ok{};
}

void clean_up(const Installation& installation) {
    std::error_code ec;
    fs::remove_all(installation.work / kNew, ec);
    const fs::path discarded = installation.work / kDiscarded;
    std::vector<fs::path> leftovers;
    for (auto it = fs::directory_iterator(discarded, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        leftovers.push_back(it->path());
    }
    for (const fs::path& leftover : leftovers) {
        std::error_code ignored;
        fs::remove_all(leftover, ignored);  // what is still in use stays until the next time
    }
    fs::remove(discarded, ec);  // only if empty
    fs::remove(installation.work, ec);
}

}  // namespace annokit::update
