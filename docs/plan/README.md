# Piano Practice MVP brief for Claude

This folder is the implementation brief for a Windows desktop MVP that teaches a user to play MIDI arrangements on a Casio CDP-S110 connected by USB MIDI. The MVP uses C++ and JUCE, plays local Standard MIDI Files, renders piano-roll notation, and advances only when the user performs required notes within configured tolerances.

Read the files in this order:

1. `01-master-plan.md` - scope, sequencing, milestones, and decisions.
2. `02-architecture.md` - boundaries, data flow, concurrency, and timing contract.
3. `03-phase-0-foundation.md` through `08-phase-5-release-readiness.md` - build work in small, independently testable increments.
4. `09-test-plan.md` and `10-acceptance-criteria.md` - verification contract.
5. `11-claude-workflow.md` - how Claude should implement and report each iteration.
6. `12-assumptions-open-questions.md` - explicit product decisions and items to revisit.

## Product boundary

MVP supports one local Windows user and one USB MIDI input device at a time. It has no accounts, cloud services, recording export, audio-file playback, time-stretching, or VST3 hosting. MIDI playback uses an internal synth first; the audio-engine boundary is deliberately designed so VST3 support can arrive later without changing lesson logic.

## Non-negotiable behaviour

Stopping or holding MIDI transport does **not** instantly silence notes already sounding in audio. The system stops future MIDI scheduling and sends appropriate MIDI note-offs / sustain reset, but an instrument may keep audible release tails. The UI must describe this accurately and must not claim that a transport pause is an instantaneous audio freeze.

