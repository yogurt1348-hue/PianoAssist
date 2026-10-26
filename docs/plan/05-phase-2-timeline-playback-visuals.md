# Phase 2 Timeline playback and visuals

## Objective

Load common local MIDI files, create one canonical timeline, play it through an internal instrument, and render a legible piano roll.

## Work items

1. Accept SMF format 0 and format 1. Reject format 2 with a clear message for MVP.
2. Parse tempo changes, track names, program changes, note events, end-of-track, and time signatures. Pair notes robustly; repair only predictable missing note-offs by closing at track end and report the repair.
3. Provide track/role mapping defaults: channels/tracks below a split heuristic become left hand, above become right hand; percussion and explicitly named backing tracks are backing. Present mapping in diagnostics because inference can be wrong.
4. Implement `InternalSynthRenderer` using JUCE-supported facilities. Schedule ahead from logical transport time; avoid per-block dynamic work.
5. Draw zoomable/scrollable piano roll, current required notes, playhead, and active keyboard keys. Rendering uses timeline seconds, not raw ticks.
6. Add basic controls: open, play, pause, stop, seek, volume, track visibility. Seek sends all-notes-off for transport-owned events and rebuilds scheduler state.

## Playback semantics

`Pause` stops logical advancement and future scheduling. It may allow previously rendered/released audio to decay. `Stop` returns playhead to zero and sends note-offs/pedal-off; it likewise permits normal voice tails. `Silence` is an explicit panic action that requests immediate voice reset where renderer supports it. UI labels must distinguish them.

## Done when

Reference MIDI files with tempo changes align playhead, visible notes, and internal playback within one audio buffer plus reported output latency. Malformed files fail safely with actionable text.

