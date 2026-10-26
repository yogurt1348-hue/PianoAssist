cmake_minimum_required(VERSION 3.25)

project(PianoAssist
    VERSION 0.1.0
    DESCRIPTION "Note-gated MIDI piano practice player"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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
