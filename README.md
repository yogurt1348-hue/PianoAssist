# PianoAssist

A Windows practice player for a USB MIDI piano (validated on a Casio CDP-S110). Load a local MIDI file, watch the notes on a piano roll, hear the accompaniment, and move forward only when you play the required notes.

The product brief and phase plan live in [`docs/plan/`](docs/plan/README.md). Implementation decisions are logged at the end of [`docs/plan/12-assumptions-open-questions.md`](docs/plan/12-assumptions-open-questions.md).

## Status

Phase 0 (foundation) is done. Phase 1 (USB MIDI input) is in progress: the app lists MIDI inputs, opens the piano automatically, shows a live monitor of notes and the sustain pedal, and recovers when the cable is unplugged and plugged back. Manual check: [`docs/manual-checks/phase-1-midi-input.md`](docs/manual-checks/phase-1-midi-input.md). Phase 2 (song loading and playback) is in progress: MIDI files (format 0/1) open from a button, drag and drop or the command line, and are drawn on a scrollable, zoomable piano roll with inferred hand roles. Playback and practice arrive next.

## Requirements

| What | Version |
| --- | --- |
| OS | Windows 10/11 x64 |
| Compiler | Visual Studio 2022 (MSVC v143), C++20 |
| CMake | 3.25 or newer |
| Git | any recent version (dependencies are fetched at configure time) |

## Build and test on Windows

From a "Developer PowerShell for VS 2022" in the repository root:

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64-debug
ctest --preset windows-x64-debug
```

Use `windows-x64-release` for the release configuration. The app is written to `build\windows-x64\src\app\PianoAssistApp_artefacts\<Config>\PianoAssist.exe`.

## Tests without JUCE (any OS)

The domain libraries do not depend on JUCE, so the tests run anywhere with CMake, Ninja and a C++20 compiler:

```sh
cmake --preset tests-only
cmake --build --preset tests-only
ctest --preset tests-only
```

No MIDI keyboard or audio device is needed for the automated tests.

## Where the app keeps its files

| File | Location on Windows |
| --- | --- |
| Settings | `%APPDATA%\PianoAssist\settings.json` |
| Logs | `%APPDATA%\PianoAssist\logs\pianoassist.log` (JSON lines, rotated at 5 MiB, 5 archives kept) |

Logs record every startup step, the app and Windows versions, MIDI devices appearing and disappearing, every note and pedal event, user actions, errors and crashes with a stack trace. They do not record the paths or contents of your music files. "Open log folder" in the app opens this folder.

## Layout

| Folder | Module |
| --- | --- |
| `src/core` | UTF-8 paths, bounded file reading, `Result` |
| `src/smf` | Standard MIDI File parser (format 0/1), tempo map |
| `src/timeline` | Immutable normalized note model, song builder, hand-role inference |
| `src/transport` | Transport states (state machine arrives in phase 3) |
| `src/lesson_engine` | Practice profile and calibration (gate engine arrives in phase 3) |
| `src/midi_io` | Input normalization, monitor state, device selection (JUCE adapter in `src/app`) |
| `src/audio` | `IInstrumentRenderer` seam (internal synth arrives in phase 2) |
| `src/settings` | Versioned settings schema with safe fallback |
| `src/diagnostics` | Rotating structured log, session report |
| `src/ui`, `src/app` | JUCE window and startup wiring |
| `tests` | Catch2 tests and MIDI fixtures (`tools/make_fixtures.py` regenerates them) |

## Dependencies

Pinned in [`cmake/Dependencies.cmake`](cmake/Dependencies.cmake): JUCE 8.0.8 (AGPLv3 or commercial), nlohmann/json 3.11.3 (MIT), Catch2 3.7.1 (BSL-1.0, tests only).
