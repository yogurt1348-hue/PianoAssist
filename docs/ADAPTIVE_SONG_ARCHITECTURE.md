# Adaptive Song Mode — Architecture

> Status: living design document. The source of truth for *progress* is
> [`ADAPTIVE_SONG_PROGRESS.md`](ADAPTIVE_SONG_PROGRESS.md).

Adaptive Song Mode lets a live MIDI pianist drive the *time* of a pre-recorded
backing arrangement. The performer plays their part (the **reference** track);
Cadenza follows them through the score and plays every other track (the
**backing** tracks) so that the band follows the player — not the other way
round.

Nothing here is specific to a song, keyboard, tempo or MIDI layout: the inputs
are any Standard MIDI File and any MIDI keyboard Cadenza can already open.

---

## 1. The existing Cadenza architecture (as found in upstream `47da8f7`)

Studied before designing anything; the new mode is built *around* these pieces,
not instead of them.

| Concern | Where | Notes |
|---|---|---|
| Build | `CMakeLists.txt` | `cadenza_core` = pure C++20 static lib (no JUCE). All unit tests link only `cadenza_core` and are plain `main()` programs with an `expect()` helper, run by CTest. The JUCE app (`Cadenza` target) is only configured when `lib/JUCE` exists. |
| MIDI input | `Source/Midi/MidiRouter.{h,cpp}` (JUCE adapter) → `Source/Midi/ArrangerMidiRouter` (pure core) | `juce::MidiInputCallback`; opens all non-aux inputs, hot-plugs every ~2 s from `MainComponent::timerCallback`. Every note goes through split routing, chord recognition, Syncro Start/Stop, right/left live voices. Callbacks fire **on the MIDI thread**. |
| Live sound of the keyboard | `RightHand`, `LiveMelodyVoice` | Right 1/2/3 layers on channels 1..3 by default (`kLiveMelodyChannel = 1`), optional left voice. |
| Transport | `Source/Audio/Transport.{h,cpp}` | Fixed-BPM PPQ clock. `advance(frames)` is called on the audio thread; fractional tick position. Tempo-map unaware. Message thread talks to it through `TransportCommandMailbox` (Play / Stop / SetBpm; `try_lock` consumer). |
| Audio callback | `Source/Audio/AudioEngine.cpp::getNextAudioBlock` | `runAudioBlock` ordering contract (`AudioBlockPipeline.h`): 1) consume transport commands, advance transport, call the single `m_onTick(ticks, transport)` hook; 2) render FluidSynth + per-part VST3 + master multitimbral VST3; 3) metronome; 4) master chain. |
| Sound output | `AudioEngine::noteOn/noteOff/controlChange/programChange/pitchBend` | Thread-safe entry points, already called from the audio thread by `StyleEngine`. They route a 1-based channel to: master multitimbral VST3 → per-part VST3 (`MidiMessageCollector`) → FluidSynth (`SynthEngine`, or `NullSynthEngine` when FluidSynth is absent). **This is the rendering pipeline Adaptive Song Mode reuses unchanged.** |
| Accompaniment | `Source/Arranger/StyleEngine.{h,cpp}` | Owns `AudioEngine::setOnTick`. Loops style *sections* (pattern loops) and transposes them to the live chord. Not a linear song player. |
| "Song mode" | `Song`, `SongLoader`, `SongPlayer` | A **chord chart** (`.csong`: bar → section + chord) stepped from the message-thread timer. It is *not* MIDI-song playback. |
| MIDI file parsing | `StyParser.cpp` (pure core, internal helpers, style-oriented, first tempo only), `MidiStyleConverter.cpp` (JUCE `juce::MidiFile`, app-only) | Neither gives a generic multitrack, tempo-mapped song model. |
| Mixer | `MixerModel`, per-channel VST3 in `AudioEngine` | Per-channel volume/mute/solo/program. |

**Conclusion.** Cadenza has no generic linear multitrack MIDI playback and no
tempo map. It does have a clean real-time output path (`AudioEngine` note API),
a pure-C++ core with a fast Linux-buildable test suite, and the MIDI input
layer. Adaptive Song Mode therefore adds:

* a small pure-C++ Standard MIDI File reader with tempo map (core),
* the adaptive core (reference model, follower, tempo estimator, transport
  controller, backing scheduler) — all pure C++ in `cadenza_core`, unit-tested,
* a thin JUCE integration layer (`AdaptiveSongEngine`) plus one optional hook
  in `AudioEngine` and one optional tap in `MidiRouter`.

