# Phase 3 manual check: note-gated practice

Run on Windows with the CDP-S110 connected and speakers or headphones on. Use a piano song with both hands (the one from phase 2 is fine). Attach `%APPDATA%\PianoAssist\logs\pianoassist.log`.

| # | Step | Expected |
| --- | --- | --- |
| 1 | Open the song, tick "Practice: wait for my notes", keep "Both hands". | The bar shows "Next: …" with the first notes; those keys are yellow on the bottom keyboard. |
| 2 | Press Play and do not touch the piano. | The app plays only backing (if any) and stops exactly at your first note. "Waiting for: C4 …" appears in yellow. |
| 3 | Press a wrong key. | It flashes red; playback stays held. |
| 4 | Play the shown note or chord (any order, any velocity). | Playback continues immediately from that point; the next notes turn yellow. |
| 5 | Play a few notes on time, in rhythm with the playhead. | Playback does not stop at all. |
| 6 | Play a note clearly too early (well before the playhead reaches it). | It does not count; playback stops at that note and waits. |
| 7 | Choose "Right hand". | Playback stops. Press Play: the app plays the left hand and backing, and waits only for right-hand notes. |
| 8 | Choose "Left hand". | Same, the other way round. |
| 9 | Click on the roll to jump somewhere, then Play. | Waiting starts from the first of your notes after that point. |
| 10 | Press Stop, then Play. | It starts over from the first note. |
| 11 | Untick Practice and Play. | Normal playback of every part, as in phase 2, with no stops. |
| 12 | Pause, Silence (Esc) and Space while held at a note. | Pause/Play toggles; Silence cuts sound; nothing hangs. |

Log lines to look for: `Practice prepared`, `Waiting at gate`, `Press judged` (verdict and offset in ms), `Gate passed` (with wait time), `Song finished in practice`.

Known limits: no scoring or statistics yet; no "loop a bar"; tempo is the file's tempo; the app does not yet play your part for you as a demo.
