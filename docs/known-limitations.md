# Known limitations

Severity: **high** blocks practice, **medium** degrades it, **low** is cosmetic or rare.

| # | Limitation | Severity | Workaround |
| --- | --- | --- | --- |
| 1 | Hand roles are inferred: a single piano part is split at C4, two unnamed piano parts by average pitch. Crossed hands in such files land in the wrong hand. | medium | Parts guessed from pitch are marked "?"; change them in the part list (remembered per song). |
| 2 | VST3 instruments run inside the app. Scanning is done in a separate process, so a plug-in that crashes while being scanned is skipped, but one that crashes while playing or in its window closes PianoAssist. | medium | Song settings are saved 1.5 s after each change, so little is lost. Choose another sound for that track; send the log. |
| 3 | Tempo change is MIDI tempo only (25–200%); there is no audio time-stretch, and no WAV/MP3 backing tracks. | low | — |
| 4 | Only VST3 plug-ins (no VST2, AU, CLAP). The main VST3 instrument does not get the drums (channel 10). | low | Give the drum track its own sound (a General MIDI SoundFont) in the track list. |
| 5 | Sound delay is measured by the player (click test), not automatically: drivers do not report what virtual devices (SteelSeries Sonar) and wireless headphones add. The app warns when presses are steadily late. ASIO is not built in. | medium | Settings → MIDI and audio → Measure delay; choose a real output and "Windows Audio (Low Latency Mode)". |
| 6 | Installer and exe are not code-signed, so SmartScreen and "unknown publisher" warnings are expected. | low | See `docs/windows-warnings.md` (check SHA-256). |
| 7 | One MIDI input at a time; no MIDI output to the piano's own sounds. | low | Use the app's sound through speakers or headphones. |
| 8 | No sounds are bundled (licences). The built-in synth is the default. | medium | Load a free SoundFont or VST3 from `docs/sounds.md`. |
| 9 | The interface language changes after a restart. Some technical strings (song summary line, part details, device errors from the system) stay in English. | low | — |
| 10 | Count-in is one bar of high and low notes played by the current main sound; there is no separate click sound or metronome during play. | low | — |
| 11 | A seek or loop jump does not replay notes that started before the new position; sustain pedal is restored, held notes are not. | low | Start the range a little earlier. |
| 12 | The update check only tells you about a new version and opens its GitHub page; it never downloads or installs. | low | Download and run the new setup, or replace the portable exe. |
| 13 | Hardware behaviour on the Casio CDP-S110 (latency, pedal, reconnect) is verified only by the owner's manual checks, not by CI. | — | `docs/manual-checks/casio-checklist.md`. |

Diagnostics stay on the computer: the log folder and "Save diagnostics" are the only outputs, and the user's folder name is replaced in reports. The only network access is the update check, when the user asks for it.
