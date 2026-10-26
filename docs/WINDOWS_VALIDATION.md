# Adaptive Song Mode — Windows validation (for the local agent / tester)

The adaptive core was developed and tested on Linux (GCC 13). The full JUCE app
also builds and starts on Linux, but there was **no audio device and no MIDI
hardware** in the cloud container, so real-time sound and a real keyboard must be
validated on Windows. Follow these steps in order and record results in
`docs/ADAPTIVE_SONG_PROGRESS.md` (section *WINDOWS VALIDATION NEEDED*).

Branch: `claude/gifted-einstein-6xjycw` of `yogurt1348-hue/PianoAssist`.

---

## 1. Prerequisites

* Visual Studio 2022 or 2026 with the **Desktop development with C++** workload (MSVC, x64).
* CMake ≥ 3.22 and Ninja on `PATH` (both ship with Visual Studio).
* Git.
* Optional but needed for sound: FluidSynth via vcpkg (see `README.md`); without
  it the app uses `NullSynthEngine` and is **silent** — still enough to validate
  following (status window + log), but not the sound.
* A GM SoundFont in `resources/sf2/` (e.g. GeneralUser GS).
* A USB MIDI keyboard.

## 2. Get the code and JUCE

```powershell
git clone https://github.com/yogurt1348-hue/PianoAssist.git
cd PianoAssist
git checkout claude/gifted-einstein-6xjycw
git clone --depth 1 --branch 8.0.13 https://github.com/juce-framework/JUCE.git lib/JUCE
```

## 3. Configure, build, test (Debug)

From a **Developer PowerShell for VS** (or after `vcvars64.bat`):

```powershell
cmake -S . -B build-msvc -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build-msvc
ctest --test-dir build-msvc --output-on-failure
```

Expected: **all tests pass** (50 on a JUCE-enabled configure: 47 core + 3 JUCE
tests). The Adaptive ones are:

```
cadenza_adaptive_midifile_tests   cadenza_adaptive_follower_tests
cadenza_adaptive_transport_tests  cadenza_adaptive_backing_tests
cadenza_adaptive_session_tests    adaptive_demo_writes_song
```

Run one verbosely if needed: `ctest --test-dir build-msvc -R adaptive --output-on-failure -V`.

Things MSVC may flag that GCC did not (fix and re-run if so):
`std::atomic<double>` lock-freedom (already asserted by `Transport.h`),
`/W4` conversion warnings in `Source/Adaptive/*`, `<bit>` (`std::popcount`, C++20).

## 4. Release build

```powershell
cmake -S . -B build-msvc-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-msvc-release
ctest --test-dir build-msvc-release --output-on-failure
```

## 5. Executable

* Debug: `build-msvc\Cadenza_artefacts\Debug\Cadenza Workstation.exe`
* Release: `build-msvc-release\Cadenza_artefacts\Release\Cadenza Workstation.exe`
* Log file: `%APPDATA%\Cadenza\cadenza.log` (recreated at each launch).

## 6. Make a test song (copyright-free)

```powershell
build-msvc\adaptive-demo.exe "%USERPROFILE%\Desktop\adaptive-demo.mid" --bpm 90
build-msvc\adaptive-demo.exe "%USERPROFILE%\Desktop\adaptive-demo-tempo.mid" --bpm 90 --tempo-change
```

Track 1 **Piano** is your part: every bar = a left-hand triad on beat 1
(C, F, G, C, Am, F, G, C) + four right-hand quarter notes (C-major melody,
see `tools/adaptive-demo/main.cpp`). Tracks 2–4 = Drums, Bass, Strings.
Any other multitrack MIDI file with a piano track works too.

## 7. Real-keyboard test scenario

1. Start the app, connect the keyboard, pick the audio device (Setting → Audio)
   and a SoundFont. Check the arranger still works first (Home → Play, play a
   chord): **Arranger Mode regression check**.
2. Setting → **Adaptive Song...** → *Load MIDI...* → `adaptive-demo.mid`.
   Expect: Reference = `1: Piano`, Backing ticked = Drums, Bass, Strings; the
   panel lists any channel remaps (normal: none, or "plays on channel 16/15").
