# Adaptive Song Mode — Architecture

> Living design document. Progress / status: [`ADAPTIVE_SONG_PROGRESS.md`](ADAPTIVE_SONG_PROGRESS.md).
> Windows checklist: [`WINDOWS_VALIDATION.md`](WINDOWS_VALIDATION.md).

Adaptive Song Mode lets a live MIDI pianist drive the *time* of a pre-recorded
backing arrangement. The performer plays their part (the **reference** part);
Cadenza follows them through the score and plays every other selected track
(the **backing**) so the band follows the player — not the other way round.

Nothing is specific to a song, keyboard, tempo or MIDI layout: the inputs are
any Standard MIDI File (format 0 or 1) and any MIDI keyboard Cadenza can open
(or the on-screen keyboard).

---

## 1. The existing Cadenza architecture (upstream `47da8f7`)

Studied before designing; the new mode is built *around* these pieces.

| Concern | Where | Notes |
|---|---|---|
| Build | `CMakeLists.txt` | `cadenza_core` = pure C++20 static lib (no JUCE). Unit tests are plain `main()` programs with an `expect()` helper, run by CTest. The JUCE app (`Cadenza`) is configured only when `lib/JUCE` exists. |
| MIDI input | `Source/Midi/MidiRouter` (JUCE adapter) → `Source/Midi/ArrangerMidiRouter` (pure) | `juce::MidiInputCallback`; opens all non-aux inputs, hot-plug rescan every ~2 s. Split routing, chord recognition, Syncro Start/Stop, Right 1/2/3 + Left live voices. Callbacks run **on the MIDI thread**. |
| On-screen keyboard | `NativePanel` → `MidiRouter::injectNote` | Same routing as hardware. |
| Transport | `Source/Audio/Transport` | Fixed-BPM PPQ clock advanced on the audio thread; no tempo map. Message thread → `TransportCommandMailbox` (`try_lock` consumer). |
| Audio callback | `AudioEngine::getNextAudioBlock` | `runAudioBlock` order: 1) transport + `m_onTick` (StyleEngine) 2) FluidSynth + per-part VST3 + master VST3 3) metronome 4) master chain. |
| Sound output | `AudioEngine::noteOn/noteOff/controlChange/programChange/pitchBend` | Thread-safe; already called from the audio thread by `StyleEngine`. Routes a 1-based channel to the master multitimbral VST3, a per-part VST3, or FluidSynth (`NullSynthEngine` without FluidSynth). **Reused unchanged.** |
| Accompaniment | `StyleEngine` | Loops style *sections*, transposed to the live chord. Not a linear song player. |
| "Song mode" | `Song/SongLoader/SongPlayer` | A `.csong` **chord chart**, stepped per bar — not MIDI-song playback. |
| MIDI files | `StyParser.cpp` (style-oriented, first tempo only), `MidiStyleConverter.cpp` (JUCE, app-only) | No generic multitrack tempo-mapped model. |
| Style Recorder | `MainComponent` + `MidiRouter` capture mode | While an editable `.cstyle` is loaded, capture mode sends **every** key to the recorder before any routing. |

**Conclusion.** Cadenza has no generic linear multitrack MIDI playback and no
tempo map, but it has a clean real-time output path, a pure-C++ core with a
Linux-buildable test suite, and the MIDI input layer. Adaptive Song Mode adds a
pure-C++ adaptive core plus a thin JUCE integration; the shared `Transport` and
`StyleEngine` are not used by the new mode (it runs its own adaptive playhead),
so the two modes cannot fight over tempo or PPQ.

---

## 2. Module map

Pure C++ (in `cadenza_core`, namespace `cadenza::adaptive`, `Source/Adaptive/`):

