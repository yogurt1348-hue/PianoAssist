add_library(pianoassist_core STATIC
    BinaryFile.cpp
    Utf8Path.cpp)
add_library(PianoAssist::core ALIAS pianoassist_core)
target_include_directories(pianoassist_core PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(pianoassist_core PUBLIC PianoAssist::options)
