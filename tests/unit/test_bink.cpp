// SPDX-License-Identifier: GPL-3.0-or-later
// Bink 2 videos: small files built bit by bit, and the transforms of Bink
// Audio against their definitions.

#include <cmath>
#include <cstring>
#include <filesystem>
#include <numbers>
#include <vector>

#include "../test_framework.hpp"
#include "bink_audio.hpp"

#include "annokit/core/file.hpp"
#include "annokit/video/bink.hpp"

using namespace annokit;

namespace {

/// Bits from the lowest bit of each byte up, as Bink stores them.
class Bits {
public:
    void bit(std::uint32_t b) {
        if (m_used == 0) {
            m_bytes.push_back(0);
        }
        m_bytes.back() = static_cast<std::uint8_t>(m_bytes.back() | ((b & 1U) << m_used));
        m_used = (m_used + 1) % 8;
    }
    void bits(std::uint32_t v, int n) {
        for (int i = 0; i < n; ++i) {
            bit(v >> i);
        }
    }
    [[nodiscard]] const std::vector<std::uint8_t>& bytes() const noexcept { return m_bytes; }

private:
    std::vector<std::uint8_t> m_bytes;
    int m_used = 0;
};

void put32(std::vector<std::uint8_t>& v, std::size_t at, std::uint32_t x) {
    for (std::size_t i = 0; i < 4; ++i) {
        v[at + i] = static_cast<std::uint8_t>(x >> (8 * i));
    }
}
void append32(std::vector<std::uint8_t>& v, std::uint32_t x) {
    v.resize(v.size() + 4);
    put32(v, v.size() - 4, x);
}

/// The bits of an intra macroblock without AC coefficients: its luma DCs `luma_dc` above the one
/// predicted (in steps of 16, the quantiser 16 gives), chroma as predicted.
void flat_macroblock(Bits& b, std::uint32_t luma_dc) {
    b.bit(1);  // dq 0
    b.bit(1);  // luma cbp: none
    b.bit(0);  // ... not repeated
    if (luma_dc == 0) {
        b.bit(0);  // no DC differences
    } else {
        b.bit(1);
        // The first DC difference: a unary count, scaled by the quantiser's pattern (16384 / 1024).
        for (std::uint32_t i = 0; i < luma_dc; ++i) {
            b.bit(1);
        }
        b.bit(0);
        b.bit(0);  // positive
        for (int i = 1; i < 16; ++i) {
            b.bit(0);
        }
    }
    for (int c = 0; c < 2; ++c) {
        b.bit(1);  // chroma cbp kept: none
        b.bit(0);  // no DC differences
    }
}

/// A key frame of revision j from its frame flags and the bits of its two slices.
std::vector<std::uint8_t> key_frame(std::uint32_t flags, const Bits& first, const Bits& second = {}) {
    std::vector<std::uint8_t> frame;
    append32(frame, flags);
    append32(frame, 0);  // where the second slice starts, filled in below
    frame.insert(frame.end(), first.bytes().begin(), first.bytes().end());
    put32(frame, 4, static_cast<std::uint32_t>(frame.size()));
    frame.insert(frame.end(), second.bytes().begin(), second.bytes().end());
    return frame;
}

/// The picture of a 32 x 32 key frame: one flat macroblock, its luma `luma_dc` above the middle
/// grey. The second slice (from the table) is empty.
std::vector<std::uint8_t> flat_key_frame(std::uint32_t luma_dc) {
    Bits b;
    flat_macroblock(b, luma_dc);
    return key_frame(0, b);
}

/// A frame of revision j whose only macroblock is skipped (a copy of the frame before).
std::vector<std::uint8_t> skip_frame() {
    std::vector<std::uint8_t> frame;
    append32(frame, 0);
    append32(frame, 9);
    frame.push_back(0);  // block type: three 0 bits, the third entry of the LRU list (skip)
    return frame;
}

/// A Bink Audio packet of one mono DCT block at 22 050 Hz with nothing but zeros.
std::vector<std::uint8_t> silent_audio_packet() {
    Bits b;
    b.bits(0, 2);
    b.bits(0, 29);  // coefficient 0: power, mantissa, sign
    b.bits(0, 29);  // coefficient 1
    for (int i = 0; i < 23; ++i) {
        b.bits(0, 8);  // the band quantisers
    }
    for (int i = 2; i < 1024; i += 8) {
        b.bit(0);      // a run of 8
        b.bits(0, 4);  // width 0: all zero
    }
    std::vector<std::uint8_t> packet;
    append32(packet, 960 * 2);  // decoded size in bytes
    packet.insert(packet.end(), b.bytes().begin(), b.bytes().end());
    packet.resize((packet.size() + 3) / 4 * 4, 0);
    return packet;
}

/// A whole file: header ("KB2j" with its extra field), one track or none, the frame index, frames.
std::vector<std::uint8_t> bink_file(std::uint32_t width, std::uint32_t height,
                                    const std::vector<std::vector<std::uint8_t>>& pictures, bool with_audio) {
    const std::uint32_t tracks = with_audio ? 1U : 0U;
    std::vector<std::uint8_t> f(48, 0);
    std::memcpy(f.data(), "KB2j", 4);
    put32(f, 8, static_cast<std::uint32_t>(pictures.size()));
    put32(f, 20, width);
    put32(f, 24, height);
    put32(f, 28, 15);
    put32(f, 32, 1);
    put32(f, 40, tracks);
    if (with_audio) {
        append32(f, 4096);                      // largest decoded size
        append32(f, 22050U | (0x1000U << 16));  // rate, DCT
        append32(f, 0);                         // id
    }
    std::vector<std::vector<std::uint8_t>> frames;
    for (const auto& picture : pictures) {
        std::vector<std::uint8_t> d;
        if (with_audio) {
            const auto packet = silent_audio_packet();
            append32(d, static_cast<std::uint32_t>(packet.size()));
            d.insert(d.end(), packet.begin(), packet.end());
        }
        d.insert(d.end(), picture.begin(), picture.end());
        d.resize((d.size() + 3) / 4 * 4, 0);  // bit 0 of an offset is the key frame flag
        frames.push_back(std::move(d));
    }
    std::size_t at = f.size() + (pictures.size() + 1) * 4;
    for (std::size_t i = 0; i < frames.size(); ++i) {
        append32(f, static_cast<std::uint32_t>(at) | (i == 0 ? 1U : 0U));
        at += frames[i].size();
    }
    append32(f, static_cast<std::uint32_t>(at));
    for (const auto& d : frames) {
        f.insert(f.end(), d.begin(), d.end());
    }
    put32(f, 4, static_cast<std::uint32_t>(f.size() - 8));
    return f;
}

/// |correlation| of two equally long series (1: one is a multiple of the other).
double correlation(const std::vector<float>& a, const std::vector<double>& b) {
    double ab = 0;
    double aa = 0;
    double bb = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto ai = static_cast<double>(a[i]);
        ab += ai * b[i];
        aa += ai * ai;
        bb += b[i] * b[i];
    }
    return std::abs(ab) / std::sqrt(aa * bb);
}

}  // namespace

