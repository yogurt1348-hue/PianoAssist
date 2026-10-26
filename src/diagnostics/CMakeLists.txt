add_library(pianoassist_diagnostics STATIC
    RotatingLog.cpp
    SessionReport.cpp)
add_library(PianoAssist::diagnostics ALIAS pianoassist_diagnostics)
target_link_libraries(pianoassist_diagnostics
    PUBLIC PianoAssist::core
    PRIVATE nlohmann_json::nlohmann_json)
