add_library(pianoassist_settings STATIC
    Settings.cpp)
add_library(PianoAssist::settings ALIAS pianoassist_settings)
target_link_libraries(pianoassist_settings
    PUBLIC PianoAssist::lesson_engine
    PRIVATE nlohmann_json::nlohmann_json)
