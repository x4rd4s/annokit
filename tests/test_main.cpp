// SPDX-License-Identifier: GPL-3.0-or-later
//
// annokit_tests [filter]
//   filter       runs the tests whose name contains this text
// Exit code: 0 all passed, 1 a test failed or nothing matched, 77 everything selected was skipped
// (ctest reports that as "Skipped", see tests/CMakeLists.txt).
#include <cstdio>
#include <cstring>
#include <exception>
#include <map>
#include <string>

#include "test_framework.hpp"

namespace {

constexpr int kAllSkipped = 77;

}  // namespace

int main(int argc, char** argv) {
    using namespace annokit::test;
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int run = 0;
    int skipped = 0;
    int failed_tests = 0;
    std::map<std::string, int> skip_reasons;  // a summary at the end
    for (const auto& test : registry()) {
        if (filter != nullptr && std::strstr(test.name, filter) == nullptr) {
            continue;
        }
        const int before = failures();
        bool skip = false;
        try {
            test.fn();
        } catch (const Skip& s) {
            std::printf("[ SKIP ] %s (%s)\n", test.name, s.reason.c_str());
            ++skip_reasons[s.reason];
            skip = true;
        } catch (const Failure&) {
            // already reported
        } catch (const std::exception& e) {
            std::fprintf(stderr, "  unexpected exception: %s\n", e.what());
            ++failures();
        } catch (...) {
            std::fprintf(stderr, "  unexpected exception\n");
            ++failures();
        }
        const bool ok = failures() == before;
        if (skip && ok) {
            ++skipped;
            continue;
        }
        // A test that failed a check before it skipped has failed.
        ++run;
        if (!ok) {
            ++failed_tests;
        }
        std::printf("[%s] %s\n", ok ? "  OK  " : " FAIL ", test.name);
    }
    if (!skip_reasons.empty()) {
        std::printf("\nskipped, by reason:\n");
        for (const auto& [reason, count] : skip_reasons) {
            std::printf("  %4d  %s\n", count, reason.c_str());
        }
    }
    std::printf("\n%d run, %d failed, %d skipped\n", run, failed_tests, skipped);
    if (failed_tests != 0) {
        return 1;
    }
    if (run == 0 && skipped == 0) {
        std::fprintf(stderr, "no test matches\n");
        return 1;
    }
    return run == 0 ? kAllSkipped : 0;
}
