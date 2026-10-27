#include "app/CrashReport.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <ctime>

#if defined(_WIN32)
 #include <windows.h>
 #include <dbghelp.h>
#else
 #include <functional>
 #include <thread>
#endif

namespace pianoassist::app::crash
{
namespace
{
struct NamedThread
{
    std::atomic<unsigned long long> id { 0 };
    std::atomic<const char*> name { nullptr };
};
std::array<NamedThread, 8> threads;

unsigned long long currentThreadId() noexcept
{
#if defined(_WIN32)
    return GetCurrentThreadId();
#else
    return static_cast<unsigned long long> (std::hash<std::thread::id> {}(std::this_thread::get_id()));
#endif
}

const char* nameOf (unsigned long long id) noexcept
{
    for (const auto& t : threads)
        if (t.id.load() == id)
            return t.name.load();
    return "other";
}

#if defined(_WIN32)
std::wstring dumpFolder;

/** "PianoAssist.exe+0x1a2b3c" for an address, or the bare address. */
std::string moduleOffset (const void* address)
{
    HMODULE module = nullptr;
    char buffer[MAX_PATH + 32] {};
    if (GetModuleHandleExW (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            static_cast<LPCWSTR> (address), &module)
        && module != nullptr)
    {
        wchar_t path[MAX_PATH] {};
        GetModuleFileNameW (module, path, MAX_PATH);
        const wchar_t* name = path;
        for (const wchar_t* p = path; *p != 0; ++p)
            if (*p == L'\\' || *p == L'/')
                name = p + 1;
        char narrow[MAX_PATH] {};
        WideCharToMultiByte (CP_UTF8, 0, name, -1, narrow, MAX_PATH, nullptr, nullptr);
        std::snprintf (buffer, sizeof (buffer), "%s+0x%llx", narrow,
                       static_cast<unsigned long long> (reinterpret_cast<const char*> (address) - reinterpret_cast<const char*> (module)));
    }
    else
        std::snprintf (buffer, sizeof (buffer), "0x%llx", static_cast<unsigned long long> (reinterpret_cast<std::uintptr_t> (address)));
    return buffer;
}

/** Frames of the crashed thread, unwound from the exception context with the
    x64 unwind tables (no symbol files needed on the user's machine). */
std::string framesOf (const CONTEXT& start)
{
    std::string text;
 #if defined(_M_X64)
    CONTEXT context = start;
    for (int i = 0; i < 48 && context.Rip != 0; ++i)
    {
        text += "  " + std::to_string (i) + ": " + moduleOffset (reinterpret_cast<const void*> (context.Rip)) + "\n";
        DWORD64 imageBase = 0;
        if (auto* function = RtlLookupFunctionEntry (context.Rip, &imageBase, nullptr))
        {
            void* handlerData = nullptr;
            DWORD64 establisher = 0;
            RtlVirtualUnwind (UNW_FLAG_NHANDLER, imageBase, context.Rip, function, &context, &handlerData, &establisher, nullptr);
        }
        else
        {
            // A leaf function: the return address is on top of the stack.
            context.Rip = *reinterpret_cast<const DWORD64*> (context.Rsp);
            context.Rsp += 8;
        }
    }
 #else
    (void) start;
 #endif
    return text;
}

std::string writeDump (EXCEPTION_POINTERS* info)
{
    if (dumpFolder.empty())
        return "(no folder)";
    SYSTEMTIME now {};
    GetLocalTime (&now);
    wchar_t name[64] {};
    swprintf (name, 64, L"crash-%04u%02u%02u-%02u%02u%02u.dmp", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
              now.wSecond);
    const std::wstring path = dumpFolder + L"\\" + name;
    const HANDLE file = CreateFileW (path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return "(could not create the file)";
    MINIDUMP_EXCEPTION_INFORMATION exception {};
    exception.ThreadId = GetCurrentThreadId();
    exception.ExceptionPointers = info;
    exception.ClientPointers = FALSE;
    const auto type = static_cast<MINIDUMP_TYPE> (MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo
                                                  | MiniDumpWithUnloadedModules);
    const BOOL ok = MiniDumpWriteDump (GetCurrentProcess(), GetCurrentProcessId(), file, type, info != nullptr ? &exception : nullptr,
                                       nullptr, nullptr);
    CloseHandle (file);
    char narrow[64] {};
    WideCharToMultiByte (CP_UTF8, 0, name, -1, narrow, 64, nullptr, nullptr);
    return ok ? std::string (narrow) : std::string ("(writing failed)");
}
#endif
} // namespace

void prepare (const std::filesystem::path& logFolder)
{
#if defined(_WIN32)
    dumpFolder = logFolder.wstring();
#else
    (void) logFolder;
#endif
}

void nameThisThread (const char* name) noexcept
{
    thread_local bool named = false;
    if (named)
        return;
    named = true;
    const auto id = currentThreadId();
    for (auto& t : threads)
    {
        unsigned long long expected = 0;
        if (t.id.load() == id || t.id.compare_exchange_strong (expected, id))
        {
            t.name.store (name);
            return;
        }
    }
}

std::string describe (void* platformInfo)
{
    std::string text = "thread: ";
    text += nameOf (currentThreadId());
    text += "\n";
#if defined(_WIN32)
    auto* info = static_cast<EXCEPTION_POINTERS*> (platformInfo);
    if (info != nullptr && info->ExceptionRecord != nullptr)
    {
        const auto& record = *info->ExceptionRecord;
        char line[160] {};
        std::snprintf (line, sizeof (line), "exception: 0x%08lx at ", static_cast<unsigned long> (record.ExceptionCode));
        text += line + moduleOffset (record.ExceptionAddress) + "\n";
        if (record.ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record.NumberParameters >= 2)
        {
            std::snprintf (line, sizeof (line), "access: %s 0x%llx\n", record.ExceptionInformation[0] == 0 ? "read" : record.ExceptionInformation[0] == 1 ? "write" : "execute",
                           static_cast<unsigned long long> (record.ExceptionInformation[1]));
            text += line;
        }
    }
    if (info != nullptr && info->ContextRecord != nullptr)
        text += "frames:\n" + framesOf (*info->ContextRecord);
    text += "dump: " + writeDump (info) + "\n";
#else
    (void) platformInfo;
#endif
    return text;
}

} // namespace pianoassist::app::crash
