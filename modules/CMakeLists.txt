# SPDX-License-Identifier: GPL-3.0-or-later
#
# The modules and what each links (cmake/CheckLayering.cmake checks the includes against this):
#
#   core      (nothing)
#   audio     -> core            video  -> audio, core
#   net       -> core (+ ENet)   script -> core (+ Lua)
#   update    -> core (+ Monocypher, WinHTTP or libcurl at run time)
#   platform  -> core (+ SDL3), only with ANNOKIT_BUILD_PLATFORM
#
# Everything but platform builds and is tested without SDL.

add_subdirectory(core)
add_subdirectory(audio)
add_subdirectory(video)
add_subdirectory(net)
add_subdirectory(script)
add_subdirectory(update)
if(ANNOKIT_BUILD_PLATFORM)
    add_subdirectory(platform)
endif()
