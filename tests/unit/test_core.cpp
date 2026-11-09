// SPDX-License-Identifier: GPL-3.0-or-later
// The core module: byte reading and writing, hashes and checksums, text conversion, PNG and
// compression round trips, and case-insensitive file lookup.

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "../test_framework.hpp"

#include "annokit/core/byte_reader.hpp"
#include "annokit/core/byte_writer.hpp"
#include "annokit/core/compress.hpp"
#include "annokit/core/file.hpp"
#include "annokit/core/hash.hpp"
#include "annokit/core/png_reader.hpp"
#include "annokit/core/png_writer.hpp"
#include "annokit/core/text.hpp"

using namespace annokit;

namespace {

std::vector<std::uint8_t> bytes_of(std::string_view text) {
    return {text.begin(), text.end()};
}

}  // namespace

TEST_CASE("core: byte writer and reader agree, and reads past the end fail") {
    core::ByteWriter w;
    w.u8(0x12);
    w.u16(0x3456);
    w.u32(0x789ABCDEU);
    w.u64(0x0102030405060708ULL);
    w.i32(-5);
    const auto& data = w.data();
    REQUIRE(data.size() == 1 + 2 + 4 + 8 + 4);
    CHECK(data[1] == 0x56);  // little-endian
    core::ByteReader r(data);
    CHECK(r.u8() == 0x12);
    CHECK(r.u16() == 0x3456);
    CHECK(r.u32() == 0x789ABCDEU);
    CHECK(r.u64() == 0x0102030405060708ULL);
    CHECK(r.u32() == 0xFFFFFFFBU);
    CHECK(r.at_end());
    CHECK(!r.u8().has_value());
}

TEST_CASE("core: CRC-32, XXH64 and SHA-256 match their reference values") {
    const auto text = bytes_of("123456789");
    CHECK(core::crc32(text) == 0xCBF43926U);
    CHECK(core::xxh64({}) == 0xEF46DB3751D8E999ULL);
    const auto abc = core::sha256(bytes_of("abc"));
    const std::array<std::uint8_t, 4> head = {0xBA, 0x78, 0x16, 0xBF};
    CHECK(std::equal(head.begin(), head.end(), abc.begin()));
    CHECK(core::xxh64(text, 1) != core::xxh64(text, 2));
}

TEST_CASE("core: Windows-1252 and UTF-8 round trip") {
    const std::string cp1252 =
        "Stra\xDF"
        "e \x80";  // sharp s and the euro sign
    const std::string utf8 = core::cp1252_to_utf8(cp1252);
    CHECK(utf8 ==
          "Stra\xC3\x9F"
          "e \xE2\x82\xAC");
    CHECK(core::is_utf8(utf8));
    CHECK(!core::is_utf8("\xC3"));
    CHECK(core::utf8_to_cp1252(utf8) == cp1252);
    CHECK(core::skip_utf8_bom("\xEF\xBB\xBFx") == "x");
    CHECK(core::equals_ignore_case("Gfx/City.DAT", "gfx/city.dat"));
}

TEST_CASE("core: PNG written and read back") {
    std::vector<std::uint8_t> rgba;
    for (std::uint8_t i = 0; i < 6; ++i) {
        rgba.insert(rgba.end(), {static_cast<std::uint8_t>(i * 40), 10, 200, static_cast<std::uint8_t>(255 - i)});
    }
    const auto png = core::encode_png_rgba(3, 2, rgba);
    auto size = core::png_size(png);
    REQUIRE(size.ok());
    CHECK(size->width == 3);
    CHECK(size->height == 2);
    auto image = core::decode_png(png);
    REQUIRE(image.ok());
    CHECK(image->rgba == rgba);
    CHECK(!core::decode_png(std::span(png).first(20)).ok());
}

TEST_CASE("core: zlib round trip, or Unsupported without zlib") {
    std::vector<std::uint8_t> data(10'000);
    for (std::size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<std::uint8_t>(i % 7);
    }
    auto packed = core::zlib_deflate(data);
    if (!core::has_zlib()) {
        CHECK(!packed.ok());
        return;
    }
    REQUIRE(packed.ok());
    CHECK(packed->size() < data.size());
    auto unpacked = core::zlib_inflate(*packed, data.size());
    REQUIRE(unpacked.ok());
    CHECK(*unpacked == data);
}

TEST_CASE("core: files are found ignoring case, never outside the root") {
    namespace fs = std::filesystem;
    const fs::path root = test::temp_path("annokit-case");
    fs::create_directories(root / "GFX");
    REQUIRE(core::write_file(root / "GFX" / "City.dat", bytes_of("x")).ok());
    CHECK(test::same_file(core::find_case_insensitive(root, "gfx\\city.dat"), root / "GFX" / "City.dat"));
    CHECK(!core::find_case_insensitive(root, "../x").has_value());
    CHECK(!core::find_case_insensitive(root, "gfx/missing.dat").has_value());
    fs::remove_all(root);
}
