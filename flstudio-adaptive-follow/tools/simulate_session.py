"""
Adaptive Follow session simulator (no FL Studio needed).

Plays a synthetic performer against a reference through the real follow
session and a virtual FL Studio transport, then prints state transitions,
transport decisions and the phase error between FL's playhead and the
performer.

    python tools/simulate_session.py                       # all scenarios, synthetic reference
    python tools/simulate_session.py --scenario pause -v   # one scenario, with transitions/decisions
    python tools/simulate_session.py --reference path/to/exported.json --strategy hybrid
    python tools/simulate_session.py --dry-run             # decisions only, transport untouched
"""

import argparse
import os
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "shared"))

from adaptive_follow import compat  # noqa: E402
from adaptive_follow.diagnostics import Diagnostics  # noqa: E402
from adaptive_follow.model import PROFILES, Config  # noqa: E402
from adaptive_follow.reference import Reference  # noqa: E402
from adaptive_follow.session import AdaptiveFollowSession  # noqa: E402
from adaptive_follow.simulation import (LiveNote, VirtualTransport, perform, performer_truth, rms,  # noqa: E402
                                        run_session, synthetic_reference)
from adaptive_follow.storage import Storage  # noqa: E402


def scenario_notes(name, ref, bpm, seed):
    total = ref.events[-1].tick * 60.0 / (bpm * ref.ppq)
    if name == "exact":
        return perform(ref, bpm)
    if name == "slow":
        return perform(ref, bpm, rate_at=lambda n: 0.8, jitter=0.015, seed=seed)
    if name == "fast":
        return perform(ref, bpm, rate_at=lambda n: 1.25, jitter=0.015, seed=seed)
    if name == "accel":
        return perform(ref, bpm, rate_at=lambda n: 0.9 + 0.35 * min(1.0, n / total), jitter=0.01, seed=seed)
    if name == "rubato":
        import math
        return perform(ref, bpm, rate_at=lambda n: 1.0 + 0.2 * math.sin(n / 2.0), jitter=0.01, seed=seed)
    if name == "chords":
        return perform(ref, bpm, chord_spread=0.08, jitter=0.02, seed=seed)
    if name == "pause":
        return perform(ref, bpm, pause_after_event=max(1, ref.event_count // 2), pause_s=4.0, seed=seed)
    if name == "mistakes":
        notes = perform(ref, bpm, jitter=0.02, seed=seed)
        out = []
        for k, n in enumerate(notes):
            if k % 17 == 5:
                continue                                                   # missing
            if k % 19 == 7:
                out.append(LiveNote(n.pitch + 1, n.t, -1))                 # wrong
                continue
            out.append(n)
            if k % 23 == 11:
                out.append(LiveNote(min(127, n.pitch + 7), n.t + 0.06, -1))  # extra
        return out
    if name == "lost":
        clean = perform(ref, bpm, seed=seed)
        cut = max(4, ref.event_count // 3)
        back = min(ref.event_count - 4, 2 * ref.event_count // 3)
        notes = [n for n in clean if n.true_event <= cut]
        t = notes[-1].t
        for k in range(10):
            t += 0.25
            notes.append(LiveNote(21 + k % 4, t, -1))
        first_back = [n.t for n in clean if n.true_event == back][0]
        shift = t + 0.5 - first_back
        notes += [LiveNote(n.pitch, n.t + shift, n.true_event) for n in clean if n.true_event >= back]
        return notes
    raise SystemExit("unknown scenario %r" % name)


SCENARIOS = ["exact", "slow", "fast", "accel", "rubato", "chords", "pause", "mistakes", "lost"]


def run(name, ref, args):
    data = tempfile.mkdtemp(prefix="af_sim_")
    storage = Storage(data)
    storage.save_reference(ref)
    values = dict(PROFILES[args.profile])
    values.update({"dry_run": args.dry_run, "follow_strategy": args.strategy,
                   "tempo_control": args.strategy != "speed_only", "allow_seek": args.allow_seek,
                   "log_level": args.log_level})
    if args.wait_behavior:
        values["wait_behavior"] = args.wait_behavior
    cfg = Config.from_dict(values)
    tr = VirtualTransport(ppq=ref.ppq, tempo=args.bpm)
    diag = Diagnostics(storage.log_path, args.log_level, echo=print if args.log_level == "debug" else None)
    s = AdaptiveFollowSession(tr, storage, diag, cfg, clock=lambda: tr.now)
    s.load_reference()
    notes = scenario_notes(name, ref, args.bpm, args.seed)
    truth = performer_truth(ref, notes)
    trace = run_session(s, tr, notes, notes[-1].t + 1.0, truth=truth)
    s.shutdown()
    spt_ms = 60000.0 / (args.bpm * ref.ppq)
    errs = []
    for t, state, playhead, true_tick, speed, tempo, playing in trace.rows:
        if true_tick is not None and playing and t > notes[0].t + 2.0 and state == "TRACKING":
            errs.append((playhead - true_tick) * spt_ms)
    matched = sum(1 for n, r in trace.notes if n.true_event >= 0 and r.event_index == n.true_event)
    scored = sum(1 for n, r in trace.notes if n.true_event >= 0)
    states = [x.new for x in trace.transitions]
    row = {
        "scenario": name,
        "notes": len(notes),
        "match_%": 100.0 * matched / max(1, scored),
        "rms_ms": rms(errs),
        "max_ms": max([abs(e) for e in errs] or [0.0]),
        "speed_calls": tr.count("set_speed"),
        "tempo_calls": tr.count("set_tempo"),
        "play_pause": tr.count("play_pause"),
        "seeks": tr.count("set_song_tick"),
        "states": " > ".join(states),
        "final": "speed %.2f tempo %.1f" % (tr.speed, tr.tempo()),
    }
    if args.verbose:
        print("\n=== %s ===" % name)
        for x in trace.transitions:
            print("  %7.3fs  %-10s -> %-10s %s" % (x.time, x.old, x.new, x.reason))
        print("  transport calls:")
        for t, call, arg in tr.calls:
            if call != "set_speed":
                print("  %7.3fs  %s(%s)" % (t, call, arg))
        sp = [c for c in tr.calls if c[1] == "set_speed"]
        if sp:
            print("  set_speed: %d calls, min %.3f, max %.3f" % (len(sp), min(c[2] for c in sp), max(c[2] for c in sp)))
        print("  log: %s" % storage.log_path)
    else:
        shutil.rmtree(data, ignore_errors=True)
    return row


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--scenario", choices=SCENARIOS + ["all"], default="all")
    ap.add_argument("--reference", help="exported reference JSON (default: synthetic part)")
    ap.add_argument("--strategy", choices=["speed_only", "tempo_only", "hybrid"], default="speed_only")
    ap.add_argument("--bpm", type=float, default=120.0, help="project tempo of the virtual FL Studio")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--allow-seek", action="store_true")
    ap.add_argument("--profile", choices=sorted(PROFILES), default="stable")
    ap.add_argument("--wait-behavior", choices=["pause", "slow", "continue"], default=None,
                    help="override the profile's wait_behavior")
    ap.add_argument("--seed", type=int, default=3)
    ap.add_argument("--log-level", choices=["off", "error", "info", "debug"], default="info")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    if args.reference:
        ref = Reference.from_json(compat.read_text(args.reference))
    else:
        ref = synthetic_reference(64, ppq=96, seed=11)
    print("reference %r: %d notes, %d events, PPQ %d; profile %s, strategy %s, wait %s%s%s" % (
        ref.name, len(ref.notes), ref.event_count, ref.ppq, args.profile, args.strategy,
        args.wait_behavior or PROFILES[args.profile]["wait_behavior"],
        ", seek allowed" if args.allow_seek else "", ", DRY-RUN" if args.dry_run else ""))
    rows = [run(name, ref, args) for name in (SCENARIOS if args.scenario == "all" else [args.scenario])]
    print("\n%-9s %5s %7s %7s %7s %6s %6s %5s %5s  %s" % ("scenario", "notes", "match%", "rms_ms", "max_ms",
                                                        "speed#", "tempo#", "p/p", "seek", "final"))
    for r in rows:
        err = ("%7s %7s" % ("n/a", "n/a")) if args.dry_run else ("%7.1f %7.1f" % (r["rms_ms"], r["max_ms"]))
        print("%-9s %5d %7.1f %s %6d %6d %5d %5d  %s" % (
            r["scenario"], r["notes"], r["match_%"], err, r["speed_calls"],
            r["tempo_calls"], r["play_pause"], r["seeks"], r["final"]))
    if args.dry_run:
        print("(dry-run: FL Studio's transport is never touched; decisions are only logged as '[dry-run] would ...')")
    print()
    for r in rows:
        print("%-9s states: %s" % (r["scenario"], r["states"]))


if __name__ == "__main__":
    main()
