"""
Adaptive transport controller: turns follower state into FL Studio transport
decisions (playback speed, tempo, play/pause, seek).

Adapted from Cadenza's ``Source/Adaptive/AdaptiveTransport`` (C++). In
Cadenza the controller *owns* the playhead; here FL Studio does, so the
controller closes the loop on the *measured* song position:

    target   P* = anchor + (now - anchor_live) * rate   (capped at the next unplayed event)
    error    e  = P* - playhead                           (nominal seconds)
    command  r  = rate + clamp(gain * e / T, +-max_corr * rate)

The performer rate is a feed-forward term (tempo error); the bounded,
slew-limited correction removes phase error, so a band that is half a bar
ahead at the right tempo slows down briefly instead of staying ahead.
``r`` is applied as playback speed (``speed_only``), project tempo
(``tempo_only``) or both (``hybrid``: tempo follows the performer slowly,
speed does the fast phase corrections).

Safety rules: no seek unless allowed, confident and after a confirmed
re-acquisition; when the performer is late the band brakes and (by default)
pauses at their next expected event; when LOST the speed glides back to 1.0;
``restore()`` returns speed 1.0 and the original tempo.

Pure Python, no FL Studio dependency.
"""

from . import model
from . import state as st

ACTION_NONE = None
ACTION_START = "start"     # begin playback (transport.start while stopped)
ACTION_PAUSE = "pause"     # pause playback (transport.start while playing = play/pause)
ACTION_RESUME = "resume"   # resume after our own pause
ACTION_SEEK = "seek"       # setSongPos (only when allowed)

INF = float("inf")


class Decision(object):
    __slots__ = ("speed", "tempo_bpm", "action", "seek_tick", "reason", "target_tick", "phase_error_ticks",
                 "rate_cmd")

    def __init__(self):
        self.speed = None          # new playback speed, or None = unchanged
        self.tempo_bpm = None      # new project tempo, or None = unchanged
        self.action = ACTION_NONE
        self.seek_tick = None      # reference ticks
        self.reason = ""
        self.target_tick = None    # reference ticks
        self.phase_error_ticks = 0.0
        self.rate_cmd = 1.0

    def empty(self):
        return self.speed is None and self.tempo_bpm is None and self.action is None

    def describe(self):
        parts = []
        if self.action:
            parts.append(self.action + ("@%d" % self.seek_tick if self.seek_tick is not None else ""))
        if self.speed is not None:
            parts.append("speed=%.3f" % self.speed)
        if self.tempo_bpm is not None:
            parts.append("tempo=%.2f" % self.tempo_bpm)
        return ", ".join(parts) or "-"


