// PitchMask — a 128-bit set of MIDI note numbers (value type, no allocation).

#pragma once

#include <bit>
#include <cstdint>

namespace cadenza::adaptive
{
struct PitchMask
{
    std::uint64_t lo = 0;   // notes 0..63
    std::uint64_t hi = 0;   // notes 64..127

    static constexpr bool valid(int pitch) noexcept { return pitch >= 0 && pitch < 128; }

    constexpr void set(int pitch) noexcept
    {
        if (!valid(pitch)) return;
        if (pitch < 64) lo |= (std::uint64_t { 1 } << pitch);
        else            hi |= (std::uint64_t { 1 } << (pitch - 64));
    }
    constexpr bool test(int pitch) const noexcept
    {
        if (!valid(pitch)) return false;
        return pitch < 64 ? ((lo >> pitch) & 1u) != 0 : ((hi >> (pitch - 64)) & 1u) != 0;
    }
    constexpr int count() const noexcept { return std::popcount(lo) + std::popcount(hi); }
    constexpr bool empty() const noexcept { return lo == 0 && hi == 0; }
    constexpr void clear() noexcept { lo = hi = 0; }

    // Pitch classes present (bit pc set), for octave-tolerant matching.
    constexpr std::uint16_t pitchClasses() const noexcept
    {
        std::uint16_t pcs = 0;
        for (int p = 0; p < 128; ++p)
            if (test(p)) pcs = static_cast<std::uint16_t>(pcs | (1u << (p % 12)));
        return pcs;
    }

    constexpr PitchMask operator&(const PitchMask& o) const noexcept { return { lo & o.lo, hi & o.hi }; }
    constexpr PitchMask operator|(const PitchMask& o) const noexcept { return { lo | o.lo, hi | o.hi }; }
    constexpr PitchMask without(const PitchMask& o) const noexcept { return { lo & ~o.lo, hi & ~o.hi }; }
    constexpr bool operator==(const PitchMask&) const noexcept = default;
};
}