| File | Role |
|---|---|
| `MidiFile.{h,cpp}` | SMF format 0/1 reader + writer (fixtures). All channel events of all tracks, paired notes, tempo map, time signatures. Errors / warnings, never crashes (fuzzed). |
| `TempoMap.{h,cpp}` | ticks ⇄ nominal seconds, BPM at tick, bar/beat, tempo-curve equivalence across PPQ. |
| `PitchMask.h` | 128-bit note set (value type). |
| `ReferencePerformance.{h,cpp}` | Reference notes + chord/note groups in musical time. |
| `TempoEstimator.{h,cpp}` | Performer rate from matched onsets (regression, pauses, outliers, EMA, limits, slew). |
| `ScoreFollower.{h,cpp}` | Online windowed DP alignment, confidence, state machine, global re-acquisition. |
| `AdaptiveTransport.{h,cpp}` | PLL playhead in nominal time (tempo + phase), braking/hold, seek policy. RT-safe. |
| `BackingSequence.{h,cpp}` | Backing events on the reference timeline; RT renderer, note bookkeeping, controller chase. |
| `AdaptiveSong.{h,cpp}` | Track description + heuristics, single-file / two-file song building, compatibility checks, channel assignment. |
| `TripleBuffer.h` | Wait-free single-writer → single-reader snapshot publication. |
| `AdaptiveSongSession.{h,cpp}` | The threading contract (MIDI / message / audio) tying everything together. |

JUCE app only:

| File | Role |
|---|---|
| `Source/Adaptive/AdaptiveSongEngine.{h,cpp}` | Owns the session; AudioEngine block hook + `MidiEventSink` → `AudioEngine`; file loading; logging. |
| `Source/UI/AdaptiveSongPanel.{h,cpp}` | The Adaptive Song window. |
| `AudioEngine` (+9 lines) | `setOnBlock()` hook. |
| `MidiRouter` (+~50 lines) | Raw note tap, performance mode, pedal forwarding (performance mode only). |
| `NativePanel` (+5 lines) | "Adaptive Song..." button on the Setting page. |
| `MainComponent` | Wiring, window, mode switching. |

---

## 3. Data flow

```
 MIDI keyboard / on-screen keyboard
     │ (MIDI thread / message thread)
     ▼
 MidiRouter ──(normal mode)──► arranger routing, chord detection, live voices (unchanged)
     │  └─(performance mode)──► whole keyboard → Right 1/2/3 voices (+ pedals)
     │
     └─ raw tap: (note, velocity, isOn, timestampSec)
            ▼
   AdaptiveSongSession::onLiveNote   (short mutex; never on the audio thread)
            ▼
   ScoreFollower + TempoEstimator ──► FollowerSnapshot
            ▼
   TripleBuffer<FollowerSnapshot>    (wait-free)
            ▼  (audio thread: AudioEngine block hook, every block)
   AdaptiveTransport::process(now, blockSeconds) → tick range, seek / release flags
            ▼
   BackingSequence::render(from, to)  (no alloc, no locks)
            ▼
   AudioEngine::noteOn/... → master VST3 / per-part VST3 / FluidSynth → mixer chain
```

The reference part is **never** sent to the output: it is only the map the
follower reads. The performer hears themselves through Cadenza's own live voice
(Right 1 etc.).

---

## 4. Musical time model

* Positions are in **reference PPQ ticks**; the transport works in **nominal
  seconds** (time at the written tempo map) and converts with `TempoMap`.
* The performer's tempo is a dimensionless **rate**: 1.0 = as written, 0.75 =
  three quarters of the written tempo. Displayed BPM = `rate × writtenBpm(tick)`.
  Tempo changes written into the file therefore cost nothing: a performer who
  follows them keeps `rate ≈ 1` (tested).

---

## 5. ReferencePerformance

* `ReferenceNote { pitch, onTick, offTick, velocity, channel, track, onSec }`
* `NoteGroup { tick, lastOnsetTick, nominalSec, pitches (PitchMask), firstNote, noteCount, maxVelocity }`
* Built from selected track(s), optional channel filter (format-0 files), GM
  drum channel excluded by default.
