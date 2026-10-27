# PianoAssist

A Windows practice player for a USB MIDI piano (validated on a Casio CDP-S110). Load a local MIDI file, watch the notes on a piano roll, hear the accompaniment, and move forward only when you play the required notes.

The product brief and phase plan live in [`docs/plan/`](docs/plan/README.md). Implementation decisions are logged at the end of [`docs/plan/12-assumptions-open-questions.md`](docs/plan/12-assumptions-open-questions.md).

## Status

Version 0.2.0, test builds. Implemented and covered by automated tests: USB MIDI input with reconnect, MIDI playback with built-in synth, SoundFont and VST3 sounds per track, note-gated practice with hand modes, A–B practice ranges with loop and count-in, tempo 25–200%, a song library that keeps a copy and the settings of each song, settings window, Russian and English UI, installer and portable builds, update check.

Hardware behaviour on the Casio is checked by hand: [`docs/manual-checks/casio-checklist.md`](docs/manual-checks/casio-checklist.md).

| Document | What it covers |
| --- | --- |
| [`docs/practice-rules.md`](docs/practice-rules.md) | How notes, chords, loops, seeks and count-in behave |
| [`docs/data-and-updates.md`](docs/data-and-updates.md) | Where data is kept, the song library, updating, versions |
| [`docs/windows-warnings.md`](docs/windows-warnings.md) | SmartScreen and Defender, checking SHA-256, signing |
| [`docs/sounds.md`](docs/sounds.md) | Sounds, free SoundFonts and VST3 instruments |
| [`docs/known-limitations.md`](docs/known-limitations.md) | Known limitations |

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
| Settings | `%APPDATA%\PianoAssist\settings.json` (portable: `Data\settings.json` next to the exe) |
| Logs | `%APPDATA%\PianoAssist\logs\pianoassist.log` (JSON lines, rotated at 5 MiB, 5 archives kept) |
| Song library | `%APPDATA%\PianoAssist\Songs` (or the folder chosen in Settings) |

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
| `src/audio` | `IInstrumentRenderer` seam, sample-accurate playback engine, built-in synth (JUCE) |
| `src/settings` | Versioned settings schema with safe fallback |
| `src/diagnostics` | Rotating structured log, session report |
| `src/ui`, `src/app` | JUCE window and startup wiring |
| `tests` | Catch2 tests and MIDI fixtures (`tools/make_fixtures.py` regenerates them) |

## Dependencies

Pinned in [`cmake/Dependencies.cmake`](cmake/Dependencies.cmake): JUCE 8.0.8 (AGPLv3 or commercial), nlohmann/json 3.11.3 (MIT), Catch2 3.7.1 (BSL-1.0, tests only).
