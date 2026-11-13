// SPDX-License-Identifier: GPL-3.0-or-later
#include "annokit/update/manifest.hpp"

#include <variant>

#include "annokit/core/data_file.hpp"
#include "annokit/core/text.hpp"

namespace annokit::update {

namespace {

using core::ErrorCode;
using core::make_error;

core::Error malformed(const std::string& why) {
    return make_error(ErrorCode::InvalidFormat, "update manifest: " + why);
}

/// The string or integer value of `key`, by value: nothing if it is missing or of another kind.
std::optional<std::string> text_value(const core::DataTable& table, std::string_view key) {
    const core::DataValue* v = table.find(key);
    if (v == nullptr) {
        return std::nullopt;
    }
    if (const auto* text = std::get_if<std::string>(&v->value)) {
        return *text;
    }
    return std::nullopt;
}

std::optional<std::int64_t> integer_value(const core::DataTable& table, std::string_view key) {
    const core::DataValue* v = table.find(key);
    if (v == nullptr) {
        return std::nullopt;
    }
    if (const auto* number = std::get_if<std::int64_t>(&v->value)) {
        return *number;
    }
    return std::nullopt;
}

bool is_https(std::string_view url) noexcept {
    return url.starts_with("https://") && url.size() > 8 && url.find_first_of(" \t\r\n\"<>") == std::string_view::npos;
}

}  // namespace

const ManifestPackage* Manifest::package(std::string_view platform) const noexcept {
    for (const ManifestPackage& p : packages) {
        if (p.platform == platform) {
            return &p;
        }
    }
    return nullptr;
}

core::Result<Manifest> parse_manifest(std::string_view text) {
    auto tables = core::parse_data_file(text);
    if (!tables) {
        return malformed(tables.error().message);
    }
    Manifest manifest;
    bool have_head = false;
    for (const core::DataTable& table : *tables) {
        if (table.name.empty()) {
            have_head = true;
            const auto format = integer_value(table, "format");
            if (!format) {
                return malformed("no format");
            }
            if (*format != kManifestFormat) {
                return make_error(ErrorCode::Unsupported, "update manifest: format " + std::to_string(*format) +
                                                              " (this program reads format " +
                                                              std::to_string(kManifestFormat) + ")");
            }
            const auto version = text_value(table, "version");
            const auto parsed = version ? parse_version(*version) : std::nullopt;
            if (!parsed) {
                return malformed("no version, or not one of the form 1.2.3");
            }
            manifest.version = *parsed;
            if (table.find("notes") != nullptr) {
                const auto notes = text_value(table, "notes");
                if (!notes || !is_https(*notes)) {
                    return malformed("the notes are not an https address");
                }
                manifest.notes = *notes;
            }
            continue;
        }
        ManifestPackage package;
        package.platform = table.name;
        const auto url = text_value(table, "url");
        const auto size = integer_value(table, "size");
        const auto sum = text_value(table, "sha256");
        if (!url || !is_https(*url)) {
            return malformed("[" + table.name + "]: the url is missing or not an https address");
        }
        if (!size || *size <= 0) {
            return malformed("[" + table.name + "]: the size is missing");
        }
        const auto hash = sum ? from_hex<32>(*sum) : std::nullopt;
        if (!hash) {
            return malformed("[" + table.name + "]: the sha256 is missing or not 64 hexadecimal digits");
        }
        package.url = *url;
        package.size = static_cast<std::uint64_t>(*size);
        package.sha256 = *hash;
        manifest.packages.push_back(std::move(package));
    }
    if (!have_head) {
        return malformed("no format");
    }
    return manifest;
}

core::Result<Manifest> read_signed_manifest(std::span<const std::uint8_t> bytes, std::string_view signature_text,
                                            std::span<const PublicKey> keys) {
    const auto signature = from_hex<64>(signature_text);
    if (!signature) {
        return malformed("the signature is not 128 hexadecimal digits");
    }
    bool verified = false;
    for (const PublicKey& key : keys) {
        verified = verified || verify_signature(*signature, key, bytes);
    }
    if (!verified) {
        return make_error(ErrorCode::InvalidFormat, "update manifest: the signature does not match");
    }
    const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    return parse_manifest(core::skip_utf8_bom(text));
}

}  // namespace annokit::update
