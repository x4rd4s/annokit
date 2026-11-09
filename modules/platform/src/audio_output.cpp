// SPDX-License-Identifier: GPL-3.0-or-later
#include "annokit/platform/audio_output.hpp"

#include <SDL3/SDL.h>

#include "annokit/platform/window.hpp"

namespace annokit::platform {

core::Result<std::unique_ptr<AudioOutput>> AudioOutput::open(std::uint32_t rate) {
    // A small device buffer (about 12 ms at 44.1 kHz): the game keeps its own short queue ahead of
    // it, and a large one would only delay every sound. Drivers may round it up.
    SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "512");
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        return sdl_error("SDL_InitSubSystem(audio)");
    }
    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_S16;
    spec.channels = 2;
    spec.freq = static_cast<int>(rate);
    SDL_AudioStream* stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (stream == nullptr) {
        auto error = sdl_error("SDL_OpenAudioDeviceStream");
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return error;
    }
    SDL_ResumeAudioStreamDevice(stream);
    return std::unique_ptr<AudioOutput>(new AudioOutput(stream, rate));
}

AudioOutput::~AudioOutput() {
    SDL_DestroyAudioStream(m_stream);
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

std::size_t AudioOutput::queued_frames() const noexcept {
    const int bytes = SDL_GetAudioStreamQueued(m_stream);
    return bytes > 0 ? static_cast<std::size_t>(bytes) / 4 : 0;
}

void AudioOutput::push(const std::int16_t* frames, std::size_t count) {
    if (count != 0) {
        SDL_PutAudioStreamData(m_stream, frames, static_cast<int>(count * 4));
    }
}

}  // namespace annokit::platform
