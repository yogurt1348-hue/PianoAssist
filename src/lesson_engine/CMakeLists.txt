add_library(pianoassist_lesson_engine STATIC
    PracticeProfile.cpp)
add_library(PianoAssist::lesson_engine ALIAS pianoassist_lesson_engine)
target_link_libraries(pianoassist_lesson_engine PUBLIC PianoAssist::timeline PianoAssist::transport)
