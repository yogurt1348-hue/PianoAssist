# JUCE-free playback core. The JUCE synth renderer is compiled into the app.
add_library(pianoassist_audio STATIC
    PlaybackEvents.cpp
    PlaybackEngine.cpp
    InstrumentRouter.cpp
    SoundFontRenderer.cpp)
add_library(PianoAssist::audio ALIAS pianoassist_audio)
target_link_libraries(pianoassist_audio PUBLIC PianoAssist::core PianoAssist::timeline)
# Header-only TinySoundFont; its warnings are not ours.
target_include_directories(pianoassist_audio SYSTEM PRIVATE ${tinysoundfont_SOURCE_DIR})
