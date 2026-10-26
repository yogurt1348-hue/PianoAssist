# Phase 4 Practice modes and accompaniment

## Objective

Deliver musically useful one- and two-hand practice without making progression depend on unreliable track assumptions.

## Modes

| Mode | Required notes | Non-practised piano role | Backing tracks |
| --- | --- | --- | --- |
| Both hands | Left + right | N/A | Play by default |
| Right hand | Right | Optional play or mute | Play by default |
| Left hand | Left | Optional play or mute | Play by default |

The option is named `Play other piano part`. It defaults on for a learner who wants context and remains independent from backing-track muting. Persist per-user settings locally.

## Auto accompaniment

For MVP, accompaniment means playback of source-MIDI tracks/roles marked `backing`; it is not algorithmic chord generation. At gate waits, accompaniment is held: no future notes are scheduled, currently sounding voices receive normal release treatment, and tail audio may remain. On automatic no-piano spans, accompaniment proceeds continuously through the span.

## Automatic non-piano segments

The engine finds spans with no requirement for selected practice mode. They transition through `Playing` normally and may pass several source events/tempo changes. To avoid a perceived bug, UI shows `Listening` or `Accompaniment only` rather than `Waiting`. A configurable maximum auto-run duration is telemetry only in MVP; do not introduce arbitrary stops.

## Manual mapping and safety

If role inference confidence is low, expose a simple per-track mapping panel before practice begins. Do not silently classify percussion as a required hand. A track with polyphonic piano accompaniment can be marked backing by the user.

## Done when

All three modes function on fixtures with split hands, crossed hands, backing-only intro, and no backing. Switching mode resets transport/gate state predictably.

