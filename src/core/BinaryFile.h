#pragma once

#include "core/Result.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace pianoassist
{

/** Reads a whole file, refusing anything larger than maxBytes. Every file the
    user hands us is untrusted, so the size bound is mandatory. */
Result<std::vector<std::uint8_t>> readBinaryFile (const std::filesystem::path& path,
                                                  std::size_t maxBytes);

/** Writes via a temporary sibling file and rename, so a crash mid-write never
    leaves a truncated file behind. */
Result<bool> writeFileAtomically (const std::filesystem::path& path,
                                  const std::vector<std::uint8_t>& bytes);

} // namespace pianoassist
