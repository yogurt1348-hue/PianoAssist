# Warning and encoding flags shared by every PianoAssist target.
add_library(pianoassist_options INTERFACE)
add_library(PianoAssist::options ALIAS pianoassist_options)

target_compile_definitions(pianoassist_options INTERFACE
    PIANOASSIST_VERSION="${PROJECT_VERSION}")

if(MSVC)
    # /utf-8: source and execution character sets are UTF-8, so non-ASCII
    # string literals and paths round-trip through std::u8string correctly.
    target_compile_options(pianoassist_options INTERFACE
        /utf-8 /W4 /permissive- /Zc:__cplusplus /EHsc)
    target_compile_definitions(pianoassist_options INTERFACE
        NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(pianoassist_options INTERFACE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()
