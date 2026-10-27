# JUCE-free part of MIDI input: event normalization, monitor state and device
# selection, so recorded input streams can be replayed in tests. The JUCE
# device adapter lives in src/app.
add_library(pianoassist_midi_io STATIC
    MidiNormalizer.cpp
    InputMonitor.cpp
    DeviceSelection.cpp)
add_library(PianoAssist::midi_io ALIAS pianoassist_midi_io)
target_link_libraries(pianoassist_midi_io PUBLIC PianoAssist::core)
