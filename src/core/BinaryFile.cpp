#include "core/BinaryFile.h"

#include "core/Utf8Path.h"

#include <fstream>
#include <system_error>

namespace pianoassist
{

Result<std::vector<std::uint8_t>> readBinaryFile (const std::filesystem::path& path,
                                                  std::size_t maxBytes)
{
    using R = Result<std::vector<std::uint8_t>>;
    const auto name = fileNameForLog (path);

    std::error_code ec;
    const auto size = std::filesystem::file_size (path, ec);
    if (ec)
        return R::fail ("Cannot read '" + name + "': " + ec.message());
    if (size > maxBytes)
        return R::fail ("'" + name + "' is too large (" + std::to_string (size)
                        + " bytes, limit " + std::to_string (maxBytes) + ")");

    std::ifstream in (path, std::ios::binary);
    if (! in)
        return R::fail ("Cannot open '" + name + "'");

    std::vector<std::uint8_t> bytes (static_cast<std::size_t> (size));
    if (size > 0)
        in.read (reinterpret_cast<char*> (bytes.data()), static_cast<std::streamsize> (size));
    if (static_cast<std::uintmax_t> (in.gcount()) != size)
        return R::fail ("'" + name + "' changed or ended while reading");

    return R::ok (std::move (bytes));
}

Result<bool> writeFileAtomically (const std::filesystem::path& path,
                                  const std::vector<std::uint8_t>& bytes)
{
    using R = Result<bool>;
    const auto name = fileNameForLog (path);
    auto temp = path;
    temp += ".tmp";

    std::error_code ec;
    if (path.has_parent_path())
        std::filesystem::create_directories (path.parent_path(), ec);

    {
        std::ofstream out (temp, std::ios::binary | std::ios::trunc);
        if (! out)
            return R::fail ("Cannot create '" + fileNameForLog (temp) + "'");
        out.write (reinterpret_cast<const char*> (bytes.data()), static_cast<std::streamsize> (bytes.size()));
        out.flush();
        if (! out)
            return R::fail ("Cannot write '" + fileNameForLog (temp) + "'");
    }

    std::filesystem::rename (temp, path, ec);
    if (ec)
    {
        std::filesystem::remove (temp, ec);
        return R::fail ("Cannot replace '" + name + "'");
    }
    return R::ok (true);
}

} // namespace pianoassist
