// SPDX-License-Identifier: GPL-3.0-or-later
//
// Sound files of any kind: WAVE files (wav.hpp), Ogg Vorbis and FLAC. The kind is told by the
// file's first bytes, not by its name.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "annokit/audio/wav.hpp"
#include "annokit/core/result.hpp"

namespace annokit::audio {

/// The longest sound decoded: a music track of a package may be long, but not fill the memory.
inline constexpr std::uint32_t kMaxSoundSeconds = 20 * 60;
/// The highest sample rate read (the mixer resamples any rate to the device's).
inline constexpr std::uint32_t kMaxSoundRate = 192000;

/// Decodes a WAVE, Ogg Vorbis or FLAC file to 16-bit samples. A file of more than two channels
/// keeps the first two (front left and right); one longer than kMaxSoundSeconds is cut there.
[[nodiscard]] core::Result<Pcm> decode_sound(std::span<const std::uint8_t> bytes);

/// How many bytes of a file check_sound_head() wants to see.
inline constexpr std::size_t kSoundHeadBytes = 4096;

/// Whether the first bytes of a file (up to kSoundHeadBytes) are those of a sound file
/// decode_sound() can read: the kind by its first bytes, for WAVE also the encoding
/// (check_wav_head). Ogg Vorbis and FLAC pass on their first bytes alone.
[[nodiscard]] core::Status check_sound_head(std::span<const std::uint8_t> head);

}  // namespace annokit::audio
