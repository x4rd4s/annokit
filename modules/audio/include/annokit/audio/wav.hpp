// SPDX-License-Identifier: GPL-3.0-or-later
//
// RIFF WAVE files: 8- and 16-bit PCM, mono or stereo, and 4-bit stereo IMA ADPCM written with
// the PCM tag.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "annokit/core/result.hpp"

namespace annokit::audio {

/// Decoded sound: 16-bit samples, interleaved when stereo.
struct Pcm {
    std::uint32_t rate = 22050;
    std::uint8_t channels = 1;
    std::vector<std::int16_t> samples;

    [[nodiscard]] std::size_t frames() const noexcept { return channels == 0 ? 0 : samples.size() / channels; }
    /// Length in milliseconds.
    [[nodiscard]] std::uint64_t duration_ms() const noexcept {
        return rate == 0 ? 0 : std::uint64_t{frames()} * 1000 / rate;
    }
};

/// Decodes a WAVE file. Fails on damaged files and encodings it does not know.
[[nodiscard]] core::Result<Pcm> decode_wav(std::span<const std::uint8_t> bytes);

}  // namespace annokit::audio
