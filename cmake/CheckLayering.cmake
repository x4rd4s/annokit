# SPDX-License-Identifier: GPL-3.0-or-later
#
# The module layering, checked as a ctest ("layering"). Every module's include directory is visible
# to the tests through PUBLIC links, so a stray include of a module that is not linked would compile;
# this check reads the includes of every source and header instead.
#
#   core      -> (nothing)
#   audio     -> core
#   video     -> audio, core
#   net       -> core
#   script    -> core
#   update    -> core
#   platform  -> core, SDL3
#
# Only platform may include SDL3.
#
# Run: cmake -DSOURCE_DIR=<repo> -P cmake/CheckLayering.cmake

cmake_policy(SET CMP0057 NEW)

set(allowed_core "core")
set(allowed_audio "audio;core")
set(allowed_video "video;audio;core")
set(allowed_net "net;core")
set(allowed_script "script;core")
set(allowed_update "update;core")
set(allowed_platform "platform;core;SDL3")

set(violations "")
foreach(module core audio video net script update platform)
    file(GLOB_RECURSE files "${SOURCE_DIR}/modules/${module}/*.cpp" "${SOURCE_DIR}/modules/${module}/*.hpp")
    foreach(file IN LISTS files)
        file(STRINGS "${file}" includes REGEX "^[ \t]*#[ \t]*include[ \t]*[<\"](annokit/[a-z_]+/|SDL3/)")
        foreach(line IN LISTS includes)
            string(REGEX MATCH "(annokit/([a-z_]+)/|SDL3/)" _ "${line}")
            if(CMAKE_MATCH_2)
                set(target "${CMAKE_MATCH_2}")
            else()
                set(target "SDL3")
            endif()
            if(NOT target IN_LIST allowed_${module})
                file(RELATIVE_PATH rel "${SOURCE_DIR}" "${file}")
                list(APPEND violations "${rel}: ${line} (${module} may include: ${allowed_${module}})")
            endif()
        endforeach()
    endforeach()
endforeach()

if(violations)
    list(JOIN violations "\n  " text)
    message(FATAL_ERROR "Module layering violated:\n  ${text}")
endif()
message(STATUS "Module layering OK")
