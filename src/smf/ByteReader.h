#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace pianoassist::smf
{

/** Bounds-checked big-endian reader over an untrusted byte buffer. Every read
    returns nullopt instead of running past the end. */
class ByteReader
{
public:
    explicit ByteReader (std::span<const std::uint8_t> bytes) noexcept : bytes_ (bytes) {}

    std::size_t position() const noexcept { return position_; }
    std::size_t remaining() const noexcept { return bytes_.size() - position_; }
    bool atEnd() const noexcept { return remaining() == 0; }

    std::optional<std::uint8_t> readU8() noexcept;
    std::optional<std::uint16_t> readU16() noexcept;
    std::optional<std::uint32_t> readU32() noexcept;

    /** SMF variable-length quantity: at most 4 bytes, value <= 0x0FFFFFFF.
        A fifth continuation byte is malformed and rejected. */
    std::optional<std::uint32_t> readVariableLength() noexcept;

    /** Returns the next count bytes and advances, or nullopt if fewer remain. */
    std::optional<std::span<const std::uint8_t>> readBytes (std::size_t count) noexcept;

    bool skip (std::size_t count) noexcept;

private:
    std::span<const std::uint8_t> bytes_;
    std::size_t position_ = 0;
};

} // namespace pianoassist::smf