The Arranger (`StyleEngine`) path is untouched; the shared `Transport` is not
used by Adaptive Song Mode at all (it keeps its own adaptive playhead), so the
two modes cannot fight over tempo or PPQ.

---

## 2. Module map (new code)

All new core code lives in `Source/Adaptive/` (namespace `cadenza::adaptive`)
and is compiled into `cadenza_core`.

```
Source/Adaptive/
  MidiFile.{h,cpp}            SMF format 0/1 reader → MidiFileData (all tracks, all events)
  TempoMap.{h,cpp}            tick <-> nominal seconds, BPM at tick, time signatures
  ReferencePerformance.{h,cpp} reference notes + chord/note groups (musical time)
  TempoEstimator.{h,cpp}      robust tempo (rate) estimate with smoothing + limits
  ScoreFollower.{h,cpp}       online windowed alignment + global re-acquisition + state machine
  AdaptiveTransport.{h,cpp}   PLL-like playhead controller (tempo + phase), hold/brake, seek policy
  BackingSequence.{h,cpp}     flattened backing events + RT-safe range renderer (MidiEventSink)
  SongTimeline.{h,cpp}        track selection, reference/backing split, two-file compatibility check
  TripleBuffer.h              wait-free single-writer/single-reader snapshot publication
  AdaptiveSongSession.{h,cpp} glue: owns the above, thread contracts (pure C++, testable)
Source/Adaptive/AdaptiveSongEngine.{h,cpp}  (JUCE app only) wires session to AudioEngine/MidiRouter
```

---

## 3. Data flow

```
 MIDI keyboard
     │  (JUCE MidiInput thread)
     ▼
 MidiRouter::handleIncomingMidiMessage ──► existing arranger / live voices (unchanged)
     │
     └─ raw-note tap (new, optional)  noteOn(pitch, vel, timestampSec)
            │
            ▼
   AdaptiveSongSession::onLiveNote  (mutex, NOT the audio thread)
            │
            ▼
   ScoreFollower ── TempoEstimator
            │  FollowerSnapshot {anchorTick, anchorTime, rate, confidence,
            │                    state, holdTick, epoch, ...}
            ▼
   TripleBuffer<FollowerSnapshot>  (wait-free publish)
            │
            ▼   (audio thread, AudioEngine block hook, every block)
   AdaptiveTransport::process(nowSec, dt) → playhead tick range (from, to]
            │
            ▼
   BackingSequence::render(from, to, sink)  (no alloc, no locks)
            │
            ▼
   AudioEngine::noteOn/noteOff/CC/program/pitchBend  →  FluidSynth / VST3 / master VST3
```

The reference track is **never** sent to the sink — it is only the map the
follower reads. The performer hears themselves through Cadenza's normal live
voice path (Right 1, etc.).

---

## 4. Musical time model

* Every position is in **reference PPQ ticks** (`double` where fractional).
* `TempoMap` converts ticks ⇄ *nominal seconds* (the time the score would take
  at its written tempo map) and gives the written BPM at any tick.
* The follower and the transport work with a dimensionless **rate**:
  `rate = 1.0` means "playing at the written tempo map". A performer at 90 BPM
  in a 120 BPM song has `rate = 0.75`. With a tempo map the displayed BPM is
  `rate × writtenBpm(tick)`.
  This makes tempo changes inside the MIDI file free: the follower never has to
  know that bar 17 is written faster — the rate stays ~1 if the player follows
  the written change.

---

## 5. ReferencePerformance

Built from one or more tracks of a `MidiFileData` (optionally a channel filter).

* `ReferenceNote { pitch, onTick, offTick, velocity, channel, track, nominalOnSec }`
* `NoteGroup { tick (earliest onset), lastOnsetTick, nominalSec, pitches[] (unique, sorted), noteIndices[] }`
* **Chord grouping**: onsets are merged into one group while
  `onset − groupFirstOnset ≤ window`, where the window is
  `chordGroupingWindowMs` (default 35 ms, nominal time — so it is tempo-map
  aware) **or** `chordGroupingWindowTicks` if set. Measuring from the group's
  first onset prevents a fast run from chaining into one giant "chord".
  Duplicate pitches inside a group collapse to one expected pitch.

---

## 6. Score follower algorithm

### 6.1 Why this algorithm

Candidates considered: per-note greedy matching (too fragile), HMM / particle
filters (heavier, harder to make deterministic and testable), audio-style
online DTW (designed for frame features, not discrete symbolic events), and
**Dannenberg-style online dynamic-programming alignment** of the symbolic
performance against the symbolic score. Our input is already symbolic (MIDI),
so the last family is the natural fit: deterministic, bounded cost per note,
explicit penalties for wrong / extra / missing notes, and easy to unit-test.

