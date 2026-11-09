# SPDX-License-Identifier: GPL-3.0-or-later
# Files, byte readers and writers, data files, text, hashes, compression, PNG and zip, Result, a
# seeded random number generator. No dependencies besides the optional zlib.

add_library(annokit_core
    src/compress.cpp
    src/data_file.cpp
    src/file.cpp
    src/hash.cpp
    src/png_reader.cpp
    src/png_writer.cpp
    src/text.cpp
    src/zip_archive.cpp
)
add_library(annokit::core ALIAS annokit_core)

target_include_directories(annokit_core PUBLIC include)
target_compile_features(annokit_core PUBLIC cxx_std_20)
annokit_set_warnings(annokit_core)

if(ZLIB_FOUND)
    target_link_libraries(annokit_core PRIVATE ZLIB::ZLIB)
    target_compile_definitions(annokit_core PRIVATE ANNOKIT_HAVE_ZLIB=1)
endif()
