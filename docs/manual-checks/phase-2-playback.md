# Phase 2 manual check: MIDI files, piano roll, playback

Run on Windows with speakers or headphones. Use any piano MIDI file you have (format 0 or 1), ideally one with both hands and one with a band. Attach `%APPDATA%\PianoAssist\logs\pianoassist.log`.

| # | Step | Expected |
| --- | --- | --- |
| 1 | Press "Open MIDI file..." and choose a song. | Notes appear on the roll; title and summary (format, tracks, notes, length, BPM, metre) show above it. Log has `MIDI file loaded` and one `Part mapping` line per part. |
| 2 | Drag another .mid file from Explorer onto the window. | It replaces the first song. |
| 3 | Look at the part list on the left. | Right hand is blue, left hand orange, backing grey. If a part is wrong, note which (inference can be wrong; manual mapping comes later). |
| 4 | Untick a part. | Its notes disappear from the roll (it still sounds). |
| 5 | Scroll with the mouse wheel, zoom with Ctrl+wheel. | The roll moves and zooms around the mouse; zoom is remembered after restart. |
| 6 | Press Play (or Space). | Sound plays; the red playhead moves in time with the music and the view follows it. |
| 7 | Press Pause (or Space) while a chord sounds. | Playhead stops; notes fade out over a fraction of a second; the note says "sound may ring out". |
| 8 | Play again, then press Silence (or Esc). | Sound stops immediately. |
| 9 | Click somewhere on the roll, then Play. | Playback continues from the clicked point. |
| 10 | Let a song play to the end; press Play. | "End of song", then it starts over from the beginning. |
| 11 | Press Stop during playback. | Playhead returns to 0:00; no notes keep sounding. |
| 12 | Move the volume slider. | Volume changes smoothly. |
| 13 | Open a file that is not MIDI (rename a .txt to .mid). | A clear error message; the app keeps working. |
| 14 | Keep the piano connected and press keys. | The keys light up on the bottom keyboard and in the roll's left gutter. |

Known limits: drums (channel 10) are not voiced by the built-in synth; the built-in sound is a simple piano-like tone, not a sampled piano.
