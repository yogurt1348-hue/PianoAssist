# Sounds: built-in synth, SoundFonts and VST3 instruments

The "Sound" box next to the time chooses what the song sounds like. The choice, and a VST3 instrument's own settings, are remembered.

| Choice | What it is | Drums | Notes |
| --- | --- | --- | --- |
| Built-in synth | Simple piano-like tone, always available | no | No setup. |
| SoundFont (.sf2) | A General MIDI bank: piano, strings, bass, drums... each part gets its instrument from the MIDI file | yes | "Load SoundFont (.sf2)...". Large banks take a few seconds to load and use memory (about twice the file size). |
| VST3 instrument | Any VST3 instrument installed on the PC, with its own window for sounds and presets ("Instrument..." button) | no (channel 10 is not sent) | "Find VST3 instruments" scans `C:\Program Files\Common Files\VST3`; "Add VST3 instrument file..." adds one from another folder. |

A plug-in that crashes during scanning is skipped next time and named in the log. Plug-ins run inside the app; a faulty plug-in can still crash it while playing (process isolation is a post-MVP item).

## Free sounds to try

Nothing third-party is bundled: licences and download sizes differ, and the owner decides what to ship. These are free at the time of writing; check each licence on its site.

- **SoundFonts (.sf2), full band with drums:** GeneralUser GS (S. Christian Collins), FluidR3_GM (MIT licence), MuseScore_General.
- **SoundFont piano only:** Salamander Grand Piano (Alexander Holm, CC-BY), available as .sf2 conversions.
- **VST3, many instruments (closest to FL Studio FLEX):** Spitfire LABS (free, needs the Spitfire app), Native Instruments Kontakt Player with Komplete Start (free).
- **VST3 piano:** Keyzone Classic (Bitsonic), Pianoteq (trial; full version is paid), sfizz (open source SFZ player) with the Salamander Grand Piano SFZ.

FLEX itself is part of FL Studio and is not available as a separate VST3 instrument.
