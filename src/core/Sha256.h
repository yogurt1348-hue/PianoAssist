#pragma once

#include <cstdint>
#include <span>
#include <string>

namespace pianoassist
{

/** SHA-256 of a byte buffer as 64 lowercase hex digits (FIPS 180-4). */
std::string sha256Hex (std::span<const std::uint8_t> bytes);

} // namespace pianoassist
