// SPDX-License-Identifier: GPL-3.0-or-later
// Updates (annokit/update): versions, signatures, manifests, packages and installing them.

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "../test_framework.hpp"
#include "zip_support.hpp"

#include "annokit/core/compress.hpp"
#include "annokit/core/file.hpp"
#include "annokit/update/archive.hpp"
#include "annokit/update/install.hpp"
#include "annokit/update/manifest.hpp"
#include "annokit/update/signature.hpp"
#include "annokit/update/version.hpp"

using namespace annokit;
namespace fs = std::filesystem;

namespace {

std::vector<std::uint8_t> bytes_of(std::string_view text) {
    return {text.begin(), text.end()};
}

std::string text_of(const std::vector<std::uint8_t>& bytes) {
    return {bytes.begin(), bytes.end()};
}

std::string read_text(const fs::path& path) {
    auto bytes = core::read_file(path);
    return bytes ? text_of(*bytes) : std::string("<missing>");
}

void write_text(const fs::path& path, std::string_view text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

// RFC 8032, 7.1, TEST 2: one byte (0x72).
constexpr const char* kRfcSeed = "4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb";
constexpr const char* kRfcKey = "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c";
constexpr const char* kRfcSignature =
    "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da"
    "085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00";

/// A tar header block for one entry.
std::vector<std::uint8_t> tar_header(const std::string& name, std::size_t size, char type, unsigned mode) {
    std::vector<std::uint8_t> h(512, 0);
    std::copy(name.begin(), name.begin() + static_cast<std::ptrdiff_t>(std::min<std::size_t>(name.size(), 100)),
              h.begin());
    const auto octal = [&](std::size_t at, std::size_t width, std::uint64_t value) {
        char text[32];
        std::snprintf(text, sizeof(text), "%0*llo", static_cast<int>(width - 1),
                      static_cast<unsigned long long>(value));
        std::copy(text, text + width - 1, h.begin() + static_cast<std::ptrdiff_t>(at));
    };
    octal(100, 8, mode);
    octal(108, 8, 0);
    octal(116, 8, 0);
    octal(124, 12, size);
    octal(136, 12, 0);
    h[156] = static_cast<std::uint8_t>(type);
    const std::string magic = "ustar";
    std::copy(magic.begin(), magic.end(), h.begin() + 257);
    h[263] = '0';
    h[264] = '0';
    std::fill(h.begin() + 148, h.begin() + 156, std::uint8_t{' '});
    unsigned sum = 0;
    for (const std::uint8_t b : h) {
        sum += b;
    }
    char text[8];
    std::snprintf(text, sizeof(text), "%06o", sum);
    std::copy(text, text + 7, h.begin() + 148);
    return h;
}

struct TarEntry {
    std::string name;
    std::string text;
    char type = '0';
    unsigned mode = 0644;
};

std::vector<std::uint8_t> make_tar(const std::vector<TarEntry>& entries) {
    std::vector<std::uint8_t> out;
    const auto add = [&](const std::string& name, const std::string& text, char type, unsigned mode) {
        const auto h = tar_header(name, text.size(), type, mode);
        out.insert(out.end(), h.begin(), h.end());
        out.insert(out.end(), text.begin(), text.end());
        out.resize((out.size() + 511) / 512 * 512, 0);
    };
    for (const TarEntry& e : entries) {
        if (e.name.size() > 100) {
            add("././@LongLink", e.name + std::string(1, '\0'), 'L', 0);
        }
        add(e.name, e.text, e.type, e.mode);
    }
    out.resize(out.size() + 1024, 0);
    return out;
}

/// A gzip file of the data (needs zlib).
std::vector<std::uint8_t> make_gzip(const std::vector<std::uint8_t>& data) {
    const auto packed = core::zlib_deflate(data);
    std::vector<std::uint8_t> out = {0x1F, 0x8B, 8, 0, 0, 0, 0, 0, 0, 3};
    out.insert(out.end(), packed->begin() + 2, packed->end() - 4);
    const std::uint32_t crc = core::crc32(data);
    const auto size = static_cast<std::uint32_t>(data.size());
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>(crc >> (8 * i)));
    }
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>(size >> (8 * i)));
    }
    return out;
}

}  // namespace

