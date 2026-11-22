// SPDX-License-Identifier: GPL-3.0-or-later
//
// RIFF WAVE files: PCM of 8, 16, 24 or 32 bits, 32- or 64-bit float (also as
// WAVE_FORMAT_EXTENSIBLE), Microsoft ADPCM, IMA ADPCM, and 4-bit stereo IMA ADPCM written with the
// PCM tag. Mono or stereo; PCM and float of up to eight channels keep the first two (front left
// and right).
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

/// Whether the first bytes of a WAVE file name an encoding decode_wav() knows, without decoding
/// it: for a check when a file is chosen, long before it is played. Passes a head too short to hold
/// the format.
[[nodiscard]] core::Status check_wav_head(std::span<const std::uint8_t> head);

}  // namespace annokit::audio
