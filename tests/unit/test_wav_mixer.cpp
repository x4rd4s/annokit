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

std::vector<std::uint8_t> wave(std::uint16_t channels, std::uint32_t rate, std::uint16_t bits,
                               const std::vector<std::uint8_t>& samples) {
    core::ByteWriter w;
    tag(w, "RIFF");
    w.u32(static_cast<std::uint32_t>(36 + samples.size()));
    tag(w, "WAVE");
    tag(w, "fmt ");
    w.u32(16);
    w.u16(1);  // PCM
    w.u16(channels);
    w.u32(rate);
    w.u32(rate * channels * bits / 8);
    w.u16(static_cast<std::uint16_t>(channels * bits / 8));
    w.u16(bits);
    tag(w, "data");
    w.u32(static_cast<std::uint32_t>(samples.size()));
    for (const std::uint8_t b : samples) {
        w.u8(b);
    }
    return w.take();
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
    CHECK(!audio::decode_wav(wave(3, 22050, 8, {0, 0, 0})).ok());
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
