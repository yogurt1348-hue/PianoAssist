# JUCE-free domain modules. Order follows the dependency direction.
add_subdirectory(core)
add_subdirectory(smf)
add_subdirectory(timeline)
add_subdirectory(transport)
add_subdirectory(lesson_engine)
add_subdirectory(midi_io)
add_subdirectory(audio)
add_subdirectory(settings)
add_subdirectory(diagnostics)

# JUCE-dependent adapters. JUCE modules are compiled into each consuming
# target, so the ui sources are compiled into the application target.
if(PIANOASSIST_BUILD_APP)
    add_subdirectory(app)
endif()
