// SPDX-License-Identifier: GPL-3.0-or-later
// Helpers for the tests of the SDL layer.
#pragma once

#include <SDL3/SDL.h>

namespace annokit::test {

/// No window on screen and no sound: SDL's dummy drivers, unless SDL_VIDEO_DRIVER or
/// SDL_AUDIO_DRIVER say otherwise (SDL prefers the environment). SDL_Quit forgets hints, so every
/// test calls this before it starts SDL.
inline void use_dummy_drivers() {
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
}

}  // namespace annokit::test
