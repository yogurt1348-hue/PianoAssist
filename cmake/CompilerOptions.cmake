# Warning and encoding flags shared by every PianoAssist target.
add_library(pianoassist_options INTERFACE)
add_library(PianoAssist::options ALIAS pianoassist_options)

# CI sets the build number of a test build (test-build-N) and the commit, so
# the app can say exactly what it is and compare itself with GitHub releases.
# A local or stable build has build number 0.
set(PIANOASSIST_BUILD_NUMBER "0" CACHE STRING "Test build number (test-build-N); 0 for stable or local builds")
set(PIANOASSIST_COMMIT "local" CACHE STRING "Commit SHA the build comes from")
target_compile_definitions(pianoassist_options INTERFACE
    PIANOASSIST_VERSION="${PROJECT_VERSION}"
    PIANOASSIST_BUILD_NUMBER=${PIANOASSIST_BUILD_NUMBER}
    PIANOASSIST_COMMIT="${PIANOASSIST_COMMIT}")

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
