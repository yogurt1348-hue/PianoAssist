add_executable(pianoassist_tests
    unit/ByteReaderTests.cpp
    unit/SmfHeaderTests.cpp
    unit/TempoMapTests.cpp
    unit/TimelineTests.cpp
    unit/PracticeProfileTests.cpp
    unit/SettingsTests.cpp
    unit/RotatingLogTests.cpp
    unit/Utf8PathTests.cpp
    unit/MidiInputTests.cpp
    unit/SmfReaderTests.cpp
    unit/PlaybackEngineTests.cpp
    unit/PracticeTests.cpp
    unit/PracticeModesTests.cpp
    unit/DiagnosticReportTests.cpp
    unit/FileSafetyTests.cpp
    unit/SoundFontTests.cpp
    unit/InstrumentRouterTests.cpp
    unit/SongLibraryTests.cpp
    unit/UpdatePolicyTests.cpp
    unit/Fixtures.cpp)

target_include_directories(pianoassist_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/unit)
target_compile_definitions(pianoassist_tests PRIVATE
    PIANOASSIST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
target_link_libraries(pianoassist_tests PRIVATE
    PianoAssist::smf
    PianoAssist::timeline
    PianoAssist::transport
    PianoAssist::lesson_engine
    PianoAssist::midi_io
    PianoAssist::audio
    PianoAssist::settings
    PianoAssist::diagnostics
    nlohmann_json::nlohmann_json
    Catch2::Catch2WithMain)

include(${catch2_SOURCE_DIR}/extras/Catch.cmake)
catch_discover_tests(pianoassist_tests)
