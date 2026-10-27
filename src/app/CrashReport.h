#pragma once

#include <filesystem>
#include <string>

namespace pianoassist::app::crash
{

/** Prepares crash reports: where a minidump goes (Windows only). Call once
    at startup, before installing the crash handler. */
void prepare (const std::filesystem::path& logFolder);

/** Remembers the calling thread under a name ("message", "audio", "midi"),
    so a report says which thread crashed. Cheap after the first call per
    thread; safe on the audio thread (no allocation, no locks). */
void nameThisThread (const char* name) noexcept;

/** Called from the crash handler with what the platform gives it
    (EXCEPTION_POINTERS* on Windows). Writes the minidump and returns the
    report fields as text lines: exception, address as module+offset,
    thread, frames as module+offset, and the dump file. */
std::string describe (void* platformInfo);

} // namespace pianoassist::app::crash