class TransportController(object):
    BRAKE_TIME_S = 0.10
    SPEED_EPSILON = 0.004
    SPEED_MIN_INTERVAL_S = 0.1   # at most ~10 setPlaybackSpeed calls per second...
    SPEED_URGENT_DELTA = 0.05    # ...unless the change is large (braking, start)
    PAUSE_LOOKAHEAD_S = 0.03     # pause one idle period early: FL cannot go below 0.25x
    CATCHUP_ERROR_S = 0.25       # behind by more than this: catchup_max_correction applies
    BRAKE_SLEW_PER_S = 4.0       # braking into a hold point may slow down faster than speed_slew_per_s

    def __init__(self, config, ppq, base_bpm):
        self.cfg = config
        self.ppq = int(ppq)
        self.base_bpm = float(base_bpm) if base_bpm > 0 else 120.0
        self.spt = 60.0 / (self.base_bpm * self.ppq)
        self.reset()

    def reset(self):
        self.speed = 1.0            # smoothed command
        self.sent_speed = 1.0       # last value sent to FL
        self.sent_tempo = self.base_bpm
        self.tempo_changed = False
        self.speed_changed = False
        self.paused_by_us = False
        self._paused_at = 0.0
        self._after_brake = False   # releasing a brake: speed may rise quickly again
        self._last_t = None
        self._tempo_rate = 1.0      # hybrid: smoothed performer rate
        self._last_tempo_t = -1e9
        self._last_speed_t = -1e9
        self._seen_epoch = None
        self._reacquired_epoch = None
        self._seeked_epoch = None
        self.started_playback = False

    # ------------------------------------------------------------------
    def _clamp_speed(self, s, braking=False):
        # While braking into a hold point the floor is FL's own minimum (0.25x),
        # so the grace period before a pause overshoots as little as possible.
        lo = model.FL_SPEED_MIN if braking else max(model.FL_SPEED_MIN, self.cfg.speed_min)
        hi = min(model.FL_SPEED_MAX, self.cfg.speed_max)
        return min(hi, max(lo, s))

    def _clamp_tempo(self, bpm):
        return min(self.cfg.tempo_max_bpm, max(self.cfg.tempo_min_bpm, bpm))

    def step(self, now, snap, playhead_tick, playing, session_state):
        """One control step. ``playhead_tick`` = FL song position converted to
        reference ticks. Returns a Decision (possibly empty)."""
        d = Decision()
        dt = 0.0 if self._last_t is None else min(0.25, max(0.0, now - self._last_t))
        self._last_t = now
        cfg = self.cfg
        tol = cfg.hold_tolerance_beats * self.ppq
        strategy = cfg.effective_strategy

        if session_state in (st.IDLE, st.STOPPED):
            return d

        if self._seen_epoch is None:
            self._seen_epoch = snap.epoch
        if snap.epoch != self._seen_epoch:
            self._seen_epoch = snap.epoch
            if session_state == st.RECOVERING:
                self._reacquired_epoch = snap.epoch

        rate = max(0.01, snap.rate or 1.0)
        rate_cmd = 1.0
        target = playhead_tick
        braking = False

        if session_state == st.ARMED:
            # Waiting for the first match. If FL is already playing (an intro),
            # run at normal speed and wait at the performer's entry.
            hold = snap.hold_tick
            rate_cmd = 1.0
            if playing and hold != INF and cfg.wait_behavior != model.WAIT_CONTINUE:
                room = (hold + tol - playhead_tick) * self.spt
                if room <= self.speed * self.PAUSE_LOOKAHEAD_S and cfg.wait_behavior == model.WAIT_PAUSE \
                        and not self.paused_by_us:
                    d.action = ACTION_PAUSE
                    d.reason = "intro reached the performer's entry; waiting for the first note"
                elif room < self.BRAKE_TIME_S * 2:
                    rate_cmd = cfg.speed_min
            target = playhead_tick
        else:
            # Performer-driven target.
            hold = snap.hold_tick if snap.hold_tick is not None else INF
            target = snap.anchor_tick + max(0.0, now - snap.anchor_live) * rate / self.spt
            if hold != INF:
                target = min(target, hold + tol)
            e_s = (target - playhead_tick) * self.spt
            d.target_tick = target
            d.phase_error_ticks = target - playhead_tick

            if session_state == st.LOST:
                rate_cmd = 1.0
                if cfg.lost_behavior == model.LOST_PAUSE and playing and not self.paused_by_us:
                    d.action = ACTION_PAUSE
                    d.reason = "performer lost; pausing (lost_behavior=pause)"
                elif cfg.lost_behavior == model.LOST_CONTINUE and self.paused_by_us:
                    d.action = ACTION_RESUME
                    d.reason = "performer lost; continuing at normal speed (lost_behavior=continue)"
            else:
                if session_state == st.TRACKING:
                    gain = 1.0 if snap.confidence >= 0.6 else 0.3
                elif session_state == st.RECOVERING:
                    gain = 0.3
                else:
                    gain = 0.0          # WAITING: no phase chasing
                # Far behind (a missed note, after our own pause): catch up faster.
                # Being ahead is always corrected gently.
                limit_corr = cfg.max_phase_correction
                if e_s > self.CATCHUP_ERROR_S:
                    limit_corr = max(limit_corr, cfg.catchup_max_correction)
                max_corr = limit_corr * rate
                corr = gain * e_s / max(0.05, cfg.phase_correction_time_s)
                corr = min(max_corr, max(-max_corr, corr))
                rate_cmd = max(rate + corr, 0.4 * rate)

                # Late for the next event (or past it): brake into the hold point.
                expected_at_hold = INF if hold == INF else snap.anchor_live + (hold - snap.anchor_tick) * self.spt / rate
                late = hold != INF and (playhead_tick >= hold or now > expected_at_hold)
                if late and cfg.wait_behavior != model.WAIT_CONTINUE:
                    room = (hold + tol - playhead_tick) * self.spt
                    limit = max(0.0, room) / self.BRAKE_TIME_S
                    braking = limit < rate_cmd
                    rate_cmd = min(rate_cmd, limit)
                    # Short lateness (rubato, a missed note) only brakes; pausing
                    # for a few ms would stutter. Pause once clearly waiting.
                    lateness = now - expected_at_hold if expected_at_hold != INF else 0.0
                    waiting = session_state == st.WAITING or lateness >= cfg.pause_grace_s
                    if playing and waiting and room <= self.speed * self.PAUSE_LOOKAHEAD_S \
                            and cfg.wait_behavior == model.WAIT_PAUSE and not self.paused_by_us:
                        d.action = ACTION_PAUSE
                        d.reason = "performer is late; pausing at the next expected event"

                # Resume after our own pause once the performer moved on.
                if self.paused_by_us and snap.anchor_live > self._paused_at \
                        and session_state in (st.TRACKING, st.RECOVERING):
                    d.action = ACTION_RESUME
                    d.reason = "performer continued"

                # Seek only after a confirmed, confident re-acquisition.
                if (cfg.allow_seek and session_state == st.TRACKING and self._reacquired_epoch is not None
                        and self._reacquired_epoch == snap.epoch and self._seeked_epoch != snap.epoch
                        and snap.confidence >= cfg.seek_min_confidence
                        and abs(target - playhead_tick) >= cfg.seek_threshold_beats * self.ppq):
                    d.action = ACTION_SEEK
                    d.seek_tick = int(round(target))
                    d.reason = "confirmed re-acquisition %.1f beats away" % ((target - playhead_tick) / self.ppq)
                    self._seeked_epoch = snap.epoch

            # LOST with pause: resume when found again.
            if self.paused_by_us and session_state == st.RECOVERING and d.action is None:
                d.action = ACTION_RESUME
                d.reason = "performer found again"

        d.rate_cmd = rate_cmd

        # ---- map the rate command onto speed / tempo ----
        if strategy == model.STRATEGY_TEMPO_ONLY:
            tempo_target = self._clamp_tempo(self.base_bpm * rate_cmd)
            speed_target = 1.0
        elif strategy == model.STRATEGY_HYBRID:
            goal = 1.0 if session_state in (st.LOST, st.ARMED) else rate
            k = min(1.0, dt / max(0.1, cfg.tempo_smoothing_s))
            self._tempo_rate += (goal - self._tempo_rate) * k
            tempo_target = self._clamp_tempo(self.base_bpm * self._tempo_rate)
            speed_target = self._clamp_speed(rate_cmd * self.base_bpm / tempo_target, braking)
        else:
            tempo_target = None
            speed_target = self._clamp_speed(rate_cmd, braking)

        # Braking into a hold point, and releasing that brake once the performer
        # continues, may change speed faster than the normal slew limit.
        if braking:
            self._after_brake = True
        elif self._after_brake and self.speed >= min(speed_target, rate * (1.0 - cfg.max_phase_correction)):
            self._after_brake = False
        fast = braking or (self._after_brake and speed_target > self.speed)
        slew = max(cfg.speed_slew_per_s, self.BRAKE_SLEW_PER_S) if fast else cfg.speed_slew_per_s
        max_step = slew * (dt if dt > 0 else 0.02)
        if session_state == st.ARMED and not playing:
            self.speed = speed_target   # nothing audible yet: no slew needed
        else:
            self.speed = min(self.speed + max_step, max(self.speed - max_step, speed_target))
        if abs(self.speed - speed_target) < self.SPEED_EPSILON:
            self.speed = speed_target
        delta = abs(self.speed - self.sent_speed)
        due = now - self._last_speed_t >= self.SPEED_MIN_INTERVAL_S or delta >= self.SPEED_URGENT_DELTA
        if due and (delta >= self.SPEED_EPSILON or (self.speed == speed_target and self.speed != self.sent_speed)):
            d.speed = round(self.speed, 4)
            self.sent_speed = d.speed
            self._last_speed_t = now

        if tempo_target is not None:
            if abs(tempo_target - self.sent_tempo) >= cfg.tempo_update_min_bpm and now - self._last_tempo_t >= 0.25:
                d.tempo_bpm = round(tempo_target, 3)
                self.sent_tempo = d.tempo_bpm
                self._last_tempo_t = now
        return d

    # ------------------------------------------------------------------
    def note_applied(self, decision, now):
        """Book-keeping after the session applied (or simulated) a decision."""
        if decision.action == ACTION_PAUSE:
            self.paused_by_us = True
            self._paused_at = now
        elif decision.action in (ACTION_RESUME, ACTION_START):
            self.paused_by_us = False
            self.started_playback = True
        if decision.speed is not None:
            self.speed_changed = decision.speed != 1.0
        if decision.tempo_bpm is not None:
            self.tempo_changed = abs(decision.tempo_bpm - self.base_bpm) > 1e-6

    def restore(self):
        """Decision that returns FL Studio to normal: speed 1.0, original tempo."""
        d = Decision()
        d.speed = 1.0
        if self.cfg.effective_strategy != model.STRATEGY_SPEED_ONLY or self.tempo_changed:
            d.tempo_bpm = self.base_bpm
        d.reason = "restore normal playback"
        self.speed = 1.0
        self.sent_speed = 1.0
        self.sent_tempo = self.base_bpm
        self.tempo_changed = False
        self.speed_changed = False
        self.paused_by_us = False
        return d
