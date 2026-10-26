# Phase 0 Foundation

## Objective

Establish a Windows JUCE repository that can be built, tested, diagnosed, and extended without hidden machine state.

## Work items

1. Use CMake presets for Windows x64 Debug and Release; pin JUCE version and compiler assumptions in README.
2. Create libraries matching architecture modules. Keep initial JUCE code thin and domain classes testable without a window or audio device.
3. Add a structured rotating log with app version, Windows version, selected audio/MIDI device names, and recoverable error messages.
4. Add fixture loader for checked-in tiny MIDI files and a test executable. Tests must run without physical hardware.
5. Add settings schema/versioning for device identifiers, practice profile, visual preferences, and calibration offset. Failed migrations fall back safely.

## Done when

- Fresh clone configures and builds using documented commands.
- App launches to an empty-state window and logs one session.
- Unit tests cover tempo conversion and serialization round trips.
- No hard-coded absolute paths, device names, or developer-only assets.

## Implementation notes

Use UTF-8 paths and test a file path with non-ASCII characters. Treat any MIDI or settings file as untrusted input; bound sizes and reject malformed variable-length fields rather than crashing.

