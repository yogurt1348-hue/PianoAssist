"""
AdaptiveFollowSession: the follow loop, independent of FL Studio.

It talks to the host only through a *transport port* object (the FL Studio
device script implements one on top of the official API; the simulator and
the tests use a virtual one):

    port.is_playing() -> bool
    port.song_tick() -> int            absolute song/pattern ticks (project PPQ)
    port.project_ppq() -> int
    port.tempo() -> float              project tempo in BPM
    port.play_pause()                  start / pause / resume (FL: transport.start)
    port.set_speed(multiplier)         FL: transport.setPlaybackSpeed
    port.set_tempo(bpm)                FL: general.processRECEvent(REC_Tempo, ...)
    port.set_song_tick(ticks)          FL: transport.setSongPos(ticks, SONGLENGTH_ABSTICKS)
    port.can_seek() / port.can_set_tempo() -> bool
    port.hint(text)                    optional status text (FL: ui.setHintMsg)

Every port call is guarded: a failing call is logged and the corresponding
feature disabled for the session; it never propagates into FL Studio.

Call order from the host:  on_note()/on_cc() from MIDI callbacks (cheap, no
transport calls), tick() regularly (FL: OnIdle, ~20 ms) - all transport
changes happen in tick().
"""

from . import adaptive_transport as at
from . import compat
from . import handoff
from . import model
from . import score_follower as sf
from . import state as st
from .diagnostics import Diagnostics
from .reference import Reference, ReferenceFormatError
from .storage import ReferenceMissingError, Storage

VERSION = "0.1.2"


