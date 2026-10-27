# Phase 1 USB MIDI input

## Objective

Make the Casio CDP-S110 usable as a generic class-compliant USB MIDI input while providing enough diagnostics to troubleshoot Windows device setup.

## Work items

1. Enumerate inputs and persist a stable device identifier plus last seen display name. Never assume `Casio` appears in the name.
2. Support live `noteOn`, `noteOff`, and CC64 sustain. Log other events at a throttled diagnostic level.
3. Timestamp at callback receipt using a monotonic clock; attach an incrementing sequence number so equal timestamps are deterministically ordered.
4. Implement an input monitor screen: device, connection status, most recent pitch/velocity, sustain state, and event-rate indicator.
5. Handle disconnect/reconnect: release matcher-held input notes, show a recoverable banner, and never crash or block audio.

## Device behaviour contract

The app must work with any compliant MIDI piano that emits conventional note events. CDP-S110 is the validation device, not a parser special case. MIDI channel is configurable/auto-accepted initially; pitch uses MIDI 0-127. Velocity zero note-on is normalized to note-off.

## Tests

- Feed recorded event streams with normal notes, zero-velocity offs, missing offs, sustain pedal, duplicate events, and out-of-order timestamps.
- Simulate device removal during a held chord and assert no stuck gate state.
- Manual: reconnect the physical keyboard without restarting the app.

## Done when

The monitor accurately shows key presses from the target keyboard, and loss/reconnection recovers without application restart.

