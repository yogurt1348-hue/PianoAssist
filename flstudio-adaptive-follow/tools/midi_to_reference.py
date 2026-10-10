"""
Convert a MIDI file into an Adaptive Follow reference (outside FL Studio).

FL Studio runs the Piano Roll script and the MIDI device script in separate,
sandboxed Python interpreters; the device script can only use files in its own
folder (...\\Settings\\Hardware\\AdaptiveFollow\\data). This converter runs with
normal Python and writes the reference straight into that folder.

In FL Studio: Piano Roll of your part > (menu) File > Export as MIDI file, or
File > Export > MIDI file for the whole project. Then:

    python tools/midi_to_reference.py part.mid                     # one note track: converted directly
    python tools/midi_to_reference.py song.mid --list              # show tracks
    python tools/midi_to_reference.py song.mid --track 3 --name "Verse"
    python tools/midi_to_reference.py song.mid --channel 1 --chord-window 6 --min-velocity 10

or drag the .mid file onto tools\\midi_to_reference.bat. Afterwards in FL Studio's
Script output: af_reload().
"""

import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "shared"))

from adaptive_follow import __version__  # noqa: E402
from adaptive_follow.midifile import MidiFormatError, read_midi_file  # noqa: E402
from adaptive_follow.reference import FILTER_ALL, build_reference_from_notes  # noqa: E402
from adaptive_follow.storage import Storage  # noqa: E402


def find_fl_settings():
    docs = []
    try:
        import ctypes.wintypes
        buf = ctypes.create_unicode_buffer(ctypes.wintypes.MAX_PATH)
        ctypes.windll.shell32.SHGetFolderPathW(None, 5, None, 0, buf)   # CSIDL_PERSONAL = Documents
        if buf.value:
            docs.append(buf.value)
    except Exception:
        pass
    home = os.path.expanduser("~")
    docs.append(os.path.join(home, "Documents"))
    if os.environ.get("OneDrive"):
        docs.append(os.path.join(os.environ["OneDrive"], "Documents"))
    for d in docs:
        p = os.path.join(d, "Image-Line", "FL Studio", "Settings")
        if os.path.isdir(p):
            return p
    return ""


def device_data_dir(fl_settings):
    return os.path.join(fl_settings, "Hardware", "AdaptiveFollow", "data")


def choose_track(midi, args):
    tracks = midi.note_tracks
    if not tracks:
        raise SystemExit("the MIDI file has no notes")
    if args.track:
        for t in midi.tracks:
            if t.index + 1 == args.track:
                if not t.notes:
                    raise SystemExit("track %d has no notes" % args.track)
                return [t]
        raise SystemExit("no track %d (use --list)" % args.track)
    if args.all_tracks or len(tracks) == 1:
        return tracks
    print("The MIDI file has several tracks with notes:")
    for t in tracks:
        print("  " + t.describe())
    if args.non_interactive or not sys.stdin.isatty():
        raise SystemExit("choose one with --track N (or --all-tracks)")
    answer = input("Track number to use (Enter = %d): " % (tracks[0].index + 1)).strip()
    wanted = int(answer) if answer else tracks[0].index + 1
    for t in tracks:
        if t.index + 1 == wanted:
            return [t]
    raise SystemExit("no track %d with notes" % wanted)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("midi", help=".mid file exported from FL Studio (or any MIDI file)")
    ap.add_argument("--list", action="store_true", help="only list the tracks")
    ap.add_argument("--track", type=int, default=0, help="track number (1-based, see --list)")
    ap.add_argument("--all-tracks", action="store_true", help="merge all tracks with notes")
    ap.add_argument("--channel", type=int, default=0, help="only this MIDI channel 1-16 (default: all)")
    ap.add_argument("--name", default="", help="reference name (default: MIDI file name)")
    ap.add_argument("--chord-window", type=int, default=None,
                    help="ticks: notes starting this close form one chord (default PPQ/16)")
    ap.add_argument("--min-velocity", type=int, default=0, help="ignore softer notes, 0-127")
    ap.add_argument("--fl-settings", default="", help="FL Studio ...\\Image-Line\\FL Studio\\Settings folder")
    ap.add_argument("--out-dir", default="", help="data folder to write to (default: the device script's data folder)")
    ap.add_argument("--non-interactive", action="store_true")
    args = ap.parse_args()

    try:
        midi = read_midi_file(args.midi)
    except (IOError, OSError) as exc:
        raise SystemExit("cannot read %s: %s" % (args.midi, exc))
    except MidiFormatError as exc:
        raise SystemExit("%s: %s" % (args.midi, exc))
    print("%s: format %d, PPQ %d, %d track(s)" % (os.path.basename(args.midi), midi.format, midi.ppq, len(midi.tracks)))
    if args.list:
        for t in midi.tracks:
            print("  " + t.describe())
        return 0

    tracks = choose_track(midi, args)
    notes = [n for t in tracks for n in t.notes if not args.channel or n.channel == args.channel]
    name = args.name or os.path.splitext(os.path.basename(args.midi))[0]
    meta = {"exporter": "Adaptive Follow midi_to_reference.py %s" % __version__, "source": "MIDI file",
            "midi_file": os.path.basename(args.midi),
            "tracks": [t.index + 1 for t in tracks], "track_names": [t.name for t in tracks],
            "channel": args.channel or None}
    ref, report = build_reference_from_notes(notes, midi.ppq, name=name, filter_mode=FILTER_ALL,
                                             min_velocity=args.min_velocity / 127.0,
                                             chord_window_ticks=args.chord_window, meta=meta)
    if ref is None:
        raise SystemExit("nothing converted: " + "; ".join(report["errors"] + report["warnings"]))

    if args.out_dir:
        data = args.out_dir
    else:
        fl = args.fl_settings or find_fl_settings()
        if not fl:
            raise SystemExit("FL Studio Settings folder not found; pass --fl-settings "
                             "\"<...>\\Image-Line\\FL Studio\\Settings\" (or --out-dir)")
        if not os.path.isdir(os.path.join(fl, "Hardware", "AdaptiveFollow")):
            raise SystemExit("the AdaptiveFollow device script is not installed in %s (run install_windows.ps1)" % fl)
        data = device_data_dir(fl)
    path = Storage(data).save_reference(ref)
    print("Converted %d notes (%d events) from %s." % (report["exported"], ref.event_count,
                                                        ", ".join(t.describe() for t in tracks)))
    for w in report["warnings"]:
        print("  warning: " + w)
    print("Reference '%s' written to %s" % (ref.name, path))
    print("In FL Studio: VIEW > Script output > AdaptiveFollow tab, type: af_reload()")
    return 0


if __name__ == "__main__":
    sys.exit(main())
