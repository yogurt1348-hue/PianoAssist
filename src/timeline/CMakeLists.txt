add_library(pianoassist_timeline STATIC
    Timeline.cpp)
add_library(PianoAssist::timeline ALIAS pianoassist_timeline)
target_link_libraries(pianoassist_timeline PUBLIC PianoAssist::core)
