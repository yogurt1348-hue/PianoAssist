# Piano Practice MVP acceptance criteria

## Functional acceptance

- On Windows, user selects or reconnects a USB MIDI piano and sees live note input without restart.
- User opens a valid local format-0 or format-1 MIDI file and sees a tempo-correct piano roll.
- Internal playback follows tempo changes and `Stop`, `Pause`, `Seek`, and `Silence` have distinct documented behaviour.
- In each practice mode, only selected hand requirements gate progression; optional other piano part can be toggled.
- A correct note/chord passes a gate within tolerance; wrong pitches never pass it; empty required spans progress automatically.
- Backing tracks play where configured and do not become accidental practice requirements.
- Malformed/unsupported MIDI and device loss result in recovery guidance, not crash, hang, or stuck state.

## Reliability acceptance

- 30-minute fixture plays/practises with no unbounded memory increase or cumulative visible playhead drift beyond 20 ms from logical reference at sampled checkpoints.
- Repeated play/stop/seek/gate cycles leave no active transport-owned notes.
- App can be launched with no MIDI device and supports later connection.
- All critical automated tests pass in Debug and Release builds.

## User experience acceptance

- At a wait, the learner can identify required pitches and selected practice mode without reading logs.
- UI says that held playback may continue to ring out, and provides a separate emergency silence action.
- Main actions are reachable by keyboard and legible at 200% Windows scaling.

## Sign-off evidence

Record build version, Windows version, audio configuration, MIDI device name, fixture set version, automated test result, and manual checklist outcome. Known limitations must link to a tracked issue with severity and workaround.

