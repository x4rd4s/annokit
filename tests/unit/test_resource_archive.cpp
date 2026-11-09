// SPDX-License-Identifier: GPL-3.0-or-later
// Reading resource archives of version 2.2 (annokit/core/resource_archive.hpp), on archives built
// here.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "../test_framework.hpp"

#include "annokit/core/compress.hpp"
#include "annokit/core/resource_archive.hpp"

using namespace annokit;

namespace {

using Bytes = std::vector<std::uint8_t>;

struct File {
    std::string path;  ///< ASCII
    std::string text;
};

struct Block {
    std::uint32_t flags = 0;
    std::vector<File> files;
};

void put32(Bytes& to, std::size_t at, std::uint32_t v) {
    for (std::size_t i = 0; i < 4; ++i) {
        to[at + i] = static_cast<std::uint8_t>(v >> (8 * i));
    }
}

void put64(Bytes& to, std::size_t at, std::uint64_t v) {
    for (std::size_t i = 0; i < 8; ++i) {
        to[at + i] = static_cast<std::uint8_t>(v >> (8 * i));
    }
}

void append(Bytes& to, const Bytes& more) {
    to.insert(to.end(), more.begin(), more.end());
}

/// An archive as a packer writes it: per block the files, its directory and its header.
Bytes make_archive(const std::vector<Block>& blocks) {
    const std::string magic = "Resource File V2.2";
    Bytes out(0x318, 0);
    std::copy(magic.begin(), magic.end(), out.begin());
    std::size_t previous_next = 0x310;  // where the offset of the next block header goes
    for (const Block& block : blocks) {
        const bool zlib = (block.flags & 1U) != 0;
        Bytes directory(block.files.size() * 560, 0);
        for (std::size_t i = 0; i < block.files.size(); ++i) {
            const File& file = block.files[i];
            Bytes data(file.text.begin(), file.text.end());
            if (zlib) {
                data = core::zlib_deflate(data).value();
            }
            const std::size_t e = i * 560;
            for (std::size_t c = 0; c < file.path.size(); ++c) {
                directory[e + c * 2] = static_cast<std::uint8_t>(file.path[c]);
            }
            put64(directory, e + 520, out.size());
            put64(directory, e + 528, data.size());
            put64(directory, e + 536, file.text.size());
            append(out, data);
        }
        const std::uint64_t unpacked = directory.size();
        if (zlib) {
            directory = core::zlib_deflate(directory).value();
        }
        append(out, directory);
        const std::size_t header = out.size();
        put64(out, previous_next, header);
        out.resize(header + 32, 0);
        put32(out, header, block.flags);
        put32(out, header + 4, static_cast<std::uint32_t>(block.files.size()));
        put64(out, header + 8, directory.size());
        put64(out, header + 16, unpacked);
        previous_next = header + 24;
    }
    put64(out, previous_next, out.size());
    return out;
}

std::string text_of(const Bytes& bytes) {
    return {bytes.begin(), bytes.end()};
}

}  // namespace

TEST_CASE("resource archive: the files of every block, found by path in any case") {
    const auto archive = core::ResourceArchive::from_bytes(make_archive(
        {{0, {}}, {0, {{"data/texts.xml", "<root/>"}, {"data\\ui\\a.json", "{}"}}}, {0, {{"b.txt", ""}}}}));
    REQUIRE(archive.ok());
    REQUIRE(archive->entries().size() == 3);
    CHECK(archive->entries()[1].path == "data/ui/a.json");
    CHECK(archive->skipped_blocks() == 0);
    CHECK(text_of(archive->read("DATA/Texts.XML").value()) == "<root/>");
    CHECK(text_of(archive->read("data\\ui\\a.json").value()) == "{}");
    CHECK(archive->read("b.txt")->empty());
    CHECK(archive->find("data/missing.txt") == nullptr);
    CHECK(archive->read("data/missing.txt").error().code == core::ErrorCode::NotFound);
}

TEST_CASE("resource archive: zlib blocks are read, encrypted and deleted ones skipped") {
    std::vector<Block> blocks = {{2, {{"secret.txt", "x"}}}, {8, {{"old.txt", "y"}}}, {0, {{"plain.txt", "z"}}}};
    if (core::has_zlib()) {
        blocks.push_back({1, {{"packed.txt", std::string(3000, 'p')}}});
    }
    const auto archive = core::ResourceArchive::from_bytes(make_archive(blocks));
    REQUIRE(archive.ok());
    CHECK(archive->skipped_blocks() == 1);
    CHECK(archive->find("secret.txt") == nullptr);
    CHECK(archive->find("old.txt") == nullptr);
    CHECK(text_of(archive->read("plain.txt").value()) == "z");
    if (core::has_zlib()) {
        REQUIRE(archive->entries().size() == 2);
        CHECK(archive->entries()[1].compressed);
        CHECK(text_of(archive->read("packed.txt").value()) == std::string(3000, 'p'));
    }
}

TEST_CASE("resource archive: damaged archives are refused") {
    CHECK(!core::ResourceArchive::from_bytes({'R', 'e', 's'}).ok());
    Bytes wrong_magic = make_archive({{0, {{"a.txt", "abc"}}}});
    wrong_magic[17] = '3';
    CHECK(!core::ResourceArchive::from_bytes(wrong_magic).ok());

    // The first block header beyond the end, a chain that runs backwards, a file outside.
    Bytes beyond = make_archive({{0, {{"a.txt", "abc"}}}});
    put64(beyond, 0x310, beyond.size() - 8);
    CHECK(!core::ResourceArchive::from_bytes(beyond).ok());
    Bytes backwards = make_archive({{0, {{"a.txt", "abc"}}}});
    put64(backwards, backwards.size() - 8, 0x318);
    CHECK(!core::ResourceArchive::from_bytes(backwards).ok());
    Bytes outside = make_archive({{0, {{"a.txt", "abc"}}}});
    put64(outside, 0x318 + 3 + 520, outside.size());
    CHECK(!core::ResourceArchive::from_bytes(outside).ok());
    // A directory whose size does not match its file count.
    Bytes counted = make_archive({{0, {{"a.txt", "abc"}}}});
    put32(counted, counted.size() - 28, 2);
    CHECK(!core::ResourceArchive::from_bytes(counted).ok());
}

TEST_CASE("resource archive: read from a file on disk") {
    const auto path = std::filesystem::temp_directory_path() / "annokit_test_archive.dat";
    {
        const Bytes bytes = make_archive({{0, {{"data/a.txt", "hello"}}}});
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));  // NOLINT
    }
    const auto archive = core::ResourceArchive::open(path);
    REQUIRE(archive.ok());
    CHECK(text_of(archive->read("data/a.txt").value()) == "hello");
    std::filesystem::remove(path);
    CHECK(core::ResourceArchive::open(path).error().code == core::ErrorCode::NotFound);
}