TEST_CASE("bink: header, tracks and a flat key frame") {
    auto video = video::Bink::open(bink_file(32, 32, {flat_key_frame(1)}, true));
    REQUIRE(video);
    const auto& info = video->info();
    CHECK(info.revision == 'j');
    CHECK(info.width == 32);
    CHECK(info.height == 32);
    CHECK(info.frames == 1);
    CHECK(info.frame_us() == 66666);
    REQUIRE(info.tracks.size() == 1);
    CHECK(info.tracks[0].rate == 22050);
    CHECK(info.tracks[0].dct);
    CHECK(!info.tracks[0].stereo);

    REQUIRE(video->next());
    CHECK(video->finished());
    std::vector<std::uint8_t> rgba;
    video->to_rgba(rgba);
    REQUIRE(rgba.size() == 32 * 32 * 4);
    // DC 1024 + 16 at the quantiser 16: (1040 * 8 + 32) >> 6 = 130; Y 130 is RGB 133 in video range.
    bool all = true;
    for (std::size_t i = 0; i < 32 * 32; ++i) {
        all = all && rgba[i * 4] == 133 && rgba[i * 4 + 1] == 133 && rgba[i * 4 + 2] == 133 && rgba[i * 4 + 3] == 255;
    }
    CHECK(all);
}

TEST_CASE("bink: the block flags count columns of whole macroblocks, rows of 16 pixels") {
    // 40 x 40: 7 column flags (64 pixels), 5 row flags (48 pixels). Two slices of one macroblock
    // row each.
    Bits b;
    b.bit(0);  // row flags stored as they are
    b.bits(0, 5);
    b.bit(0);  // column flags stored as they are
    b.bits(0x7F, 7);
    flat_macroblock(b, 1);
    flat_macroblock(b, 0);  // the DCs of the one to the left
    Bits second;
    flat_macroblock(second, 1);
    flat_macroblock(second, 0);
    auto video = video::Bink::open(bink_file(40, 40, {key_frame(0x10000, b, second)}, false));
    REQUIRE(video);
    REQUIRE(video->next());
    std::vector<std::uint8_t> rgba;
    video->to_rgba(rgba);
    REQUIRE(rgba.size() == 40 * 40 * 4);
    bool all = true;
    for (std::size_t i = 0; i < 40 * 40; ++i) {
        all = all && rgba[i * 4] == 133;
    }
    CHECK(all);
}

TEST_CASE("bink: a skipped macroblock keeps the frame before, rewind starts again") {
    auto video = video::Bink::open(bink_file(32, 32, {flat_key_frame(0), skip_frame()}, false));
    REQUIRE(video);
    REQUIRE(video->next());
    REQUIRE(video->next());
    std::vector<std::uint8_t> rgba;
    video->to_rgba(rgba);
    // Y 128: (128 - 16) * 1.164 = 130.
    CHECK(rgba[0] == 130);
    CHECK(rgba[(31 * 32 + 31) * 4 + 2] == 130);
    CHECK(!video->next());  // the end
    video->rewind();
    CHECK(video->position() == 0);
    CHECK(video->next());
}

