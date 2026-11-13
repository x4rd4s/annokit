// SPDX-License-Identifier: GPL-3.0-or-later
#include "annokit/update/signature.hpp"

#include <monocypher-ed25519.h>
#include <monocypher.h>

namespace annokit::update {

namespace {

int hex_digit(char c) noexcept {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

std::string_view trim(std::string_view text) noexcept {
    const auto space = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (!text.empty() && space(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && space(text.back())) {
        text.remove_suffix(1);
    }
    return text;
}

}  // namespace

bool verify_signature(const Signature& signature, const PublicKey& key,
                      std::span<const std::uint8_t> message) noexcept {
    return crypto_ed25519_check(signature.data(), key.data(), message.data(), message.size()) == 0;
}

KeyPair key_pair_from_seed(const std::array<std::uint8_t, 32>& seed) noexcept {
    KeyPair pair;
    std::array<std::uint8_t, 32> copy = seed;  // Monocypher wipes the seed it is given
    crypto_ed25519_key_pair(pair.secret.data(), pair.key.data(), copy.data());
    return pair;
}

Signature sign(const KeyPair& pair, std::span<const std::uint8_t> message) noexcept {
    Signature signature{};
    crypto_ed25519_sign(signature.data(), pair.secret.data(), message.data(), message.size());
    return signature;
}

std::string to_hex(std::span<const std::uint8_t> bytes) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (const std::uint8_t b : bytes) {
        out.push_back(kDigits[b >> 4]);
        out.push_back(kDigits[b & 0x0F]);
    }
    return out;
}

template <std::size_t N>
std::optional<std::array<std::uint8_t, N>> from_hex(std::string_view text) noexcept {
    text = trim(text);
    if (text.size() != 2 * N) {
        return std::nullopt;
    }
    std::array<std::uint8_t, N> out{};
    for (std::size_t i = 0; i < N; ++i) {
        const int high = hex_digit(text[2 * i]);
        const int low = hex_digit(text[2 * i + 1]);
        if (high < 0 || low < 0) {
            return std::nullopt;
        }
        out[i] = static_cast<std::uint8_t>((high << 4) | low);
    }
    return out;
}

template std::optional<std::array<std::uint8_t, 32>> from_hex<32>(std::string_view) noexcept;
template std::optional<std::array<std::uint8_t, 64>> from_hex<64>(std::string_view) noexcept;

}  // namespace annokit::update
