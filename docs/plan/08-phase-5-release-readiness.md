# Phase 5 Release readiness and future seams

## Objective

Make the single-user MVP diagnosable and safe to distribute privately, while preserving a path to public release.

## Work items

1. Build signed-installer-ready Windows package configuration; actual code-signing credentials stay outside repository.
2. Add first-run device help, missing-device recovery, MIDI file error messages, and `Copy diagnostics` with privacy-conscious local data only.
3. Verify keyboard-only navigation, readable scaling at 125% and 200%, high contrast, and clear status announcements.
4. Add crash-safe settings writes and no loss of original MIDI files. Never modify opened source files.
5. Collect only opt-in local diagnostic bundle; no telemetry/network dependency in MVP.

## Deferred feature contracts

VST3: add scanner/process isolation and plug-in latency compensation inside audio module. A plug-in may have lookahead and long tails; gating remains timeline-driven. WAV/time-stretch: add an audio-clip clock and explicit stretch quality/latency policy; do not treat it as MIDI scheduling.

## Release checklist

Run the acceptance suite on a clean Windows 10/11 system with the target keyboard and without it. Verify install/uninstall, no device, unsupported MIDI, device disconnect, and application restart.

