# Piano Practice MVP master plan

## Goal

Create a reliable practice player for a Casio CDP-S110. A learner loads a local MIDI file, sees upcoming notes, hears accompaniment as configured, and progresses through playable material only after satisfying the note gate. The result must feel musically predictable before it becomes feature-rich.

## Scope and priority

| Priority | Deliverable | MVP decision |
| --- | --- | --- |
| P0 | Windows JUCE application, device selection, MIDI diagnostics | Required |
| P0 | Local multitrack SMF parser, tempo map, internal synth playback | Required |
| P0 | Piano-roll visualisation and playhead | Required |
| P0 | Strict note-gated transport with configurable tolerance | Required |
| P0 | Both/right/left hand practice and optional playback of non-practised part | Required |
| P0 | Automatic handling of fragments that require no piano input | Required |
| P1 | Basic auto-accompaniment derived from non-piano tracks | Required if a usable backing track exists in source MIDI |
| P2 | VST3 instrument hosting | Deferred behind stable audio abstraction |
| P2 | WAV/audio files and time-stretching | Deferred |

## Delivery sequence

| Phase | Outcome | Exit evidence |
| --- | --- | --- |
| 0 | Repeatable build, logs, test fixtures, domain types | App opens and automated tests run locally |
| 1 | Casio/device input captured with monotonic timestamps | Input monitor shows correct note and pedal events |
| 2 | MIDI file loads, plays through internal synth, and draws correctly | Reference fixtures align audibly and visually |
| 3 | Gate engine controls playhead and accepts/rejects performance deterministically | Unit/integration gate matrix passes |
| 4 | Hand modes, accompaniment and automatic non-piano traversal work | End-to-end practice scenarios pass |
| 5 | Installer-ready reliability, accessibility, diagnostics | Acceptance suite passes on clean Windows machine |

## Milestones

M1 is a technical vertical slice: select MIDI input, load a small format-1 MIDI, hear it, and draw it. M2 adds deterministic gating for one hand. M3 supports all practice modes and accompaniment. M4 is release candidate hardening.

## UX decisions

- Default view: horizontal piano roll, time left-to-right, keyboard at bottom, current playhead centred where possible.
- Required notes are colored by hand; accepted notes change state immediately; wrong notes are visible briefly but do not alter the score.
- Start begins a count-in only for free-running pre-roll. When a gate is reached, the UI says `Waiting for: C4 E4 G4`.
- Default tolerance: onset window 120 ms early / 180 ms late; velocity ignored; exact pitch required. All values live in a named practice profile, not scattered constants.
- Chords are order-independent inside the onset window. Duplicate pitch requirements are counted only when distinct overlapping same-pitch notes exist.

## Explicit non-goals

Do not fake notation engraving, claim DAW-level plug-in compatibility, implement network synchronisation, or add audio time-stretch in this MVP. Keep data local. Design extension seams rather than speculative feature implementations.

