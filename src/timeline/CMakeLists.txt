add_library(pianoassist_timeline STATIC
    GmInstruments.cpp
    Song.cpp
    Timeline.cpp)
add_library(PianoAssist::timeline ALIAS pianoassist_timeline)
target_link_libraries(pianoassist_timeline PUBLIC PianoAssist::core PianoAssist::smf)
