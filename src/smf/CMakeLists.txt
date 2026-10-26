add_library(pianoassist_smf STATIC
    ByteReader.cpp
    SmfHeader.cpp
    TempoMap.cpp)
add_library(PianoAssist::smf ALIAS pianoassist_smf)
target_link_libraries(pianoassist_smf PUBLIC PianoAssist::core)
