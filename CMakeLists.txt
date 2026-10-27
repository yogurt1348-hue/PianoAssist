cmake_minimum_required(VERSION 3.25)

project(PianoAssist
    VERSION 0.1.0
    DESCRIPTION "Note-gated MIDI piano practice player"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Link the MSVC runtime statically (/MT, /MTd in Debug) into every target,
# including the fetched dependencies, so both test builds start on a clean
# Windows machine without the Visual C++ Redistributable or the debug runtime
# DLLs (VCRUNTIME140D.dll, MSVCP140D.dll, ucrtbased.dll) that only Visual
# Studio installs.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# The domain libraries (smf, timeline, transport, lesson_engine, settings,
# diagnostics, midi_io types, audio interfaces) never depend on JUCE, so the
# tests can be built and run on any machine without a window or audio device.
option(PIANOASSIST_BUILD_APP "Build the JUCE desktop application" ON)
option(PIANOASSIST_BUILD_TESTS "Build the automated test executable" ON)

include(cmake/CompilerOptions.cmake)
include(cmake/Dependencies.cmake)

add_subdirectory(src)

if(PIANOASSIST_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
