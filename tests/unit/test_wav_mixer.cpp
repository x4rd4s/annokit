// SPDX-License-Identifier: GPL-3.0-or-later
// WAVE decoding of files built in memory, and the mixer's volume, panning and voice limits.

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include "../test_framework.hpp"

#include "annokit/audio/mixer.hpp"
#include "annokit/audio/sound_file.hpp"
#include "annokit/audio/wav.hpp"
#include "annokit/core/byte_writer.hpp"

using namespace annokit;

namespace {

void tag(core::ByteWriter& w, std::string_view t) {
    for (const char c : t) {
        w.u8(static_cast<std::uint8_t>(c));
    }
}

/// A WAVE file of encoding `format` with `extra` behind the plain format (cbSize and on).
std::vector<std::uint8_t> wave_of(std::uint16_t format, std::uint16_t channels, std::uint32_t rate, std::uint16_t bits,
                                  std::uint16_t block_align, const std::vector<std::uint8_t>& extra,
                                  const std::vector<std::uint8_t>& samples) {
    core::ByteWriter w;
    tag(w, "RIFF");
    w.u32(static_cast<std::uint32_t>(36 + extra.size() + samples.size()));
    tag(w, "WAVE");
    tag(w, "fmt ");
    w.u32(static_cast<std::uint32_t>(16 + extra.size()));
    w.u16(format);
    w.u16(channels);
    w.u32(rate);
    w.u32(rate * block_align);
    w.u16(block_align);
    w.u16(bits);
    for (const std::uint8_t b : extra) {
        w.u8(b);
    }
    tag(w, "data");
    w.u32(static_cast<std::uint32_t>(samples.size()));
    for (const std::uint8_t b : samples) {
        w.u8(b);
    }
    return w.take();
}

std::vector<std::uint8_t> wave(std::uint16_t channels, std::uint32_t rate, std::uint16_t bits,
                               const std::vector<std::uint8_t>& samples) {
    return wave_of(1, channels, rate, bits, static_cast<std::uint16_t>(channels * bits / 8), {}, samples);
}

}  // namespace

TEST_CASE("wav: 8-bit mono and 16-bit stereo PCM") {
    auto mono = audio::decode_wav(wave(1, 22050, 8, {128, 255, 0}));
    REQUIRE(mono.ok());
    CHECK(mono->channels == 1);
    CHECK(mono->rate == 22050);
    REQUIRE(mono->samples.size() == 3);
    CHECK(mono->samples[0] == 0);
    CHECK(mono->samples[1] > 32000);
    CHECK(mono->samples[2] < -32000);

    auto stereo = audio::decode_wav(wave(2, 44100, 16, {0x00, 0x10, 0x00, 0xF0}));
    REQUIRE(stereo.ok());
    CHECK(stereo->channels == 2);
    CHECK(stereo->frames() == 1);
    CHECK(stereo->samples[0] == 0x1000);
    CHECK(stereo->samples[1] == -0x1000);

    CHECK(!audio::decode_wav(std::vector<std::uint8_t>{'R', 'I', 'F', 'F'}).ok());
    // More than two channels keep front left and right; more than eight are refused.
    auto three = audio::decode_wav(wave(3, 22050, 8, {255, 0, 128}));
    REQUIRE(three.ok());
    CHECK(three->channels == 2);
    REQUIRE(three->samples.size() == 2);
    CHECK(three->samples[0] > 32000);
    CHECK(three->samples[1] < -32000);
    CHECK(!audio::decode_wav(wave(9, 22050, 8, std::vector<std::uint8_t>(9))).ok());
}

TEST_CASE("wav: 24- and 32-bit PCM, float and WAVE_FORMAT_EXTENSIBLE") {
    auto pcm24 = audio::decode_wav(wave(1, 48000, 24, {0x00, 0x34, 0x12, 0xFF, 0xFF, 0x80}));
    REQUIRE(pcm24.ok());
    REQUIRE(pcm24->samples.size() == 2);
    CHECK(pcm24->samples[0] == 0x1234);
    CHECK(pcm24->samples[1] == static_cast<std::int16_t>(0x80FF));

    auto pcm32 = audio::decode_wav(wave(1, 48000, 32, {0x00, 0x00, 0x00, 0x40}));
    REQUIRE(pcm32.ok());
    CHECK(pcm32->samples[0] == 0x4000);

    // 0.5 and -1.0 as 32-bit float.
    auto f32 = audio::decode_wav(wave_of(3, 1, 44100, 32, 4, {}, {0x00, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x80, 0xBF}));
    REQUIRE(f32.ok());
    REQUIRE(f32->samples.size() == 2);
    CHECK(f32->samples[0] == 16384);
    CHECK(f32->samples[1] == -32767);

    // Extensible: cbSize 22, valid bits, channel mask, the sub-format GUID of PCM.
    const std::vector<std::uint8_t> extensible = {22,   0,    16,   0,    3,    0,    0,    0,
                                                  0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
                                                  0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};
    auto ext = audio::decode_wav(wave_of(0xFFFE, 2, 44100, 16, 4, extensible, {0x00, 0x10, 0x00, 0xF0}));
    REQUIRE(ext.ok());
    CHECK(ext->channels == 2);
    CHECK(ext->samples[0] == 0x1000);
    CHECK(ext->samples[1] == -0x1000);
}

