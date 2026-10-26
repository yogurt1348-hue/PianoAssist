#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <optional>
#include <type_traits>

namespace pianoassist
{

/** Wait-free single-producer/single-consumer ring buffer. The producer (MIDI
    or audio callback) never allocates, locks or blocks: when the buffer is
    full the item is dropped and counted. Capacity must be a power of two. */
template <typename T, std::size_t Capacity>
class SpscQueue
{
    static_assert (Capacity >= 2 && (Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");
    static_assert (std::is_trivially_copyable_v<T>, "Items are copied on real-time threads");

public:
    /** Producer side. Returns false (and counts a drop) when full. */
    bool push (const T& item) noexcept
    {
        const auto head = head_.load (std::memory_order_relaxed);
        const auto tail = tail_.load (std::memory_order_acquire);
        if (head - tail == Capacity)
        {
            dropped_.fetch_add (1, std::memory_order_relaxed);
            return false;
        }
        items_[head & (Capacity - 1)] = item;
        head_.store (head + 1, std::memory_order_release);
        return true;
    }

    /** Consumer side. */
    std::optional<T> pop() noexcept
    {
        const auto tail = tail_.load (std::memory_order_relaxed);
        const auto head = head_.load (std::memory_order_acquire);
        if (tail == head)
            return std::nullopt;
        const T item = items_[tail & (Capacity - 1)];
        tail_.store (tail + 1, std::memory_order_release);
        return item;
    }

    /** Consumer side: returns and resets the number of dropped items. */
    std::size_t takeDroppedCount() noexcept { return dropped_.exchange (0, std::memory_order_relaxed); }

    static constexpr std::size_t capacity() noexcept { return Capacity; }

private:
    std::array<T, Capacity> items_ {};
    alignas (64) std::atomic<std::size_t> head_ { 0 };
    alignas (64) std::atomic<std::size_t> tail_ { 0 };
    std::atomic<std::size_t> dropped_ { 0 };
};

} // namespace pianoassist
