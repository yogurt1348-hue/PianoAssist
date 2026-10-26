#include "core/Utf8Path.h"

namespace pianoassist
{

std::filesystem::path pathFromUtf8 (std::string_view utf8)
{
    const std::u8string text (reinterpret_cast<const char8_t*> (utf8.data()), utf8.size());
    return std::filesystem::path (text);
}

std::string pathToUtf8 (const std::filesystem::path& path)
{
    const std::u8string text = path.u8string();
    return std::string (reinterpret_cast<const char*> (text.data()), text.size());
}

std::string fileNameForLog (const std::filesystem::path& path)
{
    return pathToUtf8 (path.filename());
}

} // namespace pianoassist
