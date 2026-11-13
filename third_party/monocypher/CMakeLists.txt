# SPDX-License-Identifier: BSD-2-Clause OR CC0-1.0
#
# Monocypher 4.0.2 (https://monocypher.org, https://github.com/LoupVaillant/Monocypher, tag 4.0.2):
# Ed25519 signatures for the update manifests (annokit::update). monocypher.c and the optional
# monocypher-ed25519.c (Ed25519 with SHA-512, RFC 8032) are unchanged.

enable_language(C)

add_library(annokit_monocypher STATIC monocypher.c optional/monocypher-ed25519.c)
target_include_directories(annokit_monocypher SYSTEM PUBLIC . optional)
set_target_properties(annokit_monocypher PROPERTIES C_STANDARD 99 POSITION_INDEPENDENT_CODE ON)

# Third-party code: built quietly, never with the engine's warnings as errors.
if(MSVC)
    target_compile_options(annokit_monocypher PRIVATE /W0)
else()
    target_compile_options(annokit_monocypher PRIVATE -w)
endif()