TEST_CASE("wav: Microsoft ADPCM") {
    // Mono, blocks of 9 bytes: coefficient 0 (256, 0), delta 16, sample 1 = 100, sample 2 = 50,
    // then the nibbles 1, 2, -1, 0.
    const std::vector<std::uint8_t> block = {0, 16, 0, 100, 0, 50, 0, 0x12, 0xF0};
    // cbSize 4: samples per block 6, no coefficients of its own (the standard ones).
    auto mono = audio::decode_wav(wave_of(2, 1, 44100, 4, 9, {4, 0, 6, 0, 0, 0}, block));
    REQUIRE(mono.ok());
    CHECK(mono->channels == 1);
    REQUIRE(mono->samples.size() == 6);
    CHECK(mono->samples[0] == 50);
    CHECK(mono->samples[1] == 100);
    CHECK(mono->samples[2] == 116);
    CHECK(mono->samples[3] == 148);
    CHECK(mono->samples[4] == 132);
    CHECK(mono->samples[5] == 132);

    // Stereo: the header fields per channel in turn; the high nibble is the left channel.
    const std::vector<std::uint8_t> stereo_block = {0, 0, 16, 0, 32, 0, 10, 0, 20, 0, 5, 0, 7, 0, 0x1F};
    auto stereo = audio::decode_wav(wave_of(2, 2, 44100, 4, 15, {}, stereo_block));
    REQUIRE(stereo.ok());
    CHECK(stereo->channels == 2);
    REQUIRE(stereo->samples.size() == 6);
    CHECK(stereo->samples[0] == 5);    // left sample 2
    CHECK(stereo->samples[1] == 7);    // right sample 2
    CHECK(stereo->samples[2] == 10);   // left sample 1
    CHECK(stereo->samples[3] == 20);   // right sample 1
    CHECK(stereo->samples[4] == 26);   // 10 + 1 * 16
    CHECK(stereo->samples[5] == -12);  // 20 - 1 * 32

    CHECK(!audio::decode_wav(wave_of(2, 1, 44100, 4, 7, {}, block)).ok());  // no room for nibbles
    CHECK(!audio::decode_wav(wave_of(2, 3, 44100, 4, 30, {}, std::vector<std::uint8_t>(30))).ok());
}

TEST_CASE("wav: IMA ADPCM") {
    // Mono, blocks of 8 bytes: first sample 1000, step index 0, then 8 nibbles, low nibble first.
    const std::vector<std::uint8_t> block = {0xE8, 0x03, 0, 0, 0x74, 0x00, 0x08, 0x00};
    auto mono = audio::decode_wav(wave_of(0x11, 1, 22050, 4, 8, {2, 0, 9, 0}, block));
    REQUIRE(mono.ok());
    REQUIRE(mono->samples.size() == 9);
    CHECK(mono->samples[0] == 1000);
    // Nibble 4 at step 7: + (0 + 7) = 1007, index 2; nibble 7 at step 9: + (1 + 9 + 4 + 2) = 1023,
    // index 10; then 0, 0 at step 19 and 17: + 2, + 2.
    CHECK(mono->samples[1] == 1007);
    CHECK(mono->samples[2] == 1023);
    CHECK(mono->samples[3] == 1025);
    CHECK(mono->samples[4] == 1027);

    CHECK(!audio::decode_wav(wave_of(0x11, 1, 22050, 4, 6, {}, block)).ok());  // not whole groups
}

TEST_CASE("wav: the head of a file tells whether it can be played") {
    CHECK(audio::check_sound_head(wave(2, 44100, 16, {0, 0, 0, 0})).ok());
    CHECK(audio::check_sound_head(wave_of(2, 2, 44100, 4, 2048, {}, {})).ok());
    CHECK(audio::check_sound_head(wave_of(0x11, 2, 44100, 4, 2048, {}, {})).ok());
    CHECK(!audio::check_sound_head(wave_of(0x55, 2, 44100, 0, 1, {}, {})).ok());  // MP3 in WAVE
    CHECK(!audio::check_sound_head(wave(2, 44100, 12, {})).ok());
    const std::vector<std::uint8_t> ogg = {'O', 'g', 'g', 'S'};
    CHECK(audio::check_sound_head(ogg).ok());
    const std::vector<std::uint8_t> text = {'h', 'e', 'l', 'l', 'o'};
    CHECK(!audio::check_sound_head(text).ok());
}

TEST_CASE("mixer: group volume, panning and the voice limit") {
    auto sound = std::make_shared<audio::Pcm>();
    sound->rate = 1000;
    sound->channels = 1;
    sound->samples.assign(1000, 10000);
    audio::Mixer mixer(1000, 2);

    std::vector<std::int16_t> out(2 * 10);
    const auto left = mixer.play(sound, {.pan = -1.0F});
    REQUIRE(left != audio::Mixer::kNoVoice);
    mixer.mix(out.data(), 10);
    CHECK(out[0] > 5000);
    CHECK(out[1] == 0);

    // A group's volume applies to sounds started after it changes.
    mixer.stop(left);
    mixer.set_group_volume(audio::Group::Effects, 0.0F);
    const auto silent = mixer.play(sound);
    mixer.mix(out.data(), 10);
    CHECK(out[0] == 0);
    mixer.stop(silent);
    mixer.set_group_volume(audio::Group::Effects, 1.0F);
    const auto again = mixer.play(sound, {.pan = -1.0F});

    const auto second = mixer.play(sound);
    const auto third = mixer.play(sound);  // takes the oldest effect's voice
    CHECK(second != audio::Mixer::kNoVoice);
    CHECK(third != audio::Mixer::kNoVoice);
    CHECK(!mixer.playing(again));
    CHECK(mixer.playing_count(audio::Group::Effects) == 2);
    mixer.stop_group(audio::Group::Effects);
    CHECK(mixer.playing_count(audio::Group::Effects) == 0);
}
