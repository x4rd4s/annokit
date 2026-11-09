# SPDX-License-Identifier: GPL-3.0-or-later
# Sanitizer support. Enable with -DANNOKIT_SANITIZE=address;undefined (GCC/Clang; MSVC: address only).

set(ANNOKIT_SANITIZE "" CACHE STRING "Semicolon separated list of sanitizers (address, undefined, thread, leak)")

if(ANNOKIT_SANITIZE AND MSVC)
    if(NOT ANNOKIT_SANITIZE STREQUAL "address")
        message(FATAL_ERROR "annokit: MSVC supports only ANNOKIT_SANITIZE=address, not \"${ANNOKIT_SANITIZE}\"")
    endif()
    add_compile_options(/fsanitize=address)
    add_compile_definitions(ANNOKIT_SANITIZED=1)
    message(STATUS "annokit: sanitizers enabled: address")
elseif(ANNOKIT_SANITIZE)
    list(JOIN ANNOKIT_SANITIZE "," _annokit_san)
    add_compile_options(-fsanitize=${_annokit_san} -fno-omit-frame-pointer -fno-sanitize-recover=all)
    add_link_options(-fsanitize=${_annokit_san})
    add_compile_definitions(ANNOKIT_SANITIZED=1)
    message(STATUS "annokit: sanitizers enabled: ${_annokit_san}")
endif()
