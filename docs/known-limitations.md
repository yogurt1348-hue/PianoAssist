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

Diagnostics stay on the computer: the log folder, "Diagnostics... -> Copy" and "Save" are the only outputs, and the user's folder name is replaced in reports. There is no telemetry or network access.
