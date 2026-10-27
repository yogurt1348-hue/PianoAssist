juce_add_gui_app(PianoAssistApp
    PRODUCT_NAME "PianoAssist"
    COMPANY_NAME "PianoAssist"
    # JUCE builds the exe's .ico from these (tools/make_icon.py makes them);
    # the window and taskbar use the exe's icon, so no default JUCE icon shows.
    ICON_BIG "${PROJECT_SOURCE_DIR}/assets/pianoassist-256.png"
    ICON_SMALL "${PROJECT_SOURCE_DIR}/assets/pianoassist-32.png"
    VERSION ${PROJECT_VERSION})

# The ui module is compiled here: JUCE modules are built per consuming target.
target_sources(PianoAssistApp PRIVATE
    Main.cpp
    AppPaths.cpp
    MidiInputService.cpp
    SongLoader.cpp
    LessonController.cpp
    InstrumentService.cpp
    PluginScanner.cpp
    UpdateChecker.cpp
    AudioService.cpp
    ${PROJECT_SOURCE_DIR}/src/audio/InternalSynthRenderer.cpp
    ${PROJECT_SOURCE_DIR}/src/audio/PluginInstrumentRenderer.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/MainComponent.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/PianoRollComponent.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/KeyboardStrip.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/NextNotesBoard.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/SettingsComponent.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/LibraryComponent.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/TracksComponent.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/Translations.cpp)

target_compile_definitions(PianoAssistApp PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:PianoAssistApp,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:PianoAssistApp,JUCE_VERSION>"
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_PLUGINHOST_VST3=1)

target_link_libraries(PianoAssistApp
    PRIVATE
        PianoAssist::settings
        PianoAssist::diagnostics
        PianoAssist::transport
        PianoAssist::midi_io
        PianoAssist::timeline
        PianoAssist::audio
        PianoAssist::lesson_engine
        juce::juce_gui_basics
        juce::juce_audio_devices
        juce::juce_audio_processors
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)
