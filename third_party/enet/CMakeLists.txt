# SPDX-License-Identifier: MIT
#
# ENet 1.3.18 (http://enet.bespin.org, https://github.com/lsalzman/enet, tag v1.3.18): reliable UDP
# for the multiplayer (engine/net). The sources are unchanged; this file replaces ENet's own
# CMakeLists so that the checks and definitions stay on this target.

enable_language(C)

add_library(annokit_enet STATIC
    callbacks.c
    compress.c
    host.c
    list.c
    packet.c
    peer.c
    protocol.c
    unix.c
    win32.c
)
target_include_directories(annokit_enet SYSTEM PUBLIC include)
set_target_properties(annokit_enet PROPERTIES C_STANDARD 99 C_EXTENSIONS ON POSITION_INDEPENDENT_CODE ON)

if(WIN32)
    target_link_libraries(annokit_enet PUBLIC ws2_32 winmm)
    target_compile_definitions(annokit_enet PRIVATE _WINSOCK_DEPRECATED_NO_WARNINGS _CRT_SECURE_NO_WARNINGS)
else()
    include(CheckFunctionExists)
    include(CheckStructHasMember)
    include(CheckTypeSize)
    check_function_exists(fcntl ANNOKIT_ENET_HAS_FCNTL)
    check_function_exists(poll ANNOKIT_ENET_HAS_POLL)
    check_function_exists(getaddrinfo ANNOKIT_ENET_HAS_GETADDRINFO)
    check_function_exists(getnameinfo ANNOKIT_ENET_HAS_GETNAMEINFO)
    check_function_exists(gethostbyname_r ANNOKIT_ENET_HAS_GETHOSTBYNAME_R)
    check_function_exists(gethostbyaddr_r ANNOKIT_ENET_HAS_GETHOSTBYADDR_R)
    check_function_exists(inet_pton ANNOKIT_ENET_HAS_INET_PTON)
    check_function_exists(inet_ntop ANNOKIT_ENET_HAS_INET_NTOP)
    check_struct_has_member("struct msghdr" msg_flags "sys/types.h;sys/socket.h" ANNOKIT_ENET_HAS_MSGHDR_FLAGS LANGUAGE C)
    set(CMAKE_EXTRA_INCLUDE_FILES sys/types.h sys/socket.h)
    check_type_size(socklen_t ANNOKIT_ENET_HAS_SOCKLEN_T BUILTIN_TYPES_ONLY LANGUAGE C)
    unset(CMAKE_EXTRA_INCLUDE_FILES)
    foreach(feature FCNTL POLL GETADDRINFO GETNAMEINFO GETHOSTBYNAME_R GETHOSTBYADDR_R INET_PTON INET_NTOP
                    MSGHDR_FLAGS SOCKLEN_T)
        if(ANNOKIT_ENET_HAS_${feature})
            target_compile_definitions(annokit_enet PRIVATE HAS_${feature}=1)
        endif()
    endforeach()
endif()

# protocol.c computes a field's offset as `&((ENetProtocolHeader *) 0)->sentTime`, which UBSan
# reports as undefined; with -fno-sanitize-recover the first network test would stop there. Always
# off for ENet, also when a project around annokit turns UBSan on with its own option.
if(NOT MSVC)
    target_compile_options(annokit_enet PRIVATE -fno-sanitize=undefined)
endif()

# Third-party code: built quietly, never with the engine's warnings as errors.
if(MSVC)
    target_compile_options(annokit_enet PRIVATE /W0)
else()
    target_compile_options(annokit_enet PRIVATE -w)
endif()
