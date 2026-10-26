# Phase 1 manual check: USB MIDI input

Run on Windows with the CDP-S110 (or any class-compliant MIDI piano). Record the result and attach `%APPDATA%\PianoAssist\logs\pianoassist.log`.

| # | Step | Expected |
| --- | --- | --- |
| 1 | Turn the piano on, connect USB to its USB TO HOST port, then start PianoAssist. | The input box shows the piano (often "CASIO USB-MIDI"); status is green "Connected". Log has `MIDI input opened`. |
| 2 | Press single keys across the keyboard. | "Last note" shows the right name (middle C = C4) and a velocity that follows how hard you play. |
| 3 | Hold a three-note chord. | "Held keys" lists all three; they disappear when released. |
| 4 | Press and release the sustain pedal. | "Sustain pedal" switches Down/Up. |
| 5 | Pull the USB cable while holding a chord. | Within about a second an orange banner says the piano is disconnected; held keys clear. Log has `MIDI input disappeared` and `MIDI input closed` with the released notes. |
| 6 | Plug the cable back in. | The piano is picked up again without restarting; keys work. Log has `MIDI input appeared` and `MIDI input opened`. |
| 7 | Close and reopen the app. | The same piano is selected automatically. |
| 8 | Start the app with the piano off, then turn it on. | Status first says Windows reports no MIDI inputs; the piano is picked up when it appears. |
| 9 | Press "Open log folder". | Explorer opens the logs folder. |

If step 1 shows no inputs: check Device Manager → "Sound, video and game controllers" for a USB-MIDI device, close other music programs (they can hold the port exclusively), and send the log.
