# Phase 3 Strict note gated transport

## Objective

Turn the timeline into deterministic practice sections that wait for required learner notes and advance only after a valid match.

## Gate construction

A gate is formed at each onset cluster containing one or more notes assigned to the practised hand(s). Its requirement is a multiset of pitches and a time window around its logical onset. Notes sharing an onset within a configurable grouping epsilon are one chord gate. Sustain length is not scored in MVP; only press/onset is required.

For `both` mode, both roles contribute to a gate. `right` and `left` require only that role. If a cluster has no required notes for the selected mode, it is not a gate and transport traverses it automatically.

## Matching policy

- Match `noteOn` only; note-off has no pass/fail role in MVP.
- Exact MIDI pitch match; velocity is accepted when 1-127.
- Notes may arrive in any chord order.
- An early event is retained only inside the early tolerance; stale unmatched events expire before a gate begins.
- Wrong pitches do not satisfy requirements and do not reset already accepted pitches.
- A required pitch can match once per required multiplicity.
- Gate passes when all requirements are matched within the gate policy. Emit detailed result: accepted, missing, wrong, late, early, or expired.

## Transport algorithm

1. While playing, schedule/render until the next gate boundary.
2. At boundary, transition atomically to `WaitingForGate`; cancel events after boundary.
3. Release transport-owned active notes and pedal, permitting instrument release tails.
4. Consume timestamped input events according to matching policy.
5. On pass, resume from boundary with a small configurable lead-in only if enabled; never replay backing unexpectedly by default.
6. On retry, retain no stale input and show required notes.

## Tests

Build a table-driven matcher suite: single note early/on-time/late, chord permutations, wrong interleaving, duplicate pitches, rapid repeated same pitch, sustain pedal present, mode switches, tempo change near gate, and empty segments. Replay tests must use a fake monotonic clock.

## Done when

The same input event stream always yields identical gate decisions and transport transitions, independent of UI frame rate or audio callback block size.

