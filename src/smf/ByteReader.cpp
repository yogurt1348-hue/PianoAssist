#include "smf/ByteReader.h"

namespace pianoassist::smf
{

std::optional<std::uint8_t> ByteReader::readU8() noexcept
{
    if (remaining() < 1)
        return std::nullopt;
    return bytes_[position_++];
}

std::optional<std::uint16_t> ByteReader::readU16() noexcept
{
    if (remaining() < 2)
        return std::nullopt;
    const auto value = static_cast<std::uint16_t> ((bytes_[position_] << 8) | bytes_[position_ + 1]);
    position_ += 2;
    return value;
}

std::optional<std::uint32_t> ByteReader::readU32() noexcept
{
    if (remaining() < 4)
        return std::nullopt;
    std::uint32_t value = 0;
    for (int i = 0; i < 4; ++i)
        value = (value << 8) | bytes_[position_++];
    return value;
}

std::optional<std::uint32_t> ByteReader::readVariableLength() noexcept
{
    constexpr int maxBytes = 4;
    const auto start = position_;
    std::uint32_t value = 0;

    for (int i = 0; i < maxBytes; ++i)
    {
        const auto byte = readU8();
        if (! byte)
        {
            position_ = start;
            return std::nullopt;
        }
        value = (value << 7) | (*byte & 0x7Fu);
        if ((*byte & 0x80u) == 0)
            return value;
    }

    position_ = start;
    return std::nullopt;
}

std::optional<std::span<const std::uint8_t>> ByteReader::readBytes (std::size_t count) noexcept
{
    if (remaining() < count)
        return std::nullopt;
    const auto result = bytes_.subspan (position_, count);
    position_ += count;
    return result;
}

bool ByteReader::skip (std::size_t count) noexcept
{
    if (remaining() < count)
        return false;
    position_ += count;
    return true;
}

} // namespace pianoassist::smf
