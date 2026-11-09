// SPDX-License-Identifier: GPL-3.0-or-later
#include "annokit/video/bink.hpp"

#include <algorithm>
#include <cstring>
#include <string>
#include <utility>

#include "bink2_video.hpp"
#include "bink_audio.hpp"

namespace annokit::video {

using core::ErrorCode;
using core::make_error;

namespace {

constexpr std::size_t kHeaderSize = 44;
constexpr std::uint32_t kMaxDimension = 4096;
constexpr std::uint32_t kMaxFrames = 1'000'000;
constexpr std::uint32_t kMaxTracks = 256;
/// Sound of a whole video, in samples, as for Smacker.
constexpr std::uint64_t kMaxSamples = std::uint64_t{64} << 20;

constexpr std::uint16_t kAudioStereo = 0x2000;
constexpr std::uint16_t kAudioDct = 0x1000;

std::uint32_t load_u32(std::span<const std::uint8_t> b, std::size_t at) noexcept {
    return static_cast<std::uint32_t>(b[at]) | (static_cast<std::uint32_t>(b[at + 1]) << 8) |
           (static_cast<std::uint32_t>(b[at + 2]) << 16) | (static_cast<std::uint32_t>(b[at + 3]) << 24);
}
std::uint16_t load_u16(std::span<const std::uint8_t> b, std::size_t at) noexcept {
    return static_cast<std::uint16_t>(b[at] | (b[at + 1] << 8));
}

/// The revisions that carry one more header field after the track count.
bool has_extra_field(char revision) noexcept {
    return revision == 'i' || revision == 'j' || revision == 'k' || revision == 'n';
}

/// Where the frame index starts, for a header with `tracks` audio tracks.
std::size_t index_start(char revision, std::uint32_t tracks) noexcept {
    return kHeaderSize + (has_extra_field(revision) ? 4 : 0) + std::size_t{tracks} * 12;
}

}  // namespace

Bink::Bink() = default;
Bink::Bink(Bink&&) noexcept = default;
Bink& Bink::operator=(Bink&&) noexcept = default;
Bink::~Bink() = default;

core::Result<Bink> Bink::open(std::vector<std::uint8_t> bytes) {
    Bink b;
    b.m_bytes = std::move(bytes);
    if (auto st = b.parse(b.m_bytes, b.m_bytes.size()); !st) {
        return st.error();
    }
    return b;
}

core::Result<Bink> Bink::open_file(const std::filesystem::path& path) {
    Bink b;
    b.m_file.open(path, std::ios::binary);
    if (!b.m_file) {
        return make_error(ErrorCode::NotFound, "Bink: cannot open " + path.filename().string());
    }
    b.m_file.seekg(0, std::ios::end);
    const auto end = b.m_file.tellg();
    b.m_file.seekg(0, std::ios::beg);
    if (end < 0) {
        return make_error(ErrorCode::IoError, "Bink: cannot determine the file size");
    }
    const auto size = static_cast<std::uint64_t>(end);
    // First the header, then as much as its tracks and frame index need.
    std::vector<std::uint8_t> head(static_cast<std::size_t>(std::min<std::uint64_t>(size, kHeaderSize)));
    auto read = [&](std::size_t from) {
        return b.m_file
            .read(reinterpret_cast<char*>(head.data() + from), static_cast<std::streamsize>(head.size() - from))
            .good();
    };
    if (!read(0)) {
        return make_error(ErrorCode::IoError, "Bink: cannot read the header");
    }
    if (head.size() == kHeaderSize) {
        const std::span<const std::uint8_t> h = head;
        const std::uint32_t frames = std::min(load_u32(h, 8), kMaxFrames + 1U);
        const std::uint32_t tracks = std::min(load_u32(h, 40), kMaxTracks + 1U);
        const std::uint64_t wanted = std::min<std::uint64_t>(
            size, index_start(static_cast<char>(h[3]), tracks) + (std::uint64_t{frames} + 1) * 4);
        head.resize(static_cast<std::size_t>(wanted));
        if (!read(kHeaderSize)) {
            return make_error(ErrorCode::IoError, "Bink: cannot read the frame index");
        }
    }
    if (auto st = b.parse(head, size); !st) {
        return st.error();
    }
    return b;
}

core::Status Bink::parse(std::span<const std::uint8_t> b, std::uint64_t size) {
    if (b.size() < kHeaderSize) {
        return make_error(ErrorCode::InvalidFormat, "Bink: file too short");
    }
    if (std::memcmp(b.data(), "KB2", 3) != 0) {
        return make_error(ErrorCode::InvalidFormat, std::memcmp(b.data(), "BIK", 3) == 0
                                                        ? "Bink: Bink 1 videos are not supported"
                                                        : "Bink: no KB2 signature");
    }
    auto& info = m_info;
    info.revision = static_cast<char>(b[3]);
    const std::uint64_t file_size = std::uint64_t{load_u32(b, 4)} + 8;
    info.frames = load_u32(b, 8);
    info.width = load_u32(b, 20);
    info.height = load_u32(b, 24);
    info.fps_num = load_u32(b, 28);
    info.fps_den = load_u32(b, 32);
    info.video_flags = load_u32(b, 36);
    const std::uint32_t tracks = load_u32(b, 40);
    if (info.width == 0 || info.height == 0 || info.width > kMaxDimension || info.height > kMaxDimension ||
        info.frames == 0 || info.frames > kMaxFrames) {
        return make_error(ErrorCode::InvalidFormat, "Bink: implausible size or frame count");
    }
    if (info.fps_num == 0 || info.fps_den == 0 || info.frame_us() == 0 || info.frame_us() > 1'000'000) {
        return make_error(ErrorCode::InvalidFormat, "Bink: implausible frame rate");
    }
    if (info.revision < 'g' || info.revision > 'n') {
        return make_error(ErrorCode::InvalidFormat,
                          std::string("Bink: revision ") + info.revision + " is not supported (only g to n)");
    }
    if (tracks > kMaxTracks) {
        return make_error(ErrorCode::InvalidFormat, "Bink: implausible number of audio tracks");
    }
    const std::size_t index = index_start(info.revision, tracks);
    if (b.size() < index + std::size_t{info.frames} * 4) {
        return make_error(ErrorCode::InvalidFormat, "Bink: file too short for its frame index");
    }
    // Per track: the largest decoded size, then rate and flags, then the ids.
    const std::size_t tracks_at = index - std::size_t{tracks} * 12;
    for (std::uint32_t t = 0; t < tracks; ++t) {
        BinkTrack track;
        const std::size_t at = tracks_at + std::size_t{tracks} * 4 + std::size_t{t} * 4;
        track.rate = load_u16(b, at);
        const std::uint16_t flags = load_u16(b, at + 2);
        track.stereo = (flags & kAudioStereo) != 0;
        track.dct = (flags & kAudioDct) != 0;
        track.id = load_u32(b, tracks_at + std::size_t{tracks} * 8 + std::size_t{t} * 4);
        if (track.rate == 0) {
            return make_error(ErrorCode::InvalidFormat, "Bink: audio track " + std::to_string(t) + " has no rate");
        }
        info.tracks.push_back(track);
    }

    // The frame index: each entry is a frame's start, bit 0 marks a key frame (the first frame is
    // one anyway); a frame ends where the next starts, the last one at the end of the file.
    const std::uint64_t end = std::min(file_size, size);
    m_offsets.resize(info.frames);
    m_sizes.resize(info.frames);
    m_key.resize(info.frames);
    for (std::uint32_t f = 0; f < info.frames; ++f) {
        const std::uint32_t entry = load_u32(b, index + std::size_t{f} * 4);
        const std::uint64_t pos = entry & ~1U;
        const std::uint64_t next = f + 1 < info.frames ? (load_u32(b, index + std::size_t{f + 1} * 4) & ~1U) : end;
        if (next <= pos || next > end) {
            return make_error(ErrorCode::InvalidFormat, "Bink: damaged frame index at frame " + std::to_string(f));
        }
        m_offsets[f] = pos;
        m_sizes[f] = static_cast<std::uint32_t>(next - pos);
        m_key[f] = f == 0 || (entry & 1U) != 0;
    }

    m_video = std::make_unique<bink::Bink2Video>(
        static_cast<std::int32_t>(info.width), static_cast<std::int32_t>(info.height), info.revision, info.video_flags);
    return core::Ok{};
}

core::Result<std::span<const std::uint8_t>> Bink::frame_bytes(std::uint32_t frame) const {
    if (!m_file.is_open()) {
        if (m_offsets[frame] + m_sizes[frame] > m_bytes.size()) {
            return make_error(ErrorCode::InvalidFormat, "Bink: frame " + std::to_string(frame) + " past the end");
        }
        return std::span<const std::uint8_t>(m_bytes).subspan(static_cast<std::size_t>(m_offsets[frame]),
                                                              m_sizes[frame]);
    }
    m_chunk.resize(m_sizes[frame]);
    m_file.clear();
    if (!m_file.seekg(static_cast<std::streamoff>(m_offsets[frame])) ||
        !m_file.read(reinterpret_cast<char*>(m_chunk.data()), static_cast<std::streamsize>(m_chunk.size()))) {
        return make_error(ErrorCode::IoError, "Bink: cannot read frame " + std::to_string(frame));
    }
    return std::span<const std::uint8_t>(m_chunk);
}

core::Result<std::size_t> Bink::skip_audio(std::span<const std::uint8_t> frame) const {
    std::size_t at = 0;
    for (std::size_t t = 0; t < m_info.tracks.size(); ++t) {
        if (at + 4 > frame.size()) {
            return make_error(ErrorCode::InvalidFormat, "Bink: audio packet past the frame");
        }
        const std::uint32_t size = load_u32(frame, at);
        if (size > frame.size() - at - 4) {
            return make_error(ErrorCode::InvalidFormat, "Bink: audio packet past the frame");
        }
        at += 4 + size;
    }
    return at;
}

void Bink::rewind() {
    m_position = 0;
}

core::Status Bink::next() {
    if (finished()) {
        return make_error(ErrorCode::OutOfRange, "Bink: no more frames");
    }
    const auto read = frame_bytes(m_position);
    if (!read) {
        return read.error();
    }
    const std::span<const std::uint8_t> data = *read;
    const auto at = skip_audio(data);
    if (!at) {
        return at.error();
    }
    if (auto st = m_video->decode(data.subspan(*at), m_key[m_position]); !st) {
        return make_error(st.error().code, st.error().message + " (frame " + std::to_string(m_position) + ")");
    }
    ++m_position;
    return core::Ok{};
}

void Bink::to_rgba(std::vector<std::uint8_t>& out) const {
    const std::size_t w = m_info.width;
    const std::size_t h = m_info.height;
    out.resize(w * h * 4);
    const bink::Plane& py = m_video->plane(0);
    const bink::Plane& pu = m_video->plane(1);
    const bink::Plane& pv = m_video->plane(2);
    const bink::Plane* pa = m_video->has_alpha() ? &m_video->plane(3) : nullptr;
    // BT.601 in video range, 16.16 fixed point: 1.164 (Y - 16), 1.596 V, 0.392 U + 0.813 V, 2.017 U.
    constexpr int kY = 76309;
    constexpr int kRv = 104597;
    constexpr int kGu = 25675;
    constexpr int kGv = 53279;
    constexpr int kBu = 132201;
    const auto clamp8 = [](int v) { return static_cast<std::uint8_t>(std::clamp(v >> 16, 0, 255)); };
    for (std::size_t y = 0; y < h; ++y) {
        const std::uint8_t* ly = py.data.data() + y * static_cast<std::size_t>(py.stride);
        const std::uint8_t* lu = pu.data.data() + (y / 2) * static_cast<std::size_t>(pu.stride);
        const std::uint8_t* lv = pv.data.data() + (y / 2) * static_cast<std::size_t>(pv.stride);
        const std::uint8_t* la = pa != nullptr ? pa->data.data() + y * static_cast<std::size_t>(pa->stride) : nullptr;
        std::uint8_t* dst = out.data() + y * w * 4;
        for (std::size_t x = 0; x < w; ++x) {
            const int c = (ly[x] - 16) * kY + (1 << 15);
            const int u = lu[x / 2] - 128;
            const int v = lv[x / 2] - 128;
            dst[x * 4 + 0] = clamp8(c + kRv * v);
            dst[x * 4 + 1] = clamp8(c - kGu * u - kGv * v);
            dst[x * 4 + 2] = clamp8(c + kBu * u);
            dst[x * 4 + 3] = la != nullptr ? la[x] : 255;
        }
    }
}

core::Result<audio::Pcm> Bink::audio(std::size_t track) const {
    audio::Pcm pcm;
    if (track >= m_info.tracks.size()) {
        return pcm;
    }
    const BinkTrack& t = m_info.tracks[track];
    pcm.rate = t.rate;
    pcm.channels = t.stereo ? 2 : 1;
    bink::BinkAudio decoder(t.rate, pcm.channels, t.dct, m_info.revision);
    const std::uint64_t limit =
        std::min(kMaxSamples,
                 (std::uint64_t{m_info.frames} * m_info.frame_us() / 1000U + 1000U) * t.rate * pcm.channels / 1000U);
    for (std::uint32_t f = 0; f < m_info.frames; ++f) {
        const auto read = frame_bytes(f);
        if (!read) {
            return read.error();
        }
        const std::span<const std::uint8_t> data = *read;
        std::size_t at = 0;
        for (std::size_t k = 0; k < m_info.tracks.size(); ++k) {
            if (at + 4 > data.size()) {
                return make_error(ErrorCode::InvalidFormat, "Bink: audio packet past the frame");
            }
            const std::uint32_t size = load_u32(data, at);
            if (size > data.size() - at - 4) {
                return make_error(ErrorCode::InvalidFormat, "Bink: audio packet past the frame");
            }
            if (k == track && size >= 4) {
                // A damaged packet costs its own samples, not the track.
                (void)decoder.decode(data.subspan(at + 4, size), pcm.samples);
                if (pcm.samples.size() > limit) {
                    return make_error(ErrorCode::InvalidFormat, "Bink: more sound than the video lasts");
                }
            }
            at += 4 + size;
        }
    }
    return pcm;
}

}  // namespace annokit::video
