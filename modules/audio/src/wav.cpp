// SPDX-License-Identifier: GPL-3.0-or-later
#include "annokit/audio/wav.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <optional>
#include <string>

#include "annokit/core/byte_reader.hpp"

namespace annokit::audio {

using core::ErrorCode;
using core::make_error;

namespace {

constexpr std::uint16_t kFormatPcm = 1;
constexpr std::uint16_t kFormatMsAdpcm = 2;
constexpr std::uint16_t kFormatFloat = 3;
constexpr std::uint16_t kFormatImaAdpcm = 0x11;
constexpr std::uint16_t kFormatExtensible = 0xFFFE;
/// Channels a PCM or float file may have; the first two (front left and right) are kept.
constexpr std::uint16_t kMaxChannels = 8;

struct Format {
    std::uint16_t tag = 0;
    std::uint16_t channels = 0;
    std::uint32_t rate = 0;
    std::uint16_t block_align = 0;
    std::uint16_t bits = 0;
    /// What follows the 16 bytes of the plain format (cbSize and the rest), cut to the chunk.
    std::span<const std::uint8_t> extra;
};

bool tag_is(std::span<const std::uint8_t> bytes, std::size_t at, const char* tag) {
    return bytes.size() >= at + 4 && std::memcmp(bytes.data() + at, tag, 4) == 0;
}

std::int16_t clamp16(std::int32_t v) {
    return static_cast<std::int16_t>(std::clamp(v, -32768, 32767));
}

std::int16_t i16_at(std::span<const std::uint8_t> d, std::size_t at) {
    return static_cast<std::int16_t>(d[at] | (d[at + 1] << 8));
}

/// The sample of channel `c` in a frame of `bytes_per_sample` little-endian bytes, as 16 bits.
std::int16_t pcm_sample(std::span<const std::uint8_t> frame, std::size_t c, std::size_t bytes_per_sample,
                        bool is_float) {
    const std::uint8_t* p = frame.data() + c * bytes_per_sample;
    if (is_float) {
        double v = 0.0;
        if (bytes_per_sample == 4) {
            std::uint32_t u = 0;
            for (std::size_t i = 0; i < 4; ++i) {
                u |= std::uint32_t{p[i]} << (8 * i);
            }
            v = static_cast<double>(std::bit_cast<float>(u));
        } else {
            std::uint64_t u = 0;
            for (std::size_t i = 0; i < 8; ++i) {
                u |= std::uint64_t{p[i]} << (8 * i);
            }
            v = std::bit_cast<double>(u);
        }
        if (!std::isfinite(v)) {
            return 0;
        }
        return clamp16(static_cast<std::int32_t>(std::lround(std::clamp(v, -1.0, 1.0) * 32767.0)));
    }
    switch (bytes_per_sample) {
        case 1:
            return static_cast<std::int16_t>((static_cast<std::int32_t>(p[0]) - 128) * 256);
        case 2:
            return static_cast<std::int16_t>(p[0] | (p[1] << 8));
        default:
            // 24 and 32 bits: the two most significant bytes.
            return static_cast<std::int16_t>(p[bytes_per_sample - 2] | (p[bytes_per_sample - 1] << 8));
    }
}

core::Result<Pcm> decode_pcm(const Format& f, std::span<const std::uint8_t> data, bool is_float) {
    const bool known =
        is_float ? (f.bits == 32 || f.bits == 64) : (f.bits == 8 || f.bits == 16 || f.bits == 24 || f.bits == 32);
    if (!known) {
        return make_error(ErrorCode::Unsupported,
                          std::string("WAVE: ") + std::to_string(f.bits) + "-bit " + (is_float ? "float" : "PCM"));
    }
    const std::size_t bytes_per_sample = f.bits / 8U;
    const std::size_t frame = bytes_per_sample * f.channels;
    const std::size_t channels = std::min<std::size_t>(f.channels, 2);
    Pcm pcm;
    pcm.rate = f.rate;
    pcm.channels = static_cast<std::uint8_t>(channels);
    const std::size_t frames = data.size() / frame;
    pcm.samples.resize(frames * channels);
    for (std::size_t i = 0; i < frames; ++i) {
        const auto in = data.subspan(i * frame, frame);
        for (std::size_t c = 0; c < channels; ++c) {
            pcm.samples[i * channels + c] = pcm_sample(in, c, bytes_per_sample, is_float);
        }
    }
    return pcm;
}

// Step and index tables of IMA ADPCM (Intel/DVI), the standard ones.
constexpr std::array<std::int32_t, 89> kStep = {
    7,    8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,    25,   28,
    31,   34,    37,    41,    45,    50,    55,    60,    66,    73,    80,    88,    97,    107,  118,
    130,  143,   157,   173,   190,   209,   230,   253,   279,   307,   337,   371,   408,   449,  494,
    544,  598,   658,   724,   796,   876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878, 2066,
    2272, 2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845, 8630,
    9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
constexpr std::array<std::int32_t, 16> kIndexStep = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};

/// 4-bit stereo marked as PCM: one byte per stereo frame, high nibble left, IMA ADPCM steps
/// without the rounding term of the reference code, one stream from the start of the file.
Pcm decode_music4(const Format& f, std::span<const std::uint8_t> data) {
    Pcm pcm;
    pcm.rate = f.rate;
    pcm.channels = 2;
    pcm.samples.resize(data.size() * 2);
    std::array<std::int32_t, 2> predictor{};
    std::array<std::int32_t, 2> index{};
    for (std::size_t i = 0; i < data.size(); ++i) {
        for (std::size_t c = 0; c < 2; ++c) {
            const std::uint32_t n = c == 0 ? data[i] >> 4 : data[i] & 0x0FU;
            const std::int32_t step = kStep[static_cast<std::size_t>(index[c])];
            index[c] = std::clamp(index[c] + kIndexStep[n], 0, 88);
            std::int32_t diff =
                (((n & 4U) != 0 ? 4 * step : 0) + ((n & 2U) != 0 ? 2 * step : 0) + ((n & 1U) != 0 ? step : 0)) >> 2;
            if ((n & 8U) != 0) {
                diff = -diff;
            }
            predictor[c] = std::clamp(predictor[c] + diff, -32768, 32767);
            pcm.samples[2 * i + c] = static_cast<std::int16_t>(predictor[c]);
        }
    }
    return pcm;
}

/// One channel's state of IMA ADPCM, with the rounding term of the reference code.
struct ImaChannel {
    std::int32_t predictor = 0;
    std::int32_t index = 0;

