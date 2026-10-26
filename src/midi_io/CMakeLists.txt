# Device adapters (JUCE) arrive in phase 1; the event types stay JUCE-free so
# recorded input streams can be replayed in tests.
add_library(pianoassist_midi_io INTERFACE)
add_library(PianoAssist::midi_io ALIAS pianoassist_midi_io)
target_link_libraries(pianoassist_midi_io INTERFACE PianoAssist::core)
