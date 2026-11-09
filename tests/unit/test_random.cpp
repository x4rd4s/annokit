// SPDX-License-Identifier: GPL-3.0-or-later
// The seeded generator: specified, reproducible, and independent per seed and stream.

#include "../test_framework.hpp"

#include "annokit/core/random.hpp"

using namespace annokit;

TEST_CASE("random: PCG32 reference sequence") {
    // pcg32_srandom(42, 54) of the reference implementation.
    core::Random r(42, 54);
    CHECK(r.next() == 0xa15c02b7U);
    CHECK(r.next() == 0x7b47f409U);
    CHECK(r.next() == 0xba1d3330U);
    CHECK(r.next() == 0x83d2f293U);
    CHECK(r.next() == 0xbfa4784bU);
    CHECK(r.next() == 0xcbed606eU);
}

TEST_CASE("random: bounded draws and independent streams") {
    core::Random r(7, 1);
    std::uint32_t counts[6] = {};
    for (int i = 0; i < 60'000; ++i) {
        const std::uint32_t v = r.below(6);
        REQUIRE(v < 6);
        ++counts[v];
    }
    for (const std::uint32_t c : counts) {
        CHECK(c > 9'500);
        CHECK(c < 10'500);
    }
    CHECK(r.below(0) == 0);
    core::Random a(5, 1);
    core::Random b(5, 2);
    CHECK(a.next() != b.next());  // another stream, other numbers
    core::Random c(5, 1);
    core::Random d(5, 1);
    for (int i = 0; i < 100; ++i) {
        CHECK(c.next() == d.next());
    }
    const core::Random saved = core::Random::from_state(c.state(), c.increment());
    CHECK(saved == c);
}
