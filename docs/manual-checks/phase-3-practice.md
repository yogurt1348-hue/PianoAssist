# Phase 3 manual check: note-gated practice

Run on Windows with the CDP-S110 connected and speakers or headphones on. Use a piano song with both hands (the one from phase 2 is fine). Attach `%APPDATA%\PianoAssist\logs\pianoassist.log`.

| # | Step | Expected |
| --- | --- | --- |
| 1 | Open the song, tick "Practice: wait for my notes", keep "Both hands". | The board shows "PLAY NEXT" with the first notes; those keys are yellow on the bottom keyboard. |
| 2 | Press Play and do not touch the piano. | The app plays only backing (if any) and stops just after your first note (end of the timing window). The board turns yellow: "WAITING FOR YOU". |
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

## 3.1 additions (test-build after owner feedback)

| # | Step | Expected |
| --- | --- | --- |
| 13 | Practice on, "Normal timing". Play notes a little early or late (about a quarter second). | They count; the song keeps going without stopping. |
| 14 | Switch to "Strict timing" and repeat. | Slightly early notes no longer count; the song stops shortly after the note. Restart the app: the choice is remembered. |
| 15 | Switch to "Easy timing". | Even clearly early/late notes count. |
| 16 | Look at the board above the keyboard. | "PLAY NEXT" with the notes in big chips (name and до/ре/ми), left hand orange, right hand blue, and the next steps after "then". It turns yellow with "WAITING FOR YOU" when the song stops. |
| 17 | Look at the keyboard. | Every white key shows its letter and до ре ми; C keys show the octave (C4). |
| 18 | Click and hold a key with the mouse, drag across keys, release. | Each key sounds while held and lights blue; no note keeps sounding after release. |

Known limits: no scoring or statistics yet; no "loop a bar"; tempo is the file's tempo; the app does not yet play your part for you as a demo.
