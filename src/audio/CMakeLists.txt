# Interface seam only; InternalSynthRenderer arrives in phase 2.
add_library(pianoassist_audio INTERFACE)
add_library(PianoAssist::audio ALIAS pianoassist_audio)
target_link_libraries(pianoassist_audio INTERFACE PianoAssist::core)
