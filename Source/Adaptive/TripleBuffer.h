// TripleBuffer — wait-free publication of a small trivially-copyable value from
// one writer thread to one reader thread (e.g. follower state -> audio thread).
//
// The writer never blocks the reader and vice versa; the reader always sees a
// complete value (no tearing) and the most recent one published. If several
// threads write, they must serialise their write() calls (e.g. under a mutex).

#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <type_traits>

namespace cadenza::adaptive
{
template <class T>
class TripleBuffer
{
    static_assert(std::is_trivially_copyable_v<T>, "TripleBuffer holds trivially copyable values");

public:
    explicit TripleBuffer(const T& initial = T {}) noexcept
    {
        m_buffers.fill(initial);
    }

    // Writer side.
    void write(const T& value) noexcept
    {
        m_buffers[m_writeIndex] = value;
        const auto previous = m_middle.exchange(static_cast<std::uint8_t>(m_writeIndex | kDirty), std::memory_order_acq_rel);
        m_writeIndex = previous & kIndexMask;
    }

    // Reader side: the latest published value.
    const T& read() noexcept
    {
        if (m_middle.load(std::memory_order_acquire) & kDirty) {
            const auto previous = m_middle.exchange(m_readIndex, std::memory_order_acq_rel);
            m_readIndex = previous & kIndexMask;
        }
        return m_buffers[m_readIndex];
    }

private:
    static constexpr std::uint8_t kDirty = 0x4;
    static constexpr std::uint8_t kIndexMask = 0x3;

    std::array<T, 3> m_buffers {};
    std::atomic<std::uint8_t> m_middle { 1 };
    std::uint8_t m_writeIndex = 0;   // writer-owned
    std::uint8_t m_readIndex = 2;    // reader-owned
};
}