TEST_CASE("update: versions") {
    const auto v = update::parse_version("v1.20.3");
    REQUIRE(v.has_value());
    CHECK(v->major == 1);
    CHECK(v->minor == 20);
    CHECK(v->patch == 3);
    CHECK(update::to_string(*v) == "1.20.3");
    CHECK(*update::parse_version("0.5.0") < *update::parse_version("0.5.1"));
    CHECK(*update::parse_version("0.10.0") > *update::parse_version("0.9.9"));
    CHECK(*update::parse_version("1.0.0") == *update::parse_version("v1.0.0"));
    for (const char* bad :
         {"", "1", "1.2", "1.2.3.4", "1.2.3-rc1", "1.2.x", "a1.2.3", "1..3", "1.2.3 ", "1234567.0.0"}) {
        CHECK(!update::parse_version(bad).has_value());
    }
}

TEST_CASE("update: Ed25519 signatures of RFC 8032 and hexadecimal text") {
    const auto seed = update::from_hex<32>(kRfcSeed);
    const auto key = update::from_hex<32>(kRfcKey);
    const auto signature = update::from_hex<64>(kRfcSignature);
    REQUIRE(seed && key && signature);
    const std::vector<std::uint8_t> message = {0x72};
    const update::KeyPair pair = update::key_pair_from_seed(*seed);
    CHECK(pair.key == *key);
    CHECK(update::sign(pair, message) == *signature);
    CHECK(update::verify_signature(*signature, *key, message));
    CHECK(!update::verify_signature(*signature, *key, std::vector<std::uint8_t>{0x73}));
    auto changed = *signature;
    changed[10] ^= 1;
    CHECK(!update::verify_signature(changed, *key, message));
    CHECK(update::to_hex(*key) == kRfcKey);
    CHECK(update::from_hex<32>(std::string("  ") + kRfcKey + "\n").has_value());
    CHECK(!update::from_hex<32>(std::string(kRfcKey) + "00").has_value());
    CHECK(!update::from_hex<32>(std::string(63, 'a') + "g").has_value());
}

TEST_CASE("update: manifests, signed and not") {
    const std::string text =
        "format = 1\n"
        "version = \"1.2.0\"\n"
        "notes = \"https://example.org/notes\"\n"
        "\n"
        "[windows-x64]\n"
        "url = \"https://example.org/game-1.2.0-windows-x64.zip\"\n"
        "size = 1234\n"
        "sha256 = \"" +
        std::string(64, 'a') +
        "\"\n"
        "\n"
        "[linux-x64]\n"
        "url = \"https://example.org/game-1.2.0-linux-x64.tar.gz\"\n"
        "size = 99\n"
        "sha256 = \"" +
        std::string(64, 'b') + "\"\n";
    const auto manifest = update::parse_manifest(text);
    REQUIRE(manifest.ok());
    CHECK(manifest->version == (update::Version{1, 2, 0}));
    CHECK(manifest->notes == "https://example.org/notes");
    REQUIRE(manifest->packages.size() == 2);
    const update::ManifestPackage* linux = manifest->package("linux-x64");
    REQUIRE(linux != nullptr);
    CHECK(linux->size == 99);
    CHECK(linux->sha256[0] == 0xBB);
    CHECK(manifest->package("macos-universal") == nullptr);

    CHECK(update::parse_manifest("format = 2\nversion = \"1.0.0\"\n").error().code == core::ErrorCode::Unsupported);
    CHECK(!update::parse_manifest("version = \"1.0.0\"\n").ok());
    CHECK(!update::parse_manifest("format = 1\nversion = \"1.0\"\n").ok());
    CHECK(!update::parse_manifest("format = 1\nversion = \"1.0.0\"\n[x]\nurl = \"http://example.org/a.zip\"\nsize = 1\n"
                                  "sha256 = \"" +
                                  std::string(64, 'a') + "\"\n")
               .ok());
    CHECK(!update::parse_manifest("format = 1\nversion = \"1.0.0\"\n[x]\nurl = \"https://example.org/a.zip\"\n"
                                  "size = 1\nsha256 = \"abc\"\n")
               .ok());

    const auto pair = update::key_pair_from_seed(*update::from_hex<32>(kRfcSeed));
    const auto other = update::key_pair_from_seed(std::array<std::uint8_t, 32>{7});
    const std::vector<std::uint8_t> bytes = bytes_of(text);
    const std::string signature = update::to_hex(update::sign(pair, bytes));
    const std::vector<update::PublicKey> keys = {other.key, pair.key};
    CHECK(update::read_signed_manifest(bytes, signature + "\n", keys).ok());
    CHECK(!update::read_signed_manifest(bytes, signature, std::vector<update::PublicKey>{other.key}).ok());
    auto changed = bytes;
    changed[text.find("1234")] = '9';
    CHECK(!update::read_signed_manifest(changed, signature, keys).ok());
    CHECK(!update::read_signed_manifest(bytes, "not a signature", keys).ok());
}

