# Known limitations (MVP)

Each item has a severity and a workaround. Severity: **high** blocks practice, **medium** degrades it, **low** is cosmetic or rare.

| # | Limitation | Severity | Workaround |
| --- | --- | --- | --- |
| 1 | Hand roles are inferred; a single piano part is split at C4 and two unnamed piano parts by average pitch. Crossed hands in such files land in the wrong hand. | medium | Parts guessed from pitch are marked "?"; change them in the part list. |
| 2 | Part changes are not remembered between sessions. | low | Change them again after opening the song. |
| 3 | The built-in synth is a simple additive tone; drums (channel 10) are silent. | low | Load a VST3 instrument or a SoundFont (Sound menu). |
| 4 | No automatic input-latency calibration; the timing tolerance presets absorb typical USB MIDI latency. | low | Use "Easy timing" if presses near the beat are judged early or late. |
| 5 | Windows high-contrast themes do not recolour the app (it draws its own dark theme with high-contrast text). | low | Use Windows magnifier / 200% scaling; the layout adapts. |
| 6 | The installer and exe are not code-signed, so SmartScreen may warn on first run. | low | "More info" -> "Run anyway". Signing is prepared in `installer/PianoAssist.iss` without credentials. |
| 7 | One MIDI input device at a time; no MIDI output to the piano's own sounds. | low | Use the app's sound through speakers or headphones. |
| 8 | Tempo cannot be slowed down yet. | medium | Strict waiting at each note gives a self-paced tempo. |
| 9 | Tracks muted on the "Other tracks" tab are not remembered between sessions or songs. | low | Untick them again after opening the song. |
| 10 | No sounds are bundled (licences). The built-in synth is the default. | medium | Load a free SoundFont or VST3 from `docs/sounds.md`. |
| 11 | VST3 only: no VST2, AU or CLAP plug-ins. A plug-in that crashes during a scan is skipped next time, but one that crashes while playing closes the app. | medium | Remove the plug-in from the list by choosing another sound; report it with the log. |

Diagnostics stay on the computer: the log folder, "Diagnostics... -> Copy" and "Save" are the only outputs, and the user's folder name is replaced in reports. There is no telemetry or network access.