* **Chord grouping**: an onset joins the current group while
  `onset − groupFirstOnset ≤ window`; window = `chordGroupingWindowMs`
  (default **35 ms nominal**, tempo-map aware) or `chordGroupingWindowTicks`.
  Measuring from the *first* onset keeps fast runs from chaining. Duplicate
  pitches collapse.

---

## 6. Score follower

### 6.1 Choice of algorithm

Candidates: greedy next-note matching (fragile), HMM / particle filters (heavier,
harder to keep deterministic), frame-based online DTW (built for audio
features). The input is symbolic, so we use the classic **online dynamic
programming alignment** of symbolic performance to symbolic score (Dannenberg
family): deterministic, bounded per-note cost, explicit penalties, testable.

* **Tracking**: windowed Viterbi / local alignment over note groups.
* **Re-acquisition**: global Smith-Waterman of the recent live history against
  the whole score.

### 6.2 Tracking DP

Window of cells over groups around the best position (`windowBehind` 3,
`windowAhead` 10). A cell holds the best path ending in that group: score,
consumed pitches (PitchMask), enter/last live time, path quality, consecutive
misses. Per live Note On (pitch `p`, time `t`):

| Transition | Condition | Score change |
|---|---|---|
| chord-note (stay) | `p ∈ group j`, not consumed | `+match − (late ? spreadPenalty : 0)` (late = after `chordSpreadSec` 0.18 s) |
| advance `i → j` | `p ∈ group j`, `j − i ≤ maxJump` (5) | `+match − skipPenalty·(unplayed notes of i + all notes between) − timingCost` |
| backward `i → j` | `i − j ≤ maxBackward` (2) | `+match − backwardPenalty·(i − j)` |
| extra / wrong | always | `− extraNotePenalty` (position unchanged) |

`timingCost` compares `t` with `expected = enterLive(i) + Δnominal / rate`,
tolerance `0.10 s + 30 % of the interval`, **late notes discounted** (×0.35:
being late is normal, being early is suspicious), smooth and capped
(`2·timingWeight`). This is what separates **repeated notes** and identical
pitches in adjacent groups. Scores are renormalised every note (best = 0);
ties go to the earlier group (never run ahead on a coin flip).

### 6.3 Confidence

`confidence = pathQuality × ambiguity`, where `pathQuality` is an EMA (0.3) of
per-note quality along the best path (1 = right note in time; lower for late
chord tones, timing deviation, skipped notes; 0 = extra) and `ambiguity ∈
[0.6, 1]` grows with the score margin to the best non-adjacent competitor.
Silence never lowers confidence (a rest is not an error) — it drives WAITING.

### 6.4 State machine

```
IDLE ──first matched note──► TRACKING ◄── confidence ≥ 0.6 ── UNCERTAIN
                                │  confidence < 0.45 ─────────────►┘
TRACKING / UNCERTAIN ── late for the next event by 0.35 s + 50 % IOI ──► WAITING
WAITING ── next match ──► TRACKING / UNCERTAIN
any ── 5 consecutive extra notes or confidence < 0.2 ──► LOST
LOST ── global candidate (≥ 4 matches, margin ≥ 1.0) ──► REACQUIRING ── 3 matches ──► TRACKING
REACQUIRING ── 2 misses ──► LOST            LOST ── local window recovers ──► TRACKING
```

### 6.5 Re-acquisition

When LOST, only the notes played since the follower got lost (starting with
the miss run that caused it) are aligned globally (`O(groups × notes × maxJump)`,
~10⁵ operations for a 3000-group song; MIDI thread). Among candidates within the
margin (identical repeated passages, e.g. choruses) the one **nearest at or
after the last known position** wins. Acceptance re-seeds the DP, re-anchors
the tempo segment and **bumps the snapshot epoch** (the transport may then hard
seek). `startAnywhere` uses the same search for the first notes.