TEST_CASE("update: paths a package may hold") {
    CHECK(update::is_safe_path("bin/game"));
    CHECK(update::is_safe_path("Game.app/Contents/MacOS/Game"));
    for (const char* bad : {"", "/etc/passwd", "../x", "a/../../x", "a//b", "a/./b", "a/", "C:/x", "a\\b", "a\nb"}) {
        CHECK(!update::is_safe_path(bad));
    }
}

TEST_CASE("update: zip packages keep the executable bit and refuse links") {
    const auto archive = test::make_zip({{"Game-1.0/Game.app/Contents/MacOS/Game", "program", false, 0100755},
                                         {"Game-1.0/Game.app/Contents/Info.plist", "plist", false, 0100644},
                                         {"Game-1.0/LICENSE.txt", "licence", false}});
    auto files = update::read_package(archive, "Game-1.0-macos.zip");
    REQUIRE(files.ok());
    REQUIRE(files->size() == 3);
    CHECK((*files)[0].executable);
    CHECK(!(*files)[1].executable);
    CHECK(!(*files)[2].executable);
    CHECK(update::top_folder(*files) == "Game-1.0");
    auto bundle = update::files_below(std::move(files).value(), "Game-1.0/Game.app");
    REQUIRE(bundle.ok());
    REQUIRE(bundle->size() == 2);
    CHECK((*bundle)[0].path == "Contents/MacOS/Game");

    const auto link = test::make_zip({{"Game-1.0/lib", "target", false, 0120777}});
    CHECK(update::read_package(link, "a.zip").error().code == core::ErrorCode::Unsupported);
    const auto escape = test::make_zip({{"../evil", "x", false}});
    CHECK(!update::read_package(escape, "a.zip").ok());
    CHECK(!update::read_package(archive, "a.rar").ok());
}

TEST_CASE("update: tar.gz packages") {
    if (!core::has_zlib()) {
        SKIP("built without zlib");
    }
    const std::string long_name = "Game-1.0/share/" + std::string(120, 'n') + ".txt";
    const auto tar = make_tar({{"Game-1.0/", "", '5', 0755},
                               {"Game-1.0/bin/game", "program", '0', 0755},
                               {long_name, "long", '0', 0644},
                               {"./Game-1.0/share/empty", "", '0', 0644}});
    const auto gz = make_gzip(tar);
    REQUIRE(update::gunzip(gz, 1 << 20).ok());
    CHECK(update::gunzip(gz, 10).error().code == core::ErrorCode::LimitExceeded);
    auto damaged = gz;
    damaged[damaged.size() - 6] ^= 1;  // the CRC
    CHECK(!update::gunzip(damaged, 1 << 20).ok());

    const auto files = update::read_package(gz, "Game-1.0-linux-x64.tar.gz");
    REQUIRE(files.ok());
    REQUIRE(files->size() == 3);
    CHECK((*files)[0].path == "Game-1.0/bin/game");
    CHECK(text_of((*files)[0].bytes) == "program");
    CHECK((*files)[0].executable);
    CHECK((*files)[1].path == long_name);
    CHECK(!(*files)[1].executable);
    CHECK((*files)[2].path == "Game-1.0/share/empty");

    CHECK(update::read_tar(make_tar({{"Game-1.0/lib", "", '2', 0777}})).error().code == core::ErrorCode::Unsupported);
    CHECK(!update::read_tar(make_tar({{"../evil", "x", '0', 0644}})).ok());
    auto broken = tar;
    broken[600] ^= 1;  // the second header's name: its checksum no longer matches
    CHECK(!update::read_tar(broken).ok());
    CHECK(!update::read_tar(std::span(tar).first(700)).ok());
}

