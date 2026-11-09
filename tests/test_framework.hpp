// SPDX-License-Identifier: GPL-3.0-or-later
//
// Minimal dependency-free test framework. Can be replaced by Catch2/doctest later
// without touching the tests much (TEST_CASE / CHECK / REQUIRE have the same meaning).
#pragma once

#include <cstdio>
#include <filesystem>
#include <functional>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace annokit::test {

struct TestCase {
    const char* name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

struct Failure {};
struct Skip {
    std::string reason;
};

inline int& failures() {
    static int count = 0;
    return count;
}

/// A path in the temporary folder that belongs to this test run: two runs at the same time (two
/// presets, parallel CI jobs) do not delete each other's files.
inline std::filesystem::path temp_path(const std::string& name) {
    static const std::string run = std::to_string(std::random_device{}());
    return std::filesystem::temp_directory_path() / (name + "-" + run);
}

/// Whether `found` is the file `expected`: on Windows and macOS the file system ignores case, so a
/// path found under the name asked for need not be spelt as the file is.
inline bool same_file(const std::optional<std::filesystem::path>& found, const std::filesystem::path& expected) {
    std::error_code ec;
    return found.has_value() && std::filesystem::equivalent(*found, expected, ec);
}

inline void report(const char* file, int line, const char* expr) {
    std::fprintf(stderr, "  %s:%d: CHECK failed: %s\n", file, line, expr);
    ++failures();
}

}  // namespace annokit::test

#define ANNOKIT_CONCAT_(a, b) a##b
#define ANNOKIT_CONCAT(a, b) ANNOKIT_CONCAT_(a, b)

#define TEST_CASE(name)                                                             \
    static void ANNOKIT_CONCAT(annokit_test_, __LINE__)();                          \
    static const ::annokit::test::Registrar ANNOKIT_CONCAT(annokit_reg_, __LINE__)( \
        name, &ANNOKIT_CONCAT(annokit_test_, __LINE__));                            \
    static void ANNOKIT_CONCAT(annokit_test_, __LINE__)()

#define CHECK(expr)                                             \
    do {                                                        \
        if (!(expr))                                            \
            ::annokit::test::report(__FILE__, __LINE__, #expr); \
    } while (false)

#define REQUIRE(expr)                                           \
    do {                                                        \
        if (!(expr)) {                                          \
            ::annokit::test::report(__FILE__, __LINE__, #expr); \
            throw ::annokit::test::Failure{};                   \
        }                                                       \
    } while (false)

#define SKIP(reason)              \
    throw ::annokit::test::Skip { \
        reason                    \
    }
