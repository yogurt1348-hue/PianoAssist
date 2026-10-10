# Adaptive Song Mode — Progress (source of truth)

_Last updated: 2026-10-10 (cloud session)._

## UPSTREAM BASE

* Upstream: <https://github.com/Piskocis/Cadenza> (`master`)
* Imported commit: **`47da8f71670daa4caa1b3db2bf8f60a323f6c6b1`** ("feat: add polished master DSP")
* Imported with the full upstream history (178 commits) into
  `yogurt1348-hue/PianoAssist`, branch `claude/gifted-einstein-6xjycw`
  (the repository was empty; no history was rewritten).
  Local git remote `upstream` = `https://github.com/Piskocis/Cadenza.git`.
* License: GPLv3, unchanged. All new code is original (GPLv3); no new dependencies.

## BASELINE (Phase 0)

* Linux, GCC 13.3, CMake 3.28, Ninja.
* Core only: `cmake -S . -B build-linux -G Ninja && cmake --build build-linux && ctest --test-dir build-linux`
  → **41/41** upstream tests pass.
* With JUCE 8.0.13 in `lib/JUCE` (git-ignored) the **whole app** builds on Linux
  after linking libcurl for JUCE targets on Linux only (CMake change guarded by
  `if(UNIX AND NOT APPLE)`; Windows unaffected) → 44/44 tests (41 + 3 JUCE tests).

## DONE

| Phase | What | Commit |
|---|---|---|
| 0 | Import, baseline, architecture study, docs | `c2d23ba` |
| 1 | `MidiFile` (SMF reader/writer), `TempoMap`, `ReferencePerformance` with chord grouping | `131beab` |
| 2 | `TempoEstimator`, `ScoreFollower` (windowed DP + global re-acquisition + state machine) | `66e912c` |
| 3 | `AdaptiveTransport` (PLL tempo + phase, braking/hold, seek policy, start modes), tuned defaults | `ba9f19a` |
| 4 | `BackingSequence` (RT renderer, chase), `AdaptiveSong` (track selection, two-file checks, channel assignment) | `44eea0e` |
| 5a | `AdaptiveSongSession` + `TripleBuffer` (thread contract) | `9869082` |
| 5b | App integration: AudioEngine block hook, MidiRouter raw tap + performance mode, `AdaptiveSongEngine`, `AdaptiveSongPanel`, Setting-page button, mode switching | `2ae4ffc` |
| 5c | Pedals in performance mode, `adaptive-demo` tool, docs (architecture, Windows validation) | this commit |

### Test status (Linux)

* **50/50** CTest tests pass on a JUCE-enabled configure (47 core + 3 JUCE).
* New: `cadenza_adaptive_midifile_tests`, `cadenza_adaptive_follower_tests`,
  `cadenza_adaptive_transport_tests`, `cadenza_adaptive_backing_tests`,
  `cadenza_adaptive_session_tests`, `adaptive_demo_writes_song`.
* Required scenarios covered: exact, slower, faster, gradual acceleration,
  gradual deceleration, wrong note, extra note, missing note (+ missing chord
  tones), broken chord, repeated notes (also slower), pause, resume, confidence
  loss, re-acquisition (+ restart from the top, identical-repeat tie-break),
  tempo smoothing (+ estimator limits / slew / pause / slow start), phase
  correction (ahead and behind), empty reference, invalid input (pitches,
  timestamps, files: empty / garbage / SMPTE / 0 PPQ / truncated / missing),
  tempo-map changes, different PPQ, long sequence (3000 groups with errors),
  dense chords; closed-loop transport (steady, slower, accel, decel, rubato,
  sloppy timing, pause); end-to-end session (band follows a slower performer
  within 30 ms), stop / load while playing, multi-threaded smoke test.
* Extra verification (not in CTest): ThreadSanitizer on the session test,
  AddressSanitizer + UBSan on all adaptive tests, 30 000-mutation fuzz of the
  SMF reader + song builder; stress sweep (random pieces × rates 0.6–1.5 ×
  jitter ≤ 80 ms × 0–25 % errors): mean position accuracy ≥ 96 % (worst run 94 %), every run reached
  the end.
* App smoke test under Xvfb: app starts, Adaptive Song window renders, a MIDI
  file loads (piano auto-selected as reference, other tracks as backing),
  Play → READY, on-screen keyboard notes → TRACKING → WAITING, closing the
  window restores the arranger. This found and fixed a real conflict: the Style
  Recorder capture mode (on while an editable `.cstyle` is loaded) swallowed
  every key — Adaptive mode now suspends it.

## CURRENT

* Waiting for Windows validation (MSVC build + real keyboard + real audio).

## NEXT

1. Windows validation per `WINDOWS_VALIDATION.md`; fix anything MSVC flags.
2. Tune follower/transport defaults with real players (log analysis).
3. Persist the Adaptive window settings (last file, tracks, sliders) in `settings.json`.
4. Optional count-in / metronome for "Wait for me"; practice start bar (API exists: `AdaptiveSongSession::setStartGroup`).
5. Show warnings/remaps more prominently; per-backing-track mute in the window.
6. Anchor Mode (manual anchors without a reference MIDI) — designed for, not started.
7. Use note-offs / pedal as weak evidence in the follower (sustained-chord passages).

## KNOWN ISSUES

* No real-time audio/MIDI hardware was available in the cloud: sound, latency
  and feel are unverified (simulation only).
* Truly identical repeated passages are disambiguated only by "nearest after the
  last known position".
* Ornaments absent from the reference count as extra notes.
* Block-rate event scheduling (same as `StyleEngine`).
* Pre-existing upstream observations (not changed): `StyleEngine::install()` and
  several `MidiRouter` callbacks are assigned after the audio/MIDI devices are
  started (benign in practice; Adaptive hooks are installed before).
  Cadenza did not forward the sustain pedal to the live voices at all; Adaptive
  performance mode now does (CC64/66/67), Arranger Mode is unchanged.

## CLOUD LIMITATIONS

* Linux container (no MSVC, no ASIO, no audio device, no `/dev/snd/seq`).
* The full JUCE app builds and starts on Linux, but the audio callback never
  runs (no output device), so the backing playhead is exercised only by tests.

## WINDOWS VALIDATION NEEDED

See [`WINDOWS_VALIDATION.md`](WINDOWS_VALIDATION.md). Specifically:

* MSVC Debug + Release build of all targets, all tests green.
* Real keyboard: following, tempo changes, mistakes, pause/resume, loss and
  re-acquisition, Stop, window close → Arranger Mode works again.
* Sound through FluidSynth, a per-part VST3 and the master multitimbral VST3.
* Channel remapping with the user's Right/Left voice setup.
* MIDI timestamps vs audio clock on Windows MIDI drivers (latency / phase).
* UI layout at Windows DPI scaling.