We use a **windowed Viterbi alignment over note groups** (local sequence
alignment) for tracking, plus a **global Smith-Waterman search over the recent
live history** for re-acquisition.

### 6.2 Tracking DP

State: a window of cells `j ∈ [lo, hi]` over reference groups (default 4
behind, 12 ahead of the best cell). Each cell holds the best path that ends
"in group j": score, which pitches of group j that path has already consumed
(a 128-bit mask), the time of the group's first matched onset, the previous
anchor, and a running match quality.

For each live note-on (pitch `p`, time `t`):

```
stay   : cell j, p ∈ group(j), p not yet consumed     → + matchScore − chordSpreadCost
advance: from cell i < j (j − i ≤ maxJump), p ∈ group(j)
         → + matchScore − skipCost·(missing notes between) − timingCost(t, expected_j)
extra  : stay in cell j, p unmatched                   → − extraNotePenalty
octave : like advance/stay but p ≡ group pitch mod 12  → reduced score (wrong octave)
```

* `expected_j` = anchor time + (nominalSec(j) − nominalSec(anchor)) / rate —
  the timing term is what disambiguates **repeated notes** and **identical
  pitches in adjacent groups**: the second C of `C C` is preferred only when
  enough time has passed.
* Skip cost is proportional to the number of *unplayed expected notes* skipped
  (incomplete current group + whole skipped groups), so a **missing note** is
  cheap, a missing bar is expensive.
* A **wrong note** costs `extraNotePenalty` and does not move the position; a
  wrong note followed by the right notes simply rejoins the path.
* A **broken / arpeggiated chord** is handled by `stay` transitions: notes of a
  group may arrive in any order within `chordToleranceSec`.
* Backward moves are allowed only inside the window and cost
  `backwardPenalty` per group (handles "I replayed the last chord").

The reported position is the best cell; position is only *committed* forward
when the best cell's margin over competing cells is large enough
(ambiguity guard).

### 6.3 Confidence

`confidence ∈ [0,1]` = EMA of per-note match quality on the best path
(1 = right pitch at the right time, 0 = unmatched) × an ambiguity factor from
the score margin to the best competing non-adjacent cell. Long silence does
**not** lower confidence (a rest is not an error); it drives `WAITING`.

### 6.4 Re-acquisition

The follower keeps the last N (default 8) live onsets. When `LOST`, each new
note runs a Smith-Waterman alignment of that history against the whole group
sequence (`O(groups × N)`, ~10⁴–10⁵ ops for a long song — done on the MIDI /
worker thread, never the audio thread). A candidate is accepted when its score
passes `reacquireMinMatches` and beats the second-best distinct location by a
margin; ties (repeated choruses) resolve to the candidate nearest *after* the
last known position. Acceptance re-seeds the tracking DP there, bumps the
snapshot `epoch` and enters `REACQUIRING`; after `reacquireConfirmNotes`
consistent matches it returns to `TRACKING`.

### 6.5 State machine

```
IDLE ──first match──► TRACKING ◄──confidence ≥ hi──┐
                         │  confidence < lo         │
                         ▼                          │
                     UNCERTAIN ─────────────────────┘
                         │  N consecutive misses / confidence < lost
                         ▼
 TRACKING/UNCERTAIN ──no progress past expected onset + grace──► WAITING
 WAITING ──next match──► TRACKING
 WAITING ──waitToLostSec──► LOST (position kept as hint)
 LOST ──global candidate──► REACQUIRING ──confirm──► TRACKING
                              └──fails──► LOST
```

---

## 7. Tempo estimator

Input: anchor pairs (nominalSec of matched group onset, live time), one per
newly entered group.

1. Pairs separated by a gap longer than `pauseGapSec` start a new segment
   (a fermata / pause must not be read as a very slow tempo).
2. Instantaneous rate = slope of a least-squares line through the last
   `regressionWindow` (default 6) pairs of the current segment — robust to
   single-note timing jitter.
3. Outlier gate: samples outside `[rate/2, rate·2]` are ignored.
4. Smoothing: EMA (`smoothing`, default 0.35) on the rate.
5. Limits: clamp to `[minBpm, maxBpm]` (expressed via the written BPM at the
   position) and slew-limit to `maxTempoChangeBpmPerSec`.

All parameters live in `TempoEstimatorConfig`.

---

## 8. Adaptive transport (phase-locked playhead)

`AdaptiveTransport` is pure math, allocation-free, called once per audio block.

