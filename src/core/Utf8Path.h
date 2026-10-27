#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace pianoassist
{

/** Builds a path from UTF-8 text. On Windows a plain std::string would be
    interpreted in the active code page and mangle non-ASCII names. */
std::filesystem::path pathFromUtf8 (std::string_view utf8);

/** Returns the path as UTF-8 text, independent of the platform encoding. */
std::string pathToUtf8 (const std::filesystem::path& path);

/** File name only, for logs: full paths can expose the user's name. */
std::string fileNameForLog (const std::filesystem::path& path);

} // namespace pianoassist
