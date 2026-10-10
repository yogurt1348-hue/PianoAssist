# Adaptive Song Mode — Progress (source of truth)

## UPSTREAM BASE

* Upstream: <https://github.com/Piskocis/Cadenza> (`master`)
* Imported commit: **`47da8f71670daa4caa1b3db2bf8f60a323f6c6b1`** ("feat: add polished master DSP")
* Imported with full upstream history (178 commits) into
  `yogurt1348-hue/PianoAssist`, branch `claude/gifted-einstein-6xjycw`.
  Git remote `upstream` = `https://github.com/Piskocis/Cadenza.git`.
* License: GPLv3 (unchanged). All new code is original and GPLv3.

## BASELINE (Phase 0)

* Linux, GCC 13.3, CMake 3.28, Ninja.
* `cmake -S . -B build-linux -G Ninja && cmake --build build-linux && ctest --test-dir build-linux`
  → `cadenza_core`, tools and **41/41 tests pass** without JUCE.

## PLAN

| Phase | Content | Verification |
|---|---|---|
| 0 | Import upstream, baseline, docs | 41/41 baseline tests |
| 1 | `MidiFile` reader, `TempoMap`, `ReferencePerformance` (+ chord groups) | unit tests (synthetic SMF bytes) |
| 2 | `TempoEstimator`, `ScoreFollower` (windowed DP + global re-acquire + state machine) | scenario tests 1–22 |
| 3 | `AdaptiveTransport` (PLL playhead, hold/brake, seek policy) | closed-loop simulation tests |
| 4 | `BackingSequence` + `SongTimeline` (track selection, two-file check) | unit tests |
| 5 | `AdaptiveSongSession` (threads, snapshot), app integration (`AudioEngine` hook, `MidiRouter` tap, `AdaptiveSongEngine`) | core tests; Linux JUCE build if possible |
| 6 | Minimal UI (Adaptive Song window) | Windows validation |

## DONE

* Phase 0 — upstream imported, baseline green, architecture studied and documented
  (`ADAPTIVE_SONG_ARCHITECTURE.md` §1).

## CURRENT

* Phase 1.

## NEXT

* Phase 2.

## KNOWN ISSUES

* (none yet)

## CLOUD LIMITATIONS

* Linux container; Cadenza targets Windows/MSVC. The pure core and tests build
  and run here. Whether the JUCE app builds on Linux is being checked
  (JUCE 8.0.13 cloned into the git-ignored `lib/JUCE`).
* No MIDI keyboard / audio device in the container: real-time behaviour is
  tested by deterministic simulation only.

## WINDOWS VALIDATION NEEDED

* See `WINDOWS_VALIDATION.md` (written in Phase 5/6).