Inputs: latest `FollowerSnapshot`, `now`, `dt`. Output: new playhead tick,
playback rate, and flags (`seeked`, `holding`).

* **Target position** `P* (now)` = anchorTick advanced by
  `(now − anchorTime) × rate` nominal seconds through the tempo map, capped at
  `holdTick` (the next reference group the performer has not reached yet, plus
  a small tolerance). The backing therefore never runs past an event the
  performer has not played.
* **Tempo error vs phase error**: the base playback rate is the performer
  rate; the phase error `e = nominalSec(P*) − nominalSec(playhead)` adds a
  bounded correction:

  ```
  correction = clamp(e / phaseCorrectionTimeSec, ±maxPhaseCorrection·rate)
  rate_cmd   = rate_performer + correction            (PLL: P on phase, rate as feed-forward)
  playbackRate → rate_cmd with slew limit maxRateSlewPerSec
  ```

  So "same BPM but half a bar ahead" produces a temporarily *slower* backing
  until the phase converges — `setBpm(100)` alone could never fix that.
* **Braking**: near `holdTick` the rate is limited so the playhead approaches
  the hold point asymptotically (`rate ≤ distance / brakeTimeSec`): a performer
  who stops makes the band decelerate and wait at the next performer event,
  not run away.
* **Confidence gating**: `UNCERTAIN` scales the phase gain down (no aggressive
  corrections); `WAITING`/`LOST` hold.
* **No backwards motion** in normal operation (a sounded snare cannot be
  undone): the playhead is monotonic; a backing that is ahead only slows down.
* **Hard seek** only when (a) the snapshot `epoch` changed (re-acquisition or
  user seek) **and** |e| > `seekThresholdSec`, or (b) a confirmed large
  desync |e| > `largeDesyncSec` persisted for `largeDesyncHoldSec` while
  TRACKING. A seek releases sounding backing notes and chases controllers.
* **Long hold** (> `releaseAfterHoldSec`) releases sustained backing notes so a
  paused performer doesn't leave a pad droning.

---

## 9. Backing sequence & output

`BackingSequence` flattens the selected backing tracks into one tick-sorted
array of channel events (note on/off, CC, program, pitch bend). `render(from,
to, sink)` emits events with `from < tick ≤ to` through a virtual
`MidiEventSink` (in the app: `AudioEngine`). It keeps a fixed 16×128
active-note table (no allocation) so stop / seek / long hold can release only
what it started, and a per-channel controller snapshot for chasing after a
seek. Channel remap lets backing avoid the live performer's channels.

Output goes through the existing `AudioEngine` API, so FluidSynth, per-part
VST3 instruments, the master multitimbral VST3, mixer volumes and the master
chain all work unchanged.

---

## 10. Threading model

| Thread | Does | Must not |
|---|---|---|
| MIDI input (JUCE) | `MidiRouter` → existing routing; raw tap → `AdaptiveSongSession::onLiveNote` (short mutex, follower DP; re-acquisition search when LOST) | touch audio objects |
| Message (UI timer 20 Hz) | `session.update(now)` (WAITING / LOST timeouts), read diagnostics, load files (parsing + building models happen here, then swapped in while stopped) | — |
| Audio | `AdaptiveSongEngine` block hook: read snapshot (wait-free `TripleBuffer`), `AdaptiveTransport::process`, `BackingSequence::render` → `AudioEngine` note API | lock, allocate, parse, log, run the DP |

Loading a song only happens while Adaptive playback is stopped; the audio
thread sees immutable `shared_ptr` data published through an atomic handoff.

---

## 11. Two-file mode (reference.mid + backing.mid)

`SongTimeline` can take the reference and the backing from different files.
It does **not** assume they line up:

* different PPQ → backing ticks are rescaled to the reference PPQ (exact
  rational rescale, rounding reported);
* tempo maps are compared in nominal seconds at every tempo event; if they
  differ beyond a tolerance the result carries a `timelineMismatch` warning
  that the UI must show (and the backing's own tempo map is used to place its
  events in time, then mapped onto the reference ticks);
* a large difference in total length is reported too.

---

## 12. Limitations (current)

* Symbolic MIDI only (no audio input).
* Matching uses note-ons; note-offs / pedal are ignored by the follower.
* Ornaments not in the reference (trills, grace notes) count as extra notes.
* Block-rate scheduling (same as `StyleEngine`): events land at block start
  (~5 ms at 256 frames / 48 kHz).
* Anchor Mode (manual anchors without a reference MIDI) is designed for but
  not implemented.
