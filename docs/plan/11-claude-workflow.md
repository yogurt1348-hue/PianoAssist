# Instructions for Claude iterative development

## Working agreement

Implement one small vertical slice at a time in phase order. Before editing, restate the slice's observable outcome and identify touched modules. After editing, build and run the smallest relevant automated tests. Do not begin a later phase merely because an interface exists; prove the earlier phase exit criteria first.

## Per-iteration protocol

1. Read the relevant phase, architecture section, and acceptance items.
2. Inspect current repository state and existing tests; preserve unrelated user changes.
3. Propose a compact implementation plan with risks, especially real-time/threading implications.
4. Make the smallest coherent change. Keep business logic outside JUCE components and audio callbacks.
5. Add/adjust deterministic tests before declaring behaviour done.
6. Build and run targeted tests; then run broader tests when the changed boundary warrants it.
7. Report: files changed, user-visible behaviour, verification commands/results, limitations, and the next recommended slice.

## Code quality rules

- Treat MIDI and settings files as untrusted; validate bounds and error paths.
- Use monotonic time for runtime decisions. Do not use UI frame time as a musical clock.
- Avoid allocations, locks, file I/O, and logging on MIDI callback/audio callback paths.
- Make gate matching data-driven and table-testable.
- Use named configuration types for tolerances and calibration; no magic timing constants.
- Do not silently convert a paused logical timeline into muted audio. Preserve the pause/tail/silence semantics from architecture.
- Keep VST3, WAV, and time-stretch out of MVP implementation; create only the stated interface seams when needed.

## Escalate instead of guessing

Stop and ask when a product choice would change required musical behaviour, when MIDI source ambiguity cannot be safely inferred, when a JUCE/API version constraint blocks the design, or when a proposed dependency changes packaging/licensing. Otherwise make the documented reasonable decision and record it in `12-assumptions-open-questions.md`.

## Definition of done for a slice

Source is formatted; tests pass; error handling is intentional; logs do not expose unnecessary personal paths/content; and the implementation is linked to a phase exit criterion. A manual-only claim needs a reproducible checklist.

