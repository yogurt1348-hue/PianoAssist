# Phase 4 manual check: practice modes, accompaniment, part mapping

Run on Windows with the CDP-S110 connected. Use a song with a band (drums/bass) and both piano hands, and one piano-only song. Attach `%APPDATA%\PianoAssist\logs\pianoassist.log`.

| # | Step | Expected |
| --- | --- | --- |
| 1 | Open a band song, Practice on, "Right hand", "Play other hand" on, "Play backing" on. Play. | The app plays the left hand and the band; it waits only for your right-hand notes. |
| 2 | Untick "Play other hand". | Playback stops where it was. Play again: the left hand is silent, the band still plays. |
| 3 | Tick it again and untick "Play backing". | Left hand plays, band silent. |
| 4 | Choose "Both hands". | "Play other hand" is greyed out (both hands are yours). |
| 5 | Untick Practice. | Everything plays except backing if it is still unticked. |
| 6 | Look at the part list. Each part has a box: Right hand / Left hand / Both hands (split at C4) / Other track (app plays it). | Parts guessed from pitch show "?" and an orange notice says to check them. |
| 7 | Change a wrong part (for example a piano accompaniment track) to "Other track". | The part moves to the "Other tracks" tab; the app now plays it and does not wait for it. |
| 8 | Change a piano part to "Left hand" while practising "Left hand". | The board shows its notes; playback waits for them. |
| 9 | A song with an intro without piano: Practice on, Play. | The intro plays without stopping; the board says "Accompaniment only"; the first stop is at your first note. |
| 10 | Restart the app. | Mode, "Play other hand", "Play backing" and timing are as you left them. Part changes are not remembered yet. |

Log lines: `Accompaniment changed`, `User changed a part`, `Practice prepared` with the reason.
