# Piano Practice MVP architecture

## Design principle

The musical timeline is the authority. UI rendering, MIDI input, and audio rendering are adapters around a deterministic lesson engine. No UI callback may decide whether a learner passed a gate.

## Modules

| Module | Owns | Must not own |
| --- | --- | --- |
| `app` | JUCE startup, commands, persistence wiring | Musical decisions |
| `midi_io` | MIDI device enumeration, input callbacks, output diagnostics | Scoring state |
| `smf` | File loading, tracks, PPQ, tempo and time-signature maps | JUCE widgets |
| `timeline` | Immutable normalized note/event model and queries | Real-time audio objects |
| `lesson_engine` | Sections, gate requirements, progression, scoring events | Rendering/audio threads |
| `transport` | State machine and monotonic transport clock | MIDI parsing |
| `audio` | Synth rendering, event scheduler, gain, device lifecycle | Gate matching policy |
| `ui` | View models and drawing | Mutable engine internals |
| `diagnostics` | Structured logs, session snapshot, device report | User music files without consent |

## Core data flow

```text
SMF -> normalizer -> immutable Timeline -> LessonEngine -> Transport state
USB MIDI callback -> timestamped InputEvent ----^                 |
                                                         Scheduler -> Audio engine
Timeline + Transport + Gate state -> UI view model -> piano roll
```

## Normalized timeline

Normalize each note to `{id, trackId, channel, pitch, velocity, startTick, endTick, startSeconds, endSeconds, role}`. Preserve source tracks and channels. `role` is initially inferred (`rightHand`, `leftHand`, `backing`, `unknown`) and can be corrected by user-facing mapping later. Convert ticks to seconds using the tempo map; never assume a fixed tempo.

## Transport states

`Stopped`, `PreRoll`, `Playing`, `WaitingForGate`, `Paused`, `Completed`, `Error`. Each transition is validated by one reducer-like owner on a non-audio control thread. State changes emit immutable snapshots for UI and audio scheduling.

`WaitingForGate` freezes timeline advancement at a musical boundary. It does not ask the audio engine to freeze a signal already emitted. Existing voices may decay naturally; new future events are not scheduled. On entering wait, send note-offs for transport-owned notes and CC64=0 unless a later instrument policy says otherwise. This is a best-effort musical stop, not a sample-accurate mute.

## Voice tails and latency

MIDI is a control protocol; it has no audio samples to freeze. After note-off, a synth or later VST3 may render a release tail. A physically modelled piano can also sustain due to pedal/resonance. The app should display `Playback held; sound may ring out` while tails are possible. A separate emergency `Silence` command may panic/reset voices, but it is intentionally not the normal gate action because it sounds unnatural.

Audio output latency delays what the user hears; input latency/jitter delays observed key presses. Gate matching uses timestamped input against the logical timeline, calibrated by a configurable input offset and bounded tolerance. Audio latency must not silently shift the scoring clock. Show measured/reported device latency in diagnostics, and keep a manual calibration offset for future refinement.

## Threading and real-time rules

- MIDI input callback timestamps and pushes fixed-size events into a lock-free queue; no allocation, locks, logging, or UI calls.
- Audio callback consumes a precomputed event window and renders; no disk I/O, allocation, mutex, or timeline mutation.
- Control thread parses MIDI, computes gates, owns transport transitions, and publishes immutable snapshots atomically.
- UI thread reads snapshots and posts intents only.

## Extensibility boundary

Define `IInstrumentRenderer` with prepare, schedule events, render, all-notes-off, and latency-report methods. Implement `InternalSynthRenderer` now. A future `Vst3Renderer` remains isolated in `audio`; VST3 plug-in discovery/scanning must never affect `lesson_engine` or file format code.

