# Header-only for now: the state reducer and clock arrive in phase 2/3.
add_library(pianoassist_transport INTERFACE)
add_library(PianoAssist::transport ALIAS pianoassist_transport)
target_link_libraries(pianoassist_transport INTERFACE PianoAssist::core)
