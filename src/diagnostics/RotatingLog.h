#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <string_view>

namespace pianoassist::diagnostics
{

enum class LogLevel
{
    debug,
    info,
    warning,
    error,
};

std::string_view toString (LogLevel level) noexcept;

using LogFields = std::map<std::string, std::string>;

/** Structured JSON-lines log with size-based rotation:
    <base>.log is current, <base>.1.log the previous one, and so on.

    Thread-safe, but it takes a lock and does file I/O: never call it from the
    MIDI input callback or the audio callback. */
class RotatingLog
{
public:
    struct Options
    {
        std::filesystem::path directory;
        std::string baseName = "pianoassist";
        std::uintmax_t maxFileBytes = 1024 * 1024;
        int maxArchivedFiles = 4;
    };

    using Clock = std::function<std::string()>; ///< returns an ISO-8601 timestamp

    explicit RotatingLog (Options options, Clock clock = {});

    bool isOpen() const;
    std::filesystem::path currentFile() const;

    void write (LogLevel level, std::string_view category, std::string_view message, const LogFields& fields = {});

private:
    void openCurrent();
    void rotate();
    std::filesystem::path archivePath (int index) const;

    Options options_;
    Clock clock_;
    mutable std::mutex mutex_;
    std::ofstream stream_;
    std::uintmax_t currentBytes_ = 0;
};

/** UTC timestamp with millisecond precision, e.g. 2026-10-26T21:16:03.123Z. */
std::string utcTimestampNow();

} // namespace pianoassist::diagnostics