---

## 7. Tempo estimator

Anchors = (nominal time of a newly entered group, live time of its first note).

1. A live gap > `pauseFactor (2.5) × expected` and > `pauseMinSec (1 s)` starts
   a new segment (a fermata is not a slow tempo). Before the tempo is known
   only gaps too long even for `minBpm` count as pauses (slow starters).
2. Measurement = recency-weighted least-squares slope over the last
   `regressionWindow` (4) anchors with ≥ 0.2 s nominal span.
3. The first two measurements are taken directly (fast acquisition).
4. Outliers outside `[rate/1.6, rate·1.6]` are ignored unless 3 in a row
   (a genuine big change).
5. EMA (`smoothing` 0.5), clamp to `[minBpm 30, maxBpm 300]`, slew limit
   `maxChangeBpmPerSec` 25.

---

## 8. Adaptive transport (PLL)

Pure math, allocation-free, once per audio block, in nominal seconds.

```
target  P* = min(anchor + (now − anchorLive) · rate,  hold + holdTolerance)
error   e  = P* − playhead
command r  = rate + clamp(gain · e / phaseCorrectionTime, ±maxPhaseCorrection · rate)
             (gain: TRACKING 1, UNCERTAIN/REACQUIRING 0.3, WAITING/LOST 0)
r          → slew-limited (maxRateSlewPerSec 0.8 /s), floor minRateFraction (0.4) · rate
late       = now > expected time of `hold` or playhead ≥ hold
if late:   r ≤ (hold + tolerance − playhead) / brakeTime
```

* **Tempo error** is removed by the feed-forward `rate`, **phase error** by the
  bounded correction — "same BPM, half a bar ahead" makes the band temporarily
  slower (tested: 0.5 s error → < 20 ms in 10 s, never more than 15 % slower).
* `hold` = nominal time of the next reference event the performer has not
  played; `holdTolerance` 0.10 s, `brakeTime` 0.10 s. A stopped performer makes
  the band brake and wait just past their next event; it resumes as soon as
  they play.
* **Monotonic** playhead (a sounded snare cannot be undone). The only
  repositioning is a **hard seek**: follower epoch change (re-acquisition, user
  seek, restart) with `|e| > seekThresholdSec` (0.6), or `|e| > largeDesyncSec`
  (2 s) persisting `largeDesyncHoldSec` (1.5 s) while TRACKING. A seek releases
  sounding notes and chases controllers.
* Held longer than `releaseAfterHoldSec` (1.5 s) → sounding backing notes are
  released once (no droning pads during a pause).
