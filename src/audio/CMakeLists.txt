# JUCE-free playback core. The JUCE synth renderer is compiled into the app.
add_library(pianoassist_audio STATIC
    PlaybackEvents.cpp
    PlaybackEngine.cpp)
add_library(PianoAssist::audio ALIAS pianoassist_audio)
target_link_libraries(pianoassist_audio PUBLIC PianoAssist::core PianoAssist::timeline)
