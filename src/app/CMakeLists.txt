juce_add_gui_app(PianoAssistApp
    PRODUCT_NAME "PianoAssist"
    COMPANY_NAME "PianoAssist"
    VERSION ${PROJECT_VERSION})

# The ui module is compiled here: JUCE modules are built per consuming target.
target_sources(PianoAssistApp PRIVATE
    Main.cpp
    AppPaths.cpp
    MidiInputService.cpp
    SongLoader.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/MainComponent.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/PianoRollComponent.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/KeyboardStrip.cpp)

target_compile_definitions(PianoAssistApp PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:PianoAssistApp,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:PianoAssistApp,JUCE_VERSION>"
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(PianoAssistApp
    PRIVATE
        PianoAssist::settings
        PianoAssist::diagnostics
        PianoAssist::transport
        PianoAssist::midi_io
        PianoAssist::timeline
        juce::juce_gui_basics
        juce::juce_audio_devices
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)
