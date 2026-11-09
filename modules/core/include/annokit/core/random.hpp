// SPDX-License-Identifier: GPL-3.0-or-later
//
// A seeded random number generator that gives the same numbers on every platform, compiler and
// standard library: for simulations that must agree between computers, and for tests.
#pragma once

#include <cstdint>

namespace annokit::core {

/// PCG32 (O'Neill): small, fast, good statistics, and fully specified, so results are the same
/// with every compiler and standard library.
class Random {
public:
    constexpr Random() noexcept : Random(0x853c49e6748fea9bULL, 0xda3e39cb94b95bdbULL) {}
    constexpr Random(std::uint64_t seed, std::uint64_t stream) noexcept : m_increment((stream << 1U) | 1U) {
        next();
        m_state += seed;
        next();
    }

    constexpr std::uint32_t next() noexcept {
        const std::uint64_t old = m_state;
        m_state = old * 6364136223846793005ULL + m_increment;
        const auto shifted = static_cast<std::uint32_t>(((old >> 18U) ^ old) >> 27U);
        const auto rot = static_cast<std::uint32_t>(old >> 59U);
        return (shifted >> rot) | (shifted << ((32U - rot) & 31U));
    }

    /// Uniform in [0, n) without modulo bias; 0 for n = 0.
    constexpr std::uint32_t below(std::uint32_t n) noexcept {
        if (n == 0) {
            return 0;
        }
        const std::uint32_t limit = (0U - n) % n;  // values below this would favour small results
        for (;;) {
            const std::uint32_t r = next();
            if (r >= limit) {
                return r % n;
            }
        }
    }

    /// A generator continuing from a saved state.
    [[nodiscard]] static constexpr Random from_state(std::uint64_t state, std::uint64_t increment) noexcept {
        Random r;
        r.m_state = state;
        r.m_increment = increment | 1U;
        return r;
    }

    [[nodiscard]] constexpr std::uint64_t state() const noexcept { return m_state; }
    [[nodiscard]] constexpr std::uint64_t increment() const noexcept { return m_increment; }
    friend constexpr bool operator==(const Random&, const Random&) = default;

private:
    std::uint64_t m_state = 0;
    std::uint64_t m_increment = 1;
};

}  // namespace annokit::core
