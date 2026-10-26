#include "diagnostics/RotatingLog.h"

#include "core/Utf8Path.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdio>
#include <ctime>

namespace pianoassist::diagnostics
{

std::string_view toString (LogLevel level) noexcept
{
    switch (level)
    {
        case LogLevel::debug:   return "debug";
        case LogLevel::info:    return "info";
        case LogLevel::warning: return "warning";
        case LogLevel::error:   return "error";
    }
    return "info";
}

std::string utcTimestampNow()
{
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto time = system_clock::to_time_t (now);
    const auto millis = duration_cast<milliseconds> (now.time_since_epoch()).count() % 1000;

    std::tm utc {};
#if defined(_WIN32)
    gmtime_s (&utc, &time);
#else
    gmtime_r (&time, &utc);
#endif
    char buffer[64];
    std::snprintf (buffer, sizeof (buffer), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
                   utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
                   utc.tm_hour, utc.tm_min, utc.tm_sec, static_cast<int> (millis));
    return buffer;
}

RotatingLog::RotatingLog (Options options, Clock clock)
    : options_ (std::move (options)), clock_ (clock ? std::move (clock) : Clock (utcTimestampNow))
{
    std::error_code ec;
    std::filesystem::create_directories (options_.directory, ec);
    openCurrent();
}

bool RotatingLog::isOpen() const
{
    std::scoped_lock lock (mutex_);
    return stream_.is_open();
}

std::filesystem::path RotatingLog::currentFile() const
{
    return options_.directory / pathFromUtf8 (options_.baseName + ".log");
}

std::filesystem::path RotatingLog::archivePath (int index) const
{
    return options_.directory / pathFromUtf8 (options_.baseName + "." + std::to_string (index) + ".log");
}

void RotatingLog::openCurrent()
{
    stream_.open (currentFile(), std::ios::binary | std::ios::app);
    std::error_code ec;
    const auto size = std::filesystem::file_size (currentFile(), ec);
    currentBytes_ = ec ? 0 : size;
}

void RotatingLog::rotate()
{
    stream_.close();
    std::error_code ec;
    std::filesystem::remove (archivePath (options_.maxArchivedFiles), ec);
    for (int i = options_.maxArchivedFiles - 1; i >= 1; --i)
        if (std::filesystem::exists (archivePath (i), ec))
            std::filesystem::rename (archivePath (i), archivePath (i + 1), ec);
    if (options_.maxArchivedFiles > 0)
        std::filesystem::rename (currentFile(), archivePath (1), ec);
    else
        std::filesystem::remove (currentFile(), ec);
    openCurrent();
}

void RotatingLog::write (LogLevel level, std::string_view category, std::string_view message, const LogFields& fields)
{
    nlohmann::json line {
        { "time", clock_() },
        { "level", toString (level) },
        { "category", category },
        { "message", message },
    };
    if (! fields.empty())
        line["fields"] = fields;

    // Replace invalid UTF-8 rather than throwing: device names come from drivers.
    const auto text = line.dump (-1, ' ', false, nlohmann::json::error_handler_t::replace) + "\n";

    std::scoped_lock lock (mutex_);
    if (currentBytes_ > 0 && currentBytes_ + text.size() > options_.maxFileBytes)
        rotate();
    if (! stream_.is_open())
        return;
    stream_.write (text.data(), static_cast<std::streamsize> (text.size()));
    stream_.flush();
    currentBytes_ += text.size();
}

} // namespace pianoassist::diagnostics