* Start modes: **WaitForPerformer** (default; the band starts at the first
  reference event with the performer's first note) and **FreeRunIntro** (the
  band plays the intro at the written tempo and waits at the performer's entry).
* Defaults were tuned with closed-loop simulations (performer → follower →
  transport at 256-frame blocks): steady ≈ 5 ms mean error (one block), accel /
  decel ≈ 15 ms, ±25 % rubato ≈ 60 ms, ±30 ms sloppy timing ≈ 3 % band-rate
  wobble.

---

## 9. Backing sequence & output

* Selected tracks (or channels of a track) are flattened into one tick-sorted
  event list on the reference timeline (note on/off, CC, program, pitch bend;
  NoteOff → controls → NoteOn on equal ticks).
* `render(from, to, inclusive)` is cursor based; events skipped by a jump are
  never played late. A 16 × 128 counter table records which notes *we* started:
  release-all on stop / seek / long hold, orphan note-offs suppressed.
* `seek(tick)` releases, repositions and **chases** program, bank, CC 1/7/10/11/
  64/71–74/91/93 and pitch bend.
* **Output channels**: identity, except channels used by the live keyboard
  (Right 1/2/3 and Left channels are reserved) which move to free channels
  (never 10 for melodic parts; drums stay on 10). Every remap is reported.
* Output via `MidiEventSink` → `AudioEngine`, so FluidSynth, per-part VST3
  instruments, the master multitimbral VST3, mixer volumes and the master chain
  apply unchanged. Per-backing-track instrument choice: by its MIDI program
  changes, or by loading a VST3 on its (output) mixer channel as usual.

---

## 10. Two-file mode (reference.mid + backing.mid)

* PPQ differences: backing ticks rescaled exactly (reported).
* Alignment: **ByBeats** (default; same musical positions) or **BySeconds**
  (same absolute time, mapped through both tempo maps).
* Tempo maps compared in nominal seconds at every tempo event of either file;
  a difference above 50 ms sets `timelineMismatch` and shows a warning — never
  silently assumed equal. Lengths differing by > 4 beats are reported.

---

## 11. Threading model

| Thread | Runs | Never |
|---|---|---|
| MIDI (JUCE MidiInput) | `MidiRouter` → raw tap → `session.onLiveNote` (follower DP under `m_followerMutex`, global search when LOST), publishes the snapshot | touches audio objects |
| Message (20 Hz timer + UI) | `session.update` (WAITING, retiring old songs), status, diagnostics, file parsing and song building, play/stop | — |
| Audio | `session.processBlock`: wait-free snapshot read, transport, backing render → `AudioEngine` | locks (only `try_lock` for the stopped-state handoff), allocates, parses, logs, runs the DP |

* Follower state is published through `TripleBuffer` (writers serialised by
  the follower mutex; one reader: the audio thread).
* A new song is handed over with a `try_lock` swap only while stopped; the old
  one is parked and freed on the message thread (never deallocated on the audio
  thread). Play/stop are an atomic generation + flag.
* Hooks (`AudioEngine::setOnBlock`, `MidiRouter::setRawNoteCallback`) are
  installed once in the `MainComponent` constructor **before** audio and MIDI
  devices start.
* Verified with a real multi-threaded test under ThreadSanitizer, and the core
  tests under AddressSanitizer + UBSan (see progress doc).

---

## 12. App integration & mode switching

* **Setting page → "Adaptive Song..."** opens the window (Load MIDI, separate
  backing MIDI, reference track + channel, backing tracks, MIDI input, start
  mode, tempo smoothing / tracking sensitivity / wait timeout, diagnostics,
  Play/Stop, status).
* Pressing Play enters the mode: the arranger band is stopped, `StyleEngine` is
  disabled, the Style Recorder's capture mode is suspended (it would swallow all
  keys), and `MidiRouter` performance mode makes the whole keyboard the
  performer's instrument (no split / chords / Syncro) with sustain/sostenuto/
  soft pedals forwarded to the Right voices.
* Closing the window (or pressing the arranger Play button) leaves the mode and
  restores StyleEngine, capture mode and normal routing. A MIDI-mapped
  "Start/Stop" button toggles the adaptive song while the mode is on.
* Diagnostics: state transitions are always logged (rare); per-note lines
  (`note, kind, group, expectedTick, followerTick, playheadTick, phase, rate,
  confidence, margin, state`) in Debug builds, with `CADENZA_ADAPTIVE_DEBUG=1`,
  or via the window's checkbox. Log file: `%APPDATA%/Cadenza/cadenza.log`.

---

## 13. Limitations

* Symbolic MIDI only (no audio input); note-ons only (note-offs/pedal are not
  used for following).
* Ornaments absent from the reference (trills, grace notes) count as extra
  notes; long improvised passages lead to LOST (then re-acquisition).
* Truly identical repeated passages are disambiguated only by "nearest after
  the last known position".
* Block-rate scheduling, like `StyleEngine` (events land at the block start).
* `NullSynthEngine` builds are silent (no FluidSynth).
* No count-in / metronome for WaitForPerformer; no manual Anchor Mode yet.
* Settings of the window are not persisted yet.