class AdaptiveFollowSession(object):
    def __init__(self, port, storage=None, diagnostics=None, config=None, clock=None):
        self.port = port
        self.storage = storage or Storage()
        self.config = config or model.Config.from_dict({})
        self.diag = diagnostics or Diagnostics(None, self.config.log_level, self.config.log_max_kb)
        self.clock = clock or compat.monotonic
        self.sm = st.StateMachine()
        self.sm.listeners.append(self._on_transition)
        self.reference = None
        self.reference_path = ""
        self.follower = None
        self.controller = None
        self.offset_song_ticks = 0
        self.project_ppq = 96
        self.base_bpm = 120.0
        self.last_note = None
        self.last_decision = None
        self.disabled_features = set()
        self._pending_start = False
        self._config_mtime = 0.0
        self._last_hint = ""
        self._handoff_serial = 0

    # =============================================================== loading
    def load_config(self):
        self.config = self.storage.load_config()
        self._config_mtime = self.storage.config_mtime()
        self.diag.set_level(self.config.log_level)
        self.diag.max_bytes = self.config.log_max_kb * 1024
        for w in self.config.warnings:
            self.diag.warning("config: " + w)
        return self.config

    def load_reference(self, name=None):
        """Load the configured (or given) reference. Returns True on success."""
        if self.sm.active:
            self.diag.warning("cannot load a reference while armed; disarm first")
            return False
        wanted = self.config.reference_name if name is None else name
        try:
            ref, path = self.storage.load_reference(wanted)
        except (ReferenceMissingError, ReferenceFormatError) as exc:
            self.reference = None
            if not wanted and self.poll_handoff(force=True):
                return True             # no file (or files blocked), but an in-memory export exists
            self.diag.error("reference not loaded: %s" % exc)
            self._hint("Adaptive Follow: no valid reference (see log)")
            return False
        except Exception as exc:  # pragma: no cover - defensive
            self.reference = None
            self.diag.error("reference not loaded (unexpected): %r" % (exc,))
            return False
        self.reference = ref
        self.reference_path = path
        for w in ref.warnings:
            self.diag.warning("reference: " + w)
        self.diag.info("reference %r loaded: %d notes, %d events, PPQ %d (%s)"
                       % (ref.name, len(ref.notes), ref.event_count, ref.ppq, path))
        return True

    def set_reference(self, ref, source):
        """Use ``ref`` (already validated) as the reference. Not while armed."""
        if self.sm.active:
            self.diag.warning("cannot change the reference while armed; disarm first")
            return False
        self.reference = ref
        self.reference_path = source
        for w in ref.warnings:
            self.diag.warning("reference: " + w)
        self.diag.info("reference %r loaded: %d notes, %d events, PPQ %d (%s)"
                       % (ref.name, len(ref.notes), ref.event_count, ref.ppq, source))
        return True

    def poll_handoff(self, force=False):
        """Pick up a reference the Piano Roll exporter published in memory (see handoff.py).
        Only while disarmed; returns True when a new reference was taken."""
        if self.sm.active:
            return False
        serial, text, _name = handoff.latest()
        if not serial or (serial == self._handoff_serial and not force):
            return False
        self._handoff_serial = serial
        try:
            ref = Reference.from_json(text)
        except ReferenceFormatError as exc:
            self.diag.error("in-memory reference is invalid: %s" % exc)
            return False
        return self.set_reference(ref, "in memory, Piano Roll export #%d" % serial)

    def maybe_reload_config(self):
        """Pick up config.json edits while disarmed."""
        if self.sm.active:
            return False
        m = self.storage.config_mtime()
        if m and m != self._config_mtime:
            self.load_config()
            self.diag.info("config.json reloaded")
            return True
        return False

    # =============================================================== arm / disarm
    def arm(self):
        now = self.clock()
        if self.sm.active:
            self.diag.info("already armed")
            return True
        if self.reference is None and not self.load_reference():
            return False
        ref = self.reference
        self.project_ppq = self._safe(self.port.project_ppq, ref.ppq) or ref.ppq
        self.base_bpm = float(self._safe(self.port.tempo, 120.0) or 120.0)
        if not 10.0 <= self.base_bpm <= 999.0:
            self.diag.warning("implausible project tempo %r; assuming 120 BPM" % self.base_bpm)
            self.base_bpm = 120.0
        cfg = self.config
        if cfg.chord_window_ms is not None:
            ticks = int(round(cfg.chord_window_ms / 1000.0 * self.base_bpm / 60.0 * ref.ppq))
            ref = ref.regrouped(ticks)
        spt = 60.0 / (self.base_bpm * ref.ppq)
        fcfg = sf.FollowerConfig(timing_tolerance_s=cfg.timing_tolerance_ms / 1000.0,
                                 min_bpm=cfg.tempo_min_bpm, max_bpm=cfg.tempo_max_bpm)
        self.follower = sf.ScoreFollower(ref, spt, fcfg)
        self.controller = at.TransportController(cfg, ref.ppq, self.base_bpm)
        song_tick = self._safe(self.port.song_tick, 0) or 0
        if cfg.offset_mode == model.OFFSET_PLAYHEAD:
            self.offset_song_ticks = int(song_tick - ref.first_tick * self.project_ppq / float(ref.ppq))
        else:
            self.offset_song_ticks = int(cfg.offset_ticks)
        self._active_reference = ref
        self._pending_start = False
        self.last_note = None
        self.disabled_features = set()
        if cfg.effective_strategy != model.STRATEGY_SPEED_ONLY and not self._safe(self.port.can_set_tempo, False):
            self.disabled_features.add("tempo")
            self.diag.warning("tempo control is not available in this FL Studio version; using playback speed")
        self.sm.set(st.ARMED, "armed (%s, strategy %s%s); base tempo %.2f BPM, offset %d ticks"
                    % ("dry-run" if not cfg.controls_transport else "controlling transport",
                       cfg.effective_strategy, "" if cfg.allow_seek else ", no seek",
                       self.base_bpm, self.offset_song_ticks), now)
        return True

    def disarm(self, reason="disarmed"):
        now = self.clock()
        if not self.sm.active and self.sm.state != st.STOPPED:
            self.sm.set(st.STOPPED, reason, now)
            return
        if self.controller is not None and self.config.controls_transport:
            self._apply(self.controller.restore(), now)
        self._pending_start = False
        self.sm.set(st.STOPPED, reason, now)

    def toggle_arm(self):
        if self.sm.active:
            self.disarm("disarmed by user")
        else:
            self.arm()

    def set_dry_run(self, on):
        if not on and self.config.dry_run and self.sm.active:
            self.diag.info("dry-run off: transport control starts with the next decision")
        if on and not self.config.dry_run and self.controller is not None:
            self._apply(self.controller.restore(), self.clock())
        self.config.dry_run = bool(on)
        self.diag.info("dry-run %s" % ("ON (decisions are only logged)" if on else "OFF"))

    def shutdown(self):
        """FL Studio unloads the script: always leave normal playback behind."""
        if self.sm.active:
            self.disarm("script unloaded")

    # =============================================================== input
    def on_note(self, pitch, velocity, channel, is_on, now=None):
        """Live MIDI note. ``channel`` 1..16. Never consumes the note."""
        if not is_on or velocity <= 0:
            return None
        if self.config.midi_channel and channel != self.config.midi_channel:
            return None
        if velocity < self.config.min_velocity:
            return None
        if not self.sm.active or self.follower is None:
            return None
        now = self.clock() if now is None else now
        was_started = self.follower.started
        res = self.follower.on_note_on(pitch, now)
        self.last_note = res
        if not was_started and self.follower.started and self.config.start_on_first_note:
            self._pending_start = True
        self._sync_state(now)
        if self.diag.debug_enabled:
            self.diag.debug("note %d vel %d ch %d -> %s event=%d (tick %d) conf=%.2f bpm=%.1f state=%s"
                            % (pitch, velocity, channel, res.kind, res.event_index, res.event_tick,
                               res.confidence, self.follower.estimated_bpm, self.sm.state), now)
        return res

    def on_cc(self, cc, value, channel):
        """Optional MIDI controls. Returns True when the CC was used as a control."""
        cfg = self.config
        if value <= 0 or cc is None:
            return False
        if cc == cfg.control_cc_arm_toggle:
            self.toggle_arm()
            return True
        if cc == cfg.control_cc_dry_run_toggle:
            self.set_dry_run(not cfg.dry_run)
            return True
        if cc == cfg.control_cc_reset:
            self.disarm("reset by MIDI control")
            return True
        return False

    def command(self, text):
        """Text commands (command.txt / console): arm, disarm, toggle, dry_run on|off,
        reload, report, status."""
        parts = (text or "").strip().lower().split()
        if not parts:
            return ""
        cmd = parts[0]
        if cmd == "arm":
            self.arm()
        elif cmd in ("disarm", "stop"):
            self.disarm("disarmed by command")
        elif cmd == "toggle":
            self.toggle_arm()
        elif cmd in ("dry_run", "dry-run", "dryrun"):
            self.set_dry_run(len(parts) < 2 or parts[1] in ("on", "1", "true"))
        elif cmd == "reload":
            if self.sm.active:
                self.diag.warning("reload ignored while armed")
            else:
                self.load_config()
                self.reference = None
                self.load_reference()
                self.poll_handoff()         # a newer in-memory export wins
        elif cmd == "report":
            return self.write_report()
        elif cmd == "status":
            return self.status_line()
        else:
            self.diag.warning("unknown command %r" % text)
        return ""

    # =============================================================== loop
    def tick(self, now=None):
        now = self.clock() if now is None else now
        if not self.sm.active or self.follower is None:
            return None
        self.follower.update(now)
        self._sync_state(now)
        cfg = self.config
        playing = bool(self._safe(self.port.is_playing, False))
        song_tick = self._safe(self.port.song_tick, None)
        if song_tick is None:
            return None
        ref_tick = (song_tick - self.offset_song_ticks) * self._active_reference.ppq / float(self.project_ppq)

        # The user stopped FL Studio while we were controlling it: end the session cleanly.
        if cfg.controls_transport and self.controller.started_playback and not playing \
                and not self.controller.paused_by_us and not self._pending_start:
            self.disarm("transport stopped by the user")
            return None

        snap = self.follower.snapshot()
        d = self.controller.step(now, snap, ref_tick, playing, self.sm.state)
        if self._pending_start:
            self._pending_start = False
            if not playing:
                d.action = at.ACTION_START
                d.reason = "first confident note: starting playback"
            else:
                self.controller.started_playback = True
        if "tempo" in self.disabled_features:
            d.tempo_bpm = None
        if d.action == at.ACTION_SEEK and "seek" in self.disabled_features:
            d.action = None
        self.last_decision = d
        self._apply(d, now)
        if self.diag.debug_enabled:
            self.diag.status(now, self.status_line(now, song_tick, ref_tick, d))
        return d

    # =============================================================== internals
    def _sync_state(self, now):
        if not self.sm.active or self.follower is None:
            return
        fstate = self.follower.state
        new = st.ARMED if fstate == sf.F_IDLE else st.from_follower(fstate)
        self.sm.set(new, self.follower.state_reason, now)

    def _on_transition(self, t):
        self.diag.info("state %s -> %s: %s" % (t.old, t.new, t.reason), t.time)
        self._hint("Adaptive Follow: %s" % t.new)

    def _hint(self, text):
        if not self.config.hint_messages or text == self._last_hint:
            return
        self._last_hint = text
        try:
            self.port.hint(text)
        except Exception:
            pass

    def _safe(self, fn, default):
        try:
            return fn()
        except Exception as exc:
            self.diag.error("FL Studio call %s failed: %r" % (getattr(fn, "__name__", fn), exc))
            return default

    def _apply(self, d, now):
        if d is None or d.empty():
            return
        dry = not self.config.controls_transport
        prefix = "[dry-run] would " if dry else ""
        if d.action is not None or d.tempo_bpm is not None:
            self.diag.info("%s%s%s" % (prefix, d.describe(), (" - " + d.reason) if d.reason else ""), now)
        elif self.diag.debug_enabled and d.speed is not None:
            self.diag.debug("%sspeed %.3f (rate cmd %.3f, phase error %.1f ticks)"
                            % (prefix, d.speed, d.rate_cmd, d.phase_error_ticks), now)
        if dry:
            self.controller.note_applied(d, now)   # keep the simulated state consistent
            return
        port = self.port
        try:
            if d.tempo_bpm is not None and "tempo" not in self.disabled_features:
                port.set_tempo(d.tempo_bpm)
        except Exception as exc:
            self.disabled_features.add("tempo")
            self.diag.error("tempo change failed (%r); tempo control disabled for this session" % (exc,), now)
        try:
            if d.speed is not None:
                port.set_speed(d.speed)
        except Exception as exc:
            self.diag.error("setPlaybackSpeed failed: %r" % (exc,), now)
        try:
            if d.action in (at.ACTION_START, at.ACTION_RESUME):
                if not port.is_playing():
                    port.play_pause()
            elif d.action == at.ACTION_PAUSE:
                if port.is_playing():
                    port.play_pause()
            elif d.action == at.ACTION_SEEK:
                if port.can_seek():
                    song = int(round(self.offset_song_ticks + d.seek_tick * self.project_ppq
                                     / float(self._active_reference.ppq)))
                    port.set_song_tick(max(0, song))
                else:
                    self.disabled_features.add("seek")
                    self.diag.warning("seek requested but setSongPos(ticks) is unavailable; seek disabled", now)
        except Exception as exc:
            self.diag.error("transport action %s failed: %r" % (d.action, exc), now)
            return
        self.controller.note_applied(d, now)

    # =============================================================== reporting
    def status_line(self, now=None, song_tick=None, ref_tick=None, d=None):
        now = self.clock() if now is None else now
        f = self.follower
        parts = ["state=%s" % self.sm.state]
        if f is not None:
            parts.append("conf=%.2f" % f.confidence)
            parts.append("performer_bpm=%.1f" % f.estimated_bpm)
            parts.append("event=%d/%d" % (f.position, f.ref.event_count))
            parts.append("target_tick=%.0f" % f.position_ticks(now))
        if song_tick is not None:
            parts.append("song_tick=%d" % song_tick)
        if ref_tick is not None:
            parts.append("ref_tick=%.0f" % ref_tick)
        if self.controller is not None:
            parts.append("speed=%.3f" % self.controller.sent_speed)
            parts.append("tempo=%.2f (base %.2f)" % (self.controller.sent_tempo, self.base_bpm))
        if d is not None:
            parts.append("phase_err=%.1f" % d.phase_error_ticks)
        parts.append("dry_run=%s" % self.config.dry_run)
        return " ".join(parts)

    def report_header(self):
        lines = ["version: %s" % VERSION,
                 "state: %s" % self.sm.state,
                 "reference: %s (%s)" % (self.reference.name if self.reference else "-", self.reference_path or "-"),
                 "data dir: %s (%s)" % (self.storage.data_dir,
                                         "writable" if getattr(self.storage, "writable", True) else "NOT writable"),
                 handoff.status(),
                 "config: %s" % compat.json_dumps(self.config.to_dict(), indent=None),
                 "disabled features: %s" % (", ".join(sorted(self.disabled_features)) or "-"),
                 "status: %s" % self.status_line(),
                 "recent transitions:"]
        lines.extend("  %.3f %r" % (t.time, t) for t in self.sm.history[-20:])
        return lines

    def write_report(self):
        text = self.diag.report(self.report_header())
        try:
            path = self.storage.write_report(text)
            self.diag.info("diagnostic report written: %s" % path)
            return path
        except Exception as exc:
            self.diag.error("cannot write report: %r" % (exc,))
            return ""
