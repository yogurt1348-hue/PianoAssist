# Piano Practice MVP test plan

## Test levels

| Level | Focus | Required examples |
| --- | --- | --- |
| Unit | Pure parsing, time conversion, gate matching, state reducer | All boundary cases listed below |
| Integration | Parser -> timeline -> engine -> scheduled events | Fixture MIDI and simulated input streams |
| Audio | Renderer/scheduler timing and silence semantics | Buffer-size and sample-rate matrix |
| Manual hardware | USB connection and real play feel | CDP-S110 plus unavailable/reconnect scenario |
| Release smoke | Installer and first-run experience | Clean Windows profile |

## Required MIDI fixture corpus

1. Format 0 monophonic scale, fixed tempo.
2. Format 1 two-hand piano plus separate drums/bass.
3. Multiple tempo changes and time signatures.
4. Dense chords, arpeggios, repeated same pitch before note-off, and zero-velocity note-offs.
5. Sustain-pedal-heavy piano track.
6. Intro/outro with backing but no piano requirement.
7. Tracks with misleading names, percussion on channel 10, and all notes on one channel.
8. Non-ASCII filename and malformed/truncated files.
9. Long file (at least 30 minutes) to expose drift and memory growth.
10. Crossed hands / intentionally wrong automatic hand split for manual mapping.

Fixtures must be legally redistributable, tiny where possible, named by purpose, and accompanied by expected normalized-event assertions.

## Gate matrix

For every practice mode test: correct note; wrong note then correct note; early boundary; exactly on lower/upper boundary; just outside each boundary; chord in all permutations; duplicate required pitch; rapid repeated note; mode change during wait; stop/pause during wait; device disconnect during wait. Assert logical decisions, visible state, and absence of stuck notes.

## Timing and audio matrix

Run 44.1/48 kHz at 128, 256, 512, and 1024 frame buffers where device allows. Assert no crash, transport-state determinism, and that expected audio start is within buffer plus reported output latency. Verify a gate hold stops new scheduling but permits release tail; verify explicit `Silence` clears sound promptly.

## Regression criteria

Every fixed issue receives a smallest reproducing automated test when feasible. CI runs unit/integration tests. A release candidate requires no critical defects, no known stuck-note issue, and completion of manual scenarios.