    std::int16_t next(std::uint32_t n) {
        const std::int32_t step = kStep[static_cast<std::size_t>(index)];
        std::int32_t diff = step >> 3;
        if ((n & 4U) != 0) {
            diff += step;
        }
        if ((n & 2U) != 0) {
            diff += step >> 1;
        }
        if ((n & 1U) != 0) {
            diff += step >> 2;
        }
        predictor = std::clamp(predictor + ((n & 8U) != 0 ? -diff : diff), -32768, 32767);
        index = std::clamp(index + kIndexStep[n], 0, 88);
        return static_cast<std::int16_t>(predictor);
    }
};

/// IMA ADPCM (Intel/DVI) in blocks: per channel a header of the first sample and the step index,
/// then groups of four bytes (eight samples, low nibble first) per channel in turn.
core::Result<Pcm> decode_ima_adpcm(const Format& f, std::span<const std::uint8_t> data) {
    const std::size_t channels = f.channels;
    const std::size_t header = 4 * channels;
    if (f.bits != 4 || f.block_align <= header || (f.block_align - header) % (4 * channels) != 0) {
        return make_error(ErrorCode::Unsupported, "WAVE: IMA ADPCM with blocks of " + std::to_string(f.block_align) +
                                                      " bytes, " + std::to_string(f.bits) + " bit");
    }
    Pcm pcm;
    pcm.rate = f.rate;
    pcm.channels = static_cast<std::uint8_t>(channels);
    const std::size_t per_block = 1 + (f.block_align - header) * 2 / channels;
    pcm.samples.reserve((data.size() / f.block_align + 1) * per_block * channels);
    std::vector<std::int16_t> block(per_block * channels);
    for (std::size_t at = 0; at + header <= data.size(); at += f.block_align) {
        // The last block may be short.
        const auto in = data.subspan(at, std::min<std::size_t>(f.block_align, data.size() - at));
        std::array<ImaChannel, 2> ch{};
        for (std::size_t c = 0; c < channels; ++c) {
            ch[c].predictor = i16_at(in, 4 * c);
            ch[c].index = std::min<std::int32_t>(in[4 * c + 2], 88);
            block[c] = static_cast<std::int16_t>(ch[c].predictor);
        }
        std::size_t frames = 1;
        for (std::size_t g = header; g + 4 * channels <= in.size(); g += 4 * channels) {
            for (std::size_t c = 0; c < channels; ++c) {
                for (std::size_t b = 0; b < 4; ++b) {
                    const std::uint8_t byte = in[g + 4 * c + b];
                    block[(frames + 2 * b) * channels + c] = ch[c].next(byte & 0x0FU);
                    block[(frames + 2 * b + 1) * channels + c] = ch[c].next(byte >> 4);
                }
            }
            frames += 8;
        }
        pcm.samples.insert(pcm.samples.end(), block.begin(),
                           block.begin() + static_cast<std::ptrdiff_t>(frames * channels));
    }
    return pcm;
}

/// The standard coefficients of Microsoft ADPCM, used when the format names none.
constexpr std::array<std::array<std::int32_t, 2>, 7> kMsCoefficients = {
    {{256, 0}, {512, -256}, {0, 0}, {192, 64}, {240, 0}, {460, -208}, {392, -232}}};
constexpr std::array<std::int32_t, 16> kMsAdaptation = {230, 230, 230, 230, 307, 409, 512, 614,
                                                        768, 614, 512, 409, 307, 230, 230, 230};

/// Microsoft ADPCM in blocks: per channel a coefficient number, a delta and two samples (the
/// later one first), then one nibble per sample, high nibble first, channels in turn.
core::Result<Pcm> decode_ms_adpcm(const Format& f, std::span<const std::uint8_t> data) {
    const std::size_t channels = f.channels;
    const std::size_t header = 7 * channels;
    if (f.bits != 4 || f.block_align <= header) {
        return make_error(ErrorCode::Unsupported, "WAVE: Microsoft ADPCM with blocks of " +
                                                      std::to_string(f.block_align) + " bytes, " +
                                                      std::to_string(f.bits) + " bit");
    }
    std::vector<std::array<std::int32_t, 2>> coefficients(kMsCoefficients.begin(), kMsCoefficients.end());
    if (f.extra.size() >= 6) {
        // cbSize, samples per block, the number of coefficient pairs and the pairs.
        const auto count = static_cast<std::size_t>(f.extra[4] | (f.extra[5] << 8));
        if (count > 0 && f.extra.size() >= 6 + 4 * count) {
            coefficients.resize(count);
            for (std::size_t i = 0; i < count; ++i) {
                coefficients[i] = {i16_at(f.extra, 6 + 4 * i), i16_at(f.extra, 8 + 4 * i)};
            }
        }
    }
    Pcm pcm;
    pcm.rate = f.rate;
    pcm.channels = static_cast<std::uint8_t>(channels);
    const std::size_t per_block = 2 + (f.block_align - header) * 2 / channels;
    pcm.samples.reserve((data.size() / f.block_align + 1) * per_block * channels);
    for (std::size_t at = 0; at + header <= data.size(); at += f.block_align) {
        const auto in = data.subspan(at, std::min<std::size_t>(f.block_align, data.size() - at));
        std::array<std::int32_t, 2> c1{};
        std::array<std::int32_t, 2> c2{};
        std::array<std::int32_t, 2> delta{};
        std::array<std::int32_t, 2> s1{};
        std::array<std::int32_t, 2> s2{};
        for (std::size_t c = 0; c < channels; ++c) {
            const std::size_t k = std::min<std::size_t>(in[c], coefficients.size() - 1);
            c1[c] = coefficients[k][0];
            c2[c] = coefficients[k][1];
            delta[c] = i16_at(in, channels + 2 * c);
            s1[c] = i16_at(in, 3 * channels + 2 * c);
            s2[c] = i16_at(in, 5 * channels + 2 * c);
        }
        for (std::size_t c = 0; c < channels; ++c) {
            pcm.samples.push_back(static_cast<std::int16_t>(s2[c]));
        }
        for (std::size_t c = 0; c < channels; ++c) {
            pcm.samples.push_back(static_cast<std::int16_t>(s1[c]));
        }
        std::size_t c = 0;
        for (std::size_t i = header; i < in.size(); ++i) {
            for (const std::uint32_t n : {std::uint32_t{in[i]} >> 4, std::uint32_t{in[i]} & 0x0FU}) {
                const std::int32_t sn = n >= 8 ? static_cast<std::int32_t>(n) - 16 : static_cast<std::int32_t>(n);
                const std::int32_t predicted = (s1[c] * c1[c] + s2[c] * c2[c]) / 256;
                const std::int16_t sample = clamp16(predicted + sn * delta[c]);
                s2[c] = s1[c];
                s1[c] = sample;
                delta[c] = std::max((kMsAdaptation[n] * delta[c]) / 256, 16);
                pcm.samples.push_back(sample);
                c = (c + 1) % channels;
            }
        }
        // Whole frames only (a block with an odd byte count for stereo cannot happen, but a
        // damaged one may).
        pcm.samples.resize(pcm.samples.size() - pcm.samples.size() % channels);
    }
    return pcm;
}

/// The encoding of a format: the sub-format of WAVE_FORMAT_EXTENSIBLE, else the tag.
std::uint16_t encoding_of(const Format& f) {
    if (f.tag == kFormatExtensible && f.extra.size() >= 2 + 22) {
        // cbSize, valid bits, channel mask, then the GUID whose first two bytes are the tag.
        return static_cast<std::uint16_t>(f.extra[8] | (f.extra[9] << 8));
    }
    return f.tag;
}

/// The format and data chunks of a file; either may be missing.
struct Chunks {
    std::optional<Format> format;
    std::optional<std::span<const std::uint8_t>> data;
};

core::Result<Chunks> read_chunks(std::span<const std::uint8_t> bytes) {
    if (!tag_is(bytes, 0, "RIFF") || !tag_is(bytes, 8, "WAVE")) {
        return make_error(ErrorCode::InvalidFormat, "not a RIFF WAVE file");
    }
    core::ByteReader r(bytes);
    r.seek(12);
    Chunks out;
    while (r.remaining() >= 8) {
        const std::size_t at = r.offset();
        r.skip(4);
        const auto size = r.u32();
        if (!size) {
            break;
        }
        // A data chunk running past the end (some writers leave the size open) is cut to the file.
        const std::size_t n = std::min<std::size_t>(*size, r.remaining());
        const auto body = bytes.subspan(r.offset(), n);
        if (tag_is(bytes, at, "fmt ") && n >= 16) {
            core::ByteReader fr(body);
            Format f;
            f.tag = fr.u16().value_or(0);
            f.channels = fr.u16().value_or(0);
            f.rate = fr.u32().value_or(0);
            fr.skip(4);  // bytes per second
            f.block_align = fr.u16().value_or(0);
            f.bits = fr.u16().value_or(0);
            f.extra = body.subspan(16);
            out.format = f;
        } else if (tag_is(bytes, at, "data")) {
            out.data = body;
        }
        r.skip(n + (n & 1U));  // chunks are padded to even sizes
    }
    return out;
}

/// Whether the format can be decoded; the error says why not.
core::Status check_format(const Format& f) {
    const std::uint16_t encoding = encoding_of(f);
    const bool adpcm =
        encoding == kFormatMsAdpcm || encoding == kFormatImaAdpcm || (encoding == kFormatPcm && f.bits == 4);
    if (f.channels == 0 || f.rate == 0 || f.channels > (adpcm ? 2 : kMaxChannels)) {
        return make_error(ErrorCode::Unsupported,
                          "WAVE: " + std::to_string(f.channels) + " channels at " + std::to_string(f.rate) + " Hz");
    }
    switch (encoding) {
        case kFormatPcm:
            if (f.bits == 4 ? f.channels == 2 : (f.bits == 8 || f.bits == 16 || f.bits == 24 || f.bits == 32)) {
                return core::Ok{};
            }
            break;
        case kFormatFloat:
            if (f.bits == 32 || f.bits == 64) {
                return core::Ok{};
            }
            break;
        case kFormatMsAdpcm:
            if (f.bits == 4 && f.block_align > 7 * f.channels) {
                return core::Ok{};
            }
            break;
        case kFormatImaAdpcm:
            if (f.bits == 4 && f.block_align > 4 * f.channels &&
                (f.block_align - 4 * f.channels) % (4 * f.channels) == 0) {
                return core::Ok{};
            }
            break;
        default:
            break;
    }
    return make_error(ErrorCode::Unsupported,
                      "WAVE: encoding " + std::to_string(encoding) + ", " + std::to_string(f.bits) + " bit");
}

}  // namespace

core::Status check_wav_head(std::span<const std::uint8_t> head) {
    auto chunks = read_chunks(head);
    if (!chunks) {
        return chunks.error();
    }
    if (!chunks->format) {
        // The format may lie behind the bytes read; decoding tells.
        return core::Ok{};
    }
    return check_format(*chunks->format);
}

core::Result<Pcm> decode_wav(std::span<const std::uint8_t> bytes) {
    auto chunks = read_chunks(bytes);
    if (!chunks) {
        return chunks.error();
    }
    if (!chunks->format || !chunks->data) {
        return make_error(ErrorCode::InvalidFormat, "WAVE without fmt or data chunk");
    }
    const Format& f = *chunks->format;
    if (auto ok = check_format(f); !ok) {
        return ok.error();
    }
    const auto data = *chunks->data;
    switch (encoding_of(f)) {
        case kFormatMsAdpcm:
            return decode_ms_adpcm(f, data);
        case kFormatImaAdpcm:
            return decode_ima_adpcm(f, data);
        case kFormatFloat:
            return decode_pcm(f, data, true);
        default:
            return f.bits == 4 ? core::Result<Pcm>(decode_music4(f, data)) : decode_pcm(f, data, false);
    }
}

}  // namespace annokit::audio