TEST_CASE("bink: the audio track of a video") {
    auto video = video::Bink::open(bink_file(32, 32, {flat_key_frame(0), skip_frame()}, true));
    REQUIRE(video);
    auto pcm = video->audio(0);
    REQUIRE(pcm);
    CHECK(pcm->rate == 22050);
    CHECK(pcm->channels == 1);
    // Per block 1024 samples less the 64 of the crossfade.
    CHECK(pcm->samples.size() == 2 * 960);
    bool silent = true;
    for (const std::int16_t s : pcm->samples) {
        silent = silent && s == 0;
    }
    CHECK(silent);
    auto none = video->audio(1);
    REQUIRE(none);
    CHECK(none->samples.empty());
}

TEST_CASE("bink: damaged files fail instead of crashing") {
    CHECK(!video::Bink::open({}));
    auto file = bink_file(32, 32, {flat_key_frame(0)}, false);
    auto bink1 = file;
    std::memcpy(bink1.data(), "BIKi", 4);
    CHECK(!video::Bink::open(bink1));
    auto old = file;
    old[3] = 'f';  // floating point revisions are not supported
    CHECK(!video::Bink::open(old));
    auto huge = file;
    put32(huge, 20, 100000);
    CHECK(!video::Bink::open(huge));
    auto index = file;
    put32(index, 48, 0xFFFFFF00U);  // the first frame past the end
    CHECK(!video::Bink::open(index));
    // Cut frames decode to garbage or fail, but never read outside the file.
    for (std::size_t cut = 50; cut < file.size(); ++cut) {
        std::vector<std::uint8_t> part(file.begin(), file.begin() + static_cast<std::ptrdiff_t>(cut));
        if (auto v = video::Bink::open(part)) {
            (void)v->next();
        }
    }
    // Every byte of the frame flipped in turn.
    for (std::size_t at = 48; at < file.size(); ++at) {
        auto bad = file;
        bad[at] = static_cast<std::uint8_t>(bad[at] ^ 0xFF);
        if (auto v = video::Bink::open(bad)) {
            (void)v->next();
            (void)v->audio(0);
        }
    }
}

TEST_CASE("bink: the file is read frame by frame") {
    const auto path = std::filesystem::temp_directory_path() / "annokit-test-bink.bk2";
    REQUIRE(core::write_file(path, bink_file(32, 32, {flat_key_frame(1), skip_frame()}, true)));
    {
        auto video = video::Bink::open_file(path);
        REQUIRE(video);
        CHECK(video->info().frames == 2);
        CHECK(video->next());
        CHECK(video->next());
        auto pcm = video->audio(0);
        REQUIRE(pcm);
        CHECK(pcm->samples.size() == 2 * 960);
    }  // closes the file: Windows removes no open file
    std::error_code ec;
    std::filesystem::remove(path, ec);
    CHECK(!video::Bink::open_file(path));
}

TEST_CASE("bink: the DCT-III of Bink Audio is a sum of cosines") {
    constexpr int kBits = 10;
    constexpr int kN = 1 << kBits;
    video::bink::BinkTransform dct(kBits, true);
    for (const int k : {0, 1, 5, 100, 511, 1000}) {
        std::vector<float> data(kN + 2, 0.0f);
        data[static_cast<std::size_t>(k)] = 1.0f;
        dct.run(data.data());
        data.resize(kN);
        std::vector<double> expected(kN);
        for (int n = 0; n < kN; ++n) {
            expected[static_cast<std::size_t>(n)] = std::cos(std::numbers::pi * k * (2 * n + 1) / (2.0 * kN));
        }
        CHECK(correlation(data, expected) > 0.9999);
    }
}

TEST_CASE("bink: the real inverse DFT of Bink Audio") {
    constexpr int kBits = 11;
    constexpr int kN = 1 << kBits;
    video::bink::BinkTransform rdft(kBits, false);
    // Bin k (its real part at 2k, imaginary at 2k + 1) gives a cosine or sine of k periods.
    for (const int k : {1, 3, 200, 1000}) {
        for (const int part : {0, 1}) {
            std::vector<float> data(kN + 2, 0.0f);
            data[static_cast<std::size_t>(2 * k + part)] = 1.0f;
            rdft.run(data.data());
            data.resize(kN);
            std::vector<double> expected(kN);
            for (int n = 0; n < kN; ++n) {
                const double a = 2 * std::numbers::pi * k * n / kN;
                expected[static_cast<std::size_t>(n)] = part == 0 ? std::cos(a) : std::sin(a);
            }
            CHECK(correlation(data, expected) > 0.9999);
        }
    }
    // The packed DC term alone is flat.
    std::vector<float> dc(kN + 2, 0.0f);
    dc[0] = 1.0f;
    rdft.run(dc.data());
    bool flat = true;
    for (int n = 1; n < kN; ++n) {
        flat = flat && std::abs(dc[static_cast<std::size_t>(n)] - dc[0]) < 1e-5f;
    }
    CHECK(flat);
    CHECK(dc[0] != 0.0f);
}
