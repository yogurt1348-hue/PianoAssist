# Every third-party dependency is pinned to an exact tag here, so a fresh clone
# resolves the same sources on every machine. See README "Dependencies".
include(FetchContent)

set(FETCHCONTENT_QUIET ON)

# nlohmann/json (MIT): settings serialization in the JUCE-free settings module.
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE)
set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)

# TinySoundFont (MIT): SoundFont (.sf2) playback in the JUCE-free audio
# module. Header-only; pinned to a commit because the project has no tags.
FetchContent_Declare(tinysoundfont
    GIT_REPOSITORY https://github.com/schellingb/TinySoundFont.git
    GIT_TAG        853a0a171759f1ddba0de1442133a75912bbeffa)
FetchContent_MakeAvailable(tinysoundfont)

if(PIANOASSIST_BUILD_TESTS)
    # Catch2 (BSL-1.0): test executable only, never shipped.
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.7.1
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(Catch2)
endif()

if(PIANOASSIST_BUILD_APP)
    # JUCE (AGPLv3 or commercial licence): application, MIDI devices, audio.
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.8
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()