TEST_CASE("update: installing file by file keeps the previous version and brings it back") {
    const fs::path base = test::temp_path("annokit-update-files");
    fs::remove_all(base);
    const update::Installation at{base / "game", base / "game" / ".update", update::InstallLayout::Files};
    write_text(at.root / "bin" / "game", "old program");
    write_text(at.root / "share" / "old.txt", "only in the old version");
    write_text(at.root / "saves" / "game1.sav", "the player's");
    CHECK(update::check_writable(at).ok());
    CHECK(!update::previous_version(at).has_value());

    const std::vector<update::PackageFile> files = {{"bin/game", bytes_of("new program"), true},
                                                    {"share/new/added.txt", bytes_of("added"), false}};
    REQUIRE(update::install_files(at, files, "1.0.0").ok());
    CHECK(read_text(at.root / "bin" / "game") == "new program");
    CHECK(read_text(at.root / "share" / "new" / "added.txt") == "added");
    CHECK(read_text(at.root / "share" / "old.txt") == "only in the old version");
    CHECK(read_text(at.root / "saves" / "game1.sav") == "the player's");
#ifndef _WIN32
    CHECK((fs::status(at.root / "bin" / "game").permissions() & fs::perms::owner_exec) != fs::perms::none);
#endif
    CHECK(update::previous_version(at) == std::optional<std::string>("1.0.0"));
    CHECK(!fs::exists(at.work / "new"));

    REQUIRE(update::restore_previous(at).ok());
    CHECK(read_text(at.root / "bin" / "game") == "old program");
    CHECK(!fs::exists(at.root / "share" / "new" / "added.txt"));
    CHECK(read_text(at.root / "saves" / "game1.sav") == "the player's");
    CHECK(!update::previous_version(at).has_value());
    CHECK(!update::restore_previous(at).ok());
    update::clean_up(at);
    CHECK(!fs::exists(at.work));
    fs::remove_all(base);
}

TEST_CASE("update: a failed install leaves the old files in place") {
    const fs::path base = test::temp_path("annokit-update-undo");
    fs::remove_all(base);
    const update::Installation at{base / "game", base / "work", update::InstallLayout::Files};
    write_text(at.root / "a.txt", "old a");
    fs::create_directories(at.root / "b.txt");  // a folder where the package has a file
    const std::vector<update::PackageFile> files = {{"a.txt", bytes_of("new a"), false},
                                                    {"b.txt", bytes_of("new b"), false}};
    CHECK(!update::install_files(at, files, "1.0.0").ok());
    CHECK(read_text(at.root / "a.txt") == "old a");
    CHECK(fs::is_directory(at.root / "b.txt"));
    CHECK(!update::previous_version(at).has_value());
    CHECK(!update::install_files(at, std::vector<update::PackageFile>{{"../x", bytes_of("x"), false}}, "1").ok());
    fs::remove_all(base);
}

TEST_CASE("update: installing a tree replaces the folder as a whole") {
    const fs::path base = test::temp_path("annokit-update-tree");
    fs::remove_all(base);
    const update::Installation at{base / "Apps" / "Game.app", base / "Apps" / ".game-update",
                                  update::InstallLayout::Tree};
    write_text(at.root / "Contents" / "MacOS" / "Game", "old program");
    write_text(at.root / "Contents" / "Resources" / "gone.txt", "old only");
    const std::vector<update::PackageFile> files = {{"Contents/MacOS/Game", bytes_of("new program"), true},
                                                    {"Contents/Info.plist", bytes_of("plist"), false}};
    REQUIRE(update::install_files(at, files, "2.0.0").ok());
    CHECK(read_text(at.root / "Contents" / "MacOS" / "Game") == "new program");
    CHECK(!fs::exists(at.root / "Contents" / "Resources" / "gone.txt"));
    CHECK(update::previous_version(at) == std::optional<std::string>("2.0.0"));
    REQUIRE(update::restore_previous(at).ok());
    CHECK(read_text(at.root / "Contents" / "MacOS" / "Game") == "old program");
    CHECK(read_text(at.root / "Contents" / "Resources" / "gone.txt") == "old only");
    CHECK(!fs::exists(at.root / "Contents" / "Info.plist"));
    fs::remove_all(base);
}
