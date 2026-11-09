# SPDX-License-Identifier: GPL-3.0-or-later
# Transports for lockstep multiplayer: ENet over UDP, and an in-memory network with latency, jitter
# and lost links for tests. The sessions and the protocol belong to the game.

add_library(annokit_net
    src/enet_transport.cpp
    src/loopback.cpp
)
add_library(annokit::net ALIAS annokit_net)

target_include_directories(annokit_net PUBLIC include)
target_link_libraries(annokit_net PUBLIC annokit::core PRIVATE annokit_enet)
annokit_set_warnings(annokit_net)
if(WIN32)
    # enet.h pulls in windows.h: no min/max macros, and only what sockets need.
    target_compile_definitions(annokit_net PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