3. *MIDI input*: your keyboard (or Auto). *Start*: "Wait for me".
4. Press **Play**. Expect status `READY - start playing`; the band is silent;
   the arranger band is stopped; the whole keyboard plays piano (no split).
5. Play the piano part at roughly 90 BPM. Expect within the first notes:
   status **TRACKING** (green), the band starts on your first chord, "You: ~90 BPM".
6. **Slow down** gradually to ~70 BPM over 8 bars → "Band" BPM follows,
   drums stay steady (no stutter, no jumps).
7. **Speed up** to ~110 BPM → the band speeds up.
8. Play **one wrong note** and **skip one note** → status may flash UNCERTAIN,
   never LOST; the band keeps going.
9. **Stop playing** in the middle of a bar for 5 s → status **WAITING**, the band
   stops just after your next expected note (≤ ~0.1 s of extra music), sustained
   strings are released after ~1.5 s. Resume → TRACKING, band continues from
   where you are, without a jump.
10. Play 6–8 random notes (garbage) → **LOST**; then play bars 9–12 correctly →
    **REACQUIRING** → **TRACKING**, the band jumps to the right place once.
11. Restart the piece from bar 1 while it is running → it relocates to the top.
12. Press **Stop** → all backing notes stop at once. Close the window → the
    arranger works again (repeat step 1).
13. Repeat with *Start* = "Play the intro, wait at my entry" (band plays from
    bar 1 at the written tempo and waits for you if you are late).
14. Repeat with `adaptive-demo-tempo.mid`: follow the written tempo change; the
    "You" BPM should jump by +20 BPM with the written change while the band
    stays locked.
15. Two-file mode: *Separate backing MIDI...* with a second file → the panel
    must warn when PPQ / tempo maps / lengths differ.
16. Optional: load a VST3 instrument on a backing channel (Mixer) and verify the
    backing plays through it; try the master multitimbral instrument too.
17. Optional: sustain pedal while playing → piano sustains (performance mode
    forwards CC64/66/67 to the Right voices).

## 8. Expected behaviour summary

| Situation | Expected |
|---|---|
| Steady playing | band locked (error ≈ one audio block) |
| Gradual tempo change | band follows smoothly, no audible jumps |
| Band ahead/behind (phase) | corrected by ≤ 15 % temporary speed change |
| Single wrong / missing / extra note | no loss of position |
| Pause | WAITING, band waits at your next event, then resumes |
| Garbage / jump | LOST → REACQUIRING → TRACKING with one clean seek |
| Stop / close | all backing notes off, arranger restored |

## 9. Useful debug logs

* Debug builds log per-note lines automatically; Release builds when the
  window's **Diagnostics log** box is ticked or with `set CADENZA_ADAPTIVE_DEBUG=1`
  before starting the app.
* Lines look like:
  ```
  [Adaptive] state IDLE -> TRACKING at group 0, 90.0 BPM, confidence 0.72
  [Adaptive] note=64 t=1234.567 advance group=12 expectedTick=5760 followerTick=5760 playheadTick=5741 phase=+21ms bpm(rate)=0.947 conf=0.90 margin=3.40 state=TRACKING
  ```
  `phase` > 0 = band behind you. Look for: `extra` kinds while you played the
  right notes (tracking too strict), large `phase` values (latency/tuning),
  frequent UNCERTAIN/LOST (sensitivity), `margin` ≈ 0 (ambiguous passages).
* MIDI input: `[MIDI] dev=... route=performance` lines confirm performance mode.
* If timing feels uniformly late/early, compare audio buffer size (Setting → Audio);
  MIDI timestamps come from JUCE's `getMillisecondCounterHiRes` clock on both
  the MIDI and audio threads.

## 10. Report back

For each step: pass/fail + notes; attach `cadenza.log` for failures. Update
`docs/ADAPTIVE_SONG_PROGRESS.md` → *WINDOWS VALIDATION NEEDED* and *KNOWN ISSUES*.
