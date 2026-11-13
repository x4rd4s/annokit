# SPDX-License-Identifier: GPL-3.0-or-later
# Updates of a program from its releases: versions, signed manifests (Ed25519 through Monocypher),
# release packages (zip, tar.gz), HTTPS downloads through the system (WinHTTP, libcurl loaded at run
# time), putting the new files in place with the previous version kept, and the restart.

add_library(annokit_update
    src/archive.cpp
    src/install.cpp
    src/manifest.cpp
    src/process.cpp
    src/signature.cpp
    src/version.cpp
)
add_library(annokit::update ALIAS annokit_update)

if(WIN32)
    target_sources(annokit_update PRIVATE src/http_winhttp.cpp)
    target_link_libraries(annokit_update PRIVATE winhttp)
    target_compile_definitions(annokit_update PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
else()
    target_sources(annokit_update PRIVATE src/http_curl.cpp)
    target_link_libraries(annokit_update PRIVATE ${CMAKE_DL_LIBS})
endif()

target_include_directories(annokit_update PUBLIC include)
target_link_libraries(annokit_update PUBLIC annokit::core PRIVATE annokit_monocypher)
annokit_set_warnings(annokit_update)
