"""
Online score follower: aligns live Note On events with a Reference.

Python adaptation of Cadenza's ``Source/Adaptive/ScoreFollower`` (C++):

* Tracking: a windowed Viterbi / local-alignment DP over reference events.
  Transitions per live note: another note of the same chord, advance (with a
  skipped-note penalty and a timing cost), a small backward move, or an extra
  (wrong) note. Timing tolerates lateness more than earliness.
* Confidence: EMA of per-note match quality on the best path, reduced when a
  competing position scores almost as well (ambiguity).
* Loss and re-acquisition: after several consecutive misses the follower is
  LOST and searches the whole reference. *Difference from Cadenza:* instead of
  a full Smith-Waterman pass (too slow for FL Studio's single-threaded script
  loop in pure Python) it scores only candidate events that contain the latest
  pitch, with a short tolerant backward alignment of the recent notes.

Pure Python 3.6+, no FL Studio dependency.
"""

import math

from .tempo_estimator import TempoEstimator

# Follower-internal tracking states (mapped to the public session states in state.py).
F_IDLE = "IDLE"
F_TRACKING = "TRACKING"
F_UNCERTAIN = "UNCERTAIN"
F_WAITING = "WAITING"
F_LOST = "LOST"
F_REACQUIRING = "REACQUIRING"

# Match kinds (diagnostics).
K_NONE = "none"
K_ADVANCE = "advance"
K_CHORD = "chord-note"
K_BACKWARD = "backward"
K_EXTRA = "extra"
K_REACQUIRED = "reacquired"
K_IGNORED = "ignored"

INF = float("inf")


class FollowerConfig(object):
    def __init__(self, **kw):
        # window
        self.window_behind = 3
        self.window_ahead = 10
        self.max_jump = 5
        self.max_backward = 2
        # scores
        self.match_score = 1.0
        self.extra_penalty = 0.7
        self.skip_penalty = 0.45
        self.backward_penalty = 1.2
        self.timing_weight = 0.6
        self.timing_tolerance_s = 0.10
        self.timing_tolerance_rel = 0.30
        self.late_discount = 0.35
        self.chord_spread_s = 0.18
        self.spread_penalty = 0.4
        # confidence / states
        self.confidence_smoothing = 0.3
        self.ambiguity_margin = 1.0
        self.uncertain_below = 0.45
        self.tracking_above = 0.6
        self.lost_below = 0.2
        self.lost_after_misses = 5
        self.wait_grace_s = 0.35
        self.wait_relative = 0.5
        # re-acquisition
        self.history_length = 10
        self.reacquire_min_matches = 4
        self.reacquire_margin = 1.0
        self.reacquire_confirm = 3
        # start
        self.start_event = 0
        self.start_anywhere = False
        # tempo
        self.min_bpm = 40.0
        self.max_bpm = 240.0
        self.tempo_smoothing = 0.5
        for k, v in kw.items():
            if not hasattr(self, k):
                raise AttributeError("unknown follower option %r" % k)
            setattr(self, k, v)
        self.max_jump = max(1, min(32, int(self.max_jump)))
        self.window_behind = max(1, int(self.window_behind))
        self.window_ahead = max(self.max_jump, int(self.window_ahead))
        self.max_backward = max(0, min(self.window_behind, int(self.max_backward)))
        self.history_length = max(2, int(self.history_length))
        self.reacquire_min_matches = max(2, min(self.history_length, int(self.reacquire_min_matches)))


class _Cell(object):
    __slots__ = ("score", "consumed", "enter", "last", "quality", "misses", "in_row", "kind", "timed")

    def __init__(self, score=0.0, consumed=0, enter=0.0, last=0.0, quality=0.0, misses=0, in_row=0,
                 kind=K_NONE, timed=False):
        self.score = score
        self.consumed = consumed
        self.enter = enter
        self.last = last
        self.quality = quality
        self.misses = misses
        self.in_row = in_row
        self.kind = kind
        self.timed = timed

    def copy(self):
        return _Cell(self.score, self.consumed, self.enter, self.last, self.quality, self.misses,
                     self.in_row, self.kind, self.timed)


class FollowerSnapshot(object):
    """What the transport controller needs (plain values)."""
    __slots__ = ("state", "epoch", "event_index", "anchor_tick", "anchor_live", "hold_tick", "rate",
                 "confidence", "last_note_live", "started")

    def __init__(self, **kw):
        for k in self.__slots__:
            setattr(self, k, kw.get(k))


class NoteResult(object):
    """Per-note diagnostics."""
    __slots__ = ("pitch", "live", "kind", "event_index", "event_tick", "rate", "confidence", "margin",
                 "state", "state_reason")

    def __init__(self, pitch, live):
        self.pitch = pitch
        self.live = live
        self.kind = K_NONE
        self.event_index = -1
        self.event_tick = 0
        self.rate = 1.0
        self.confidence = 0.0
        self.margin = 0.0
        self.state = F_IDLE
        self.state_reason = ""


class ScoreFollower(object):
    def __init__(self, reference, seconds_per_tick, config=None):
        """``seconds_per_tick``: duration of one reference tick at the project
        (base) tempo, i.e. 60 / (bpm * ppq)."""
        self.ref = reference
        self.cfg = config or FollowerConfig()
        self.spt = float(seconds_per_tick)
        base_bpm = 60.0 / (self.spt * reference.ppq) if self.spt > 0 else 120.0
        self.tempo = TempoEstimator(min_bpm=self.cfg.min_bpm, max_bpm=self.cfg.max_bpm,
                                    smoothing=self.cfg.tempo_smoothing, base_bpm=base_bpm)
        self._pitch_index = reference.pitch_index()
        self.epoch = 0
        self.reset()

    # ------------------------------------------------------------- helpers
    def nominal(self, event_index):
        return self.ref.events[event_index].tick * self.spt

    def _ev(self, i):
        return self.ref.events[i]

    def _window_size(self):
        return self.cfg.window_behind + self.cfg.window_ahead + 2

    # ------------------------------------------------------------- control
    def reset(self):
        n = self.ref.event_count
        start = min(max(0, int(self.cfg.start_event)), max(0, n - 1))
        self.tempo.reset(1.0)
        self.state = F_IDLE
        self.state_reason = "reset"
        self.epoch += 1
        self.confidence = 0.0
        self.started = False
        self.matched_notes = 0
        self.extra_notes = 0
        self.last_note_live = 0.0
        self._history = []
        self._search_notes = 0
        self._reacquire_matches = 0
        self._seed_start(start)

    def seek_to_event(self, index):
        rate = self.tempo.rate
        self.cfg.start_event = index
        self.reset()
        self.tempo.reset(rate)

    def _seed_start(self, start):
        seed = _Cell(quality=0.6)
        if start - 1 >= 0:
            seed.consumed = self._ev(start - 1).mask
        self._seed_at(start - 1, seed)
        self.position = start - 1
        self._last_anchor_event = start - 1
        self.anchor_tick = self._ev(start).tick if self.ref.event_count else 0
        self.anchor_live = 0.0

    def _seed_at(self, event_index, cell):
        self._cells = [None] * self._window_size()
        self._base = max(-1, event_index - self.cfg.window_behind)
        self._cells[event_index - self._base] = cell

    def _cell(self, cells, event_index):
        k = event_index - self._base
        if 0 <= k < len(cells):
            return cells[k]
        return None

    def _recenter(self, best):
        new_base = max(-1, best - self.cfg.window_behind)
        if new_base == self._base:
            return
        shifted = [None] * len(self._cells)
        for k, c in enumerate(self._cells):
            nk = self._base + k - new_base
            if 0 <= nk < len(shifted):
                shifted[nk] = c
        self._cells = shifted
        self._base = new_base

    def _set_state(self, state, reason):
        if state != self.state:
            self.state = state
            self.state_reason = reason

    # ------------------------------------------------------------- timing
    def _timing_cost(self, expected, actual, nominal_gap):
        rate = max(0.05, self.tempo.rate)
        gap_live = max(0.0, nominal_gap) / rate
        tol = self.cfg.timing_tolerance_s + self.cfg.timing_tolerance_rel * gap_live
        dev = actual - expected
        if dev > 0:
            dev *= self.cfg.late_discount
        x = dev / max(1e-3, tol)
        # smooth, bounded (0 .. 2*timing_weight)
        return 2.0 * self.cfg.timing_weight * (1.0 - math.exp(-0.5 * x * x))

    def _unplayed(self, cell, event_index):
        if event_index < 0:
            return 0
        mask = self._ev(event_index).mask
        return bin(mask).count("1") - bin(mask & cell.consumed).count("1")

    def hold_tick(self):
        """Tick of the next reference event the performer has not played yet."""
        nxt = self.position + 1
        if nxt >= self.ref.event_count:
            return INF
        return self._ev(max(0, nxt)).tick

    # ------------------------------------------------------------- input
    def on_note_on(self, pitch, live):
        res = NoteResult(pitch, live)
        self.last_note_live = live
        if self.ref.empty or not 0 <= pitch <= 127:
            res.kind = K_IGNORED
            res.state = self.state
            return res
        cfg = self.cfg

        self._history.append((pitch, live))
        if len(self._history) > cfg.history_length:
            self._history.pop(0)
        searching = self.state == F_LOST or (self.state == F_IDLE and cfg.start_anywhere)
        if searching:
            self._search_notes = min(self._search_notes + 1, len(self._history))

        # ---- global re-acquisition ----
        if searching and self._search_notes >= cfg.reacquire_min_matches:
            found = self._global_search()
            if found is not None:
                index, cell, anchors = found
                self._seed_at(index, cell)
                self.position = index
                self._last_anchor_event = index
                self.anchor_tick = self._ev(index).tick
                self.anchor_live = cell.enter
                self.tempo.break_segment()
                for ev_i, t in anchors:
                    self.tempo.add_anchor(self.nominal(ev_i), t)
                self.epoch += 1
                self._reacquire_matches = 0
                self._search_notes = 0
                self.started = True
                self.confidence = cell.quality
                self._set_state(F_REACQUIRING, "global search found the performer at event %d" % index)
                res.kind = K_REACQUIRED
                res.event_index = index
                res.event_tick = self.anchor_tick
                res.rate = self.tempo.rate
                res.confidence = self.confidence
                res.state = self.state
                res.state_reason = self.state_reason
                return res

        # ---- windowed DP step ----
        alpha = cfg.confidence_smoothing
        n_events = self.ref.event_count
        cells = self._cells
        nxt = [None] * len(cells)
        for k in range(len(cells)):
            j = self._base + k
            best = None
            selfc = cells[k]
            real = 0 <= j < n_events
            in_group = real and self._ev(j).has(pitch)

            if selfc is not None:                                  # (c) extra / wrong note
                c = selfc.copy()
                c.score -= cfg.extra_penalty
                c.misses += 1
                c.in_row = 0
                c.quality += alpha * (0.0 - c.quality)
                c.kind = K_EXTRA
                best = c
            if in_group:
                if selfc is not None and not (selfc.consumed >> pitch) & 1:   # (a) chord note
                    late = selfc.timed and (live - selfc.enter) > cfg.chord_spread_s
                    c = selfc.copy()
                    c.score += cfg.match_score - (cfg.spread_penalty if late else 0.0)
                    c.consumed |= 1 << pitch
                    c.last = live
                    c.misses = 0
                    c.in_row += 1
                    c.quality += alpha * ((0.6 if late else 1.0) - c.quality)
                    c.kind = K_CHORD
                    if best is None or c.score > best.score:
                        best = c
                between = 0                                         # (b) advance i -> j
                i = j - 1
                lo = max(self._base, j - cfg.max_jump)
                while i >= lo:
                    if i < j - 1 and i + 1 >= 0:
                        between += self._ev(i + 1).size
                    frm = self._cell(cells, i)
                    if frm is not None:
                        skipped = self._unplayed(frm, i) + between
                        tc = 0.0
                        if frm.timed:
                            from_nom = self.nominal(i) if i >= 0 else self.nominal(0)
                            gap = self.nominal(j) - from_nom
                            expected = frm.enter + gap / max(0.05, self.tempo.rate)
                            tc = self._timing_cost(expected, live, gap)
                        score = frm.score + cfg.match_score - cfg.skip_penalty * skipped - tc
                        if best is None or score > best.score:
                            q = 1.0 - tc / (2.0 * max(1e-6, cfg.timing_weight)) * 0.5 - 0.15 * skipped
                            q = min(1.0, max(0.0, q))
                            best = _Cell(score, 1 << pitch, live, live, frm.quality + alpha * (q - frm.quality),
                                         0, frm.in_row + 1, K_ADVANCE, True)
                    i -= 1
                for i in range(j + 1, j + cfg.max_backward + 1):      # (d) small backward move
                    frm = self._cell(cells, i)
                    if frm is None:
                        continue
                    score = frm.score + cfg.match_score - cfg.backward_penalty * (i - j)
                    if best is None or score > best.score:
                        best = _Cell(score, 1 << pitch, live, live, frm.quality + alpha * (0.5 - frm.quality),
                                     0, 1, K_BACKWARD, True)
            nxt[k] = best

        best_k = -1
        for k, c in enumerate(nxt):
            if c is not None and (best_k < 0 or c.score > nxt[best_k].score + 1e-9):
                best_k = k
        if best_k < 0:   # cannot happen: extra transitions keep cells alive
            self.reset()
            res.kind = K_IGNORED
            res.state = self.state
            return res
        best_score = nxt[best_k].score
        second = -INF
        for k, c in enumerate(nxt):
            if c is None:
                continue
            c.score -= best_score
            if abs(k - best_k) >= 2:
                second = max(second, c.score)
        self._cells = nxt
        best = self._base + best_k
        bc = nxt[best_k]
        margin = -second if second != -INF else 10.0

        self.position = best
        matched = bc.kind != K_EXTRA
        if matched:
            self.matched_notes += 1
        else:
            self.extra_notes += 1

        if best >= 0 and matched and best != self._last_anchor_event:
            if best < self._last_anchor_event:
                self.tempo.break_segment()
            self.anchor_tick = self._ev(best).tick
            self.anchor_live = bc.enter
            self.tempo.add_anchor(self.nominal(best), bc.enter)
            self._last_anchor_event = best

        amb = 0.6 + 0.4 * min(1.0, max(0.0, margin / max(1e-6, cfg.ambiguity_margin)))
        self.confidence = min(1.0, max(0.0, bc.quality * amb))

        # ---- state machine (note-driven) ----
        st = self.state
        if st == F_IDLE:
            if matched and best >= 0:
                self.started = True
                self._set_state(F_TRACKING if self.confidence >= cfg.uncertain_below else F_UNCERTAIN,
                                "first matching note (event %d)" % best)
        elif st in (F_TRACKING, F_UNCERTAIN, F_WAITING):
            if bc.misses >= cfg.lost_after_misses:
                self._set_state(F_LOST, "%d consecutive notes did not match the reference" % bc.misses)
                self._search_notes = min(bc.misses, len(self._history))
            elif self.confidence < cfg.lost_below:
                self._set_state(F_LOST, "confidence %.2f below %.2f" % (self.confidence, cfg.lost_below))
                self._search_notes = min(max(bc.misses, 1), len(self._history))
            elif matched:
                if self.confidence >= cfg.tracking_above:
                    self._set_state(F_TRACKING, "confident match")
                elif self.confidence < cfg.uncertain_below:
                    self._set_state(F_UNCERTAIN, "low confidence %.2f" % self.confidence)
                elif st == F_WAITING:
                    self._set_state(F_TRACKING, "performer resumed")
            elif self.confidence < cfg.uncertain_below:
                self._set_state(F_UNCERTAIN, "unmatched note, confidence %.2f" % self.confidence)
        elif st == F_REACQUIRING:
            if matched:
                self._reacquire_matches += 1
                if self._reacquire_matches >= cfg.reacquire_confirm:
                    self._set_state(F_TRACKING if self.confidence >= cfg.uncertain_below else F_UNCERTAIN,
                                    "re-acquisition confirmed by %d notes" % self._reacquire_matches)
            elif bc.misses >= 2:
                self._set_state(F_LOST, "re-acquired position not confirmed")
                self._search_notes = min(bc.misses, len(self._history))
        elif st == F_LOST:
            if matched and bc.in_row >= cfg.reacquire_confirm + 1 and self.confidence >= cfg.uncertain_below:
                self._set_state(F_TRACKING, "performer found again nearby")
                self._search_notes = 0

        self._recenter(max(best, -1))

        res.kind = bc.kind
        res.event_index = best
        res.event_tick = self._ev(best).tick if best >= 0 else 0
        res.rate = self.tempo.rate
        res.confidence = self.confidence
        res.margin = margin
        res.state = self.state
        res.state_reason = self.state_reason
        return res

    def update(self, now):
        """Time-driven transitions (WAITING). Call regularly (e.g. from OnIdle)."""
        if self.ref.empty or not self.started:
            return
        if self.state not in (F_TRACKING, F_UNCERTAIN):
            return
        nxt = self.position + 1
        if nxt <= 0 or nxt >= self.ref.event_count:
            return
        rate = max(0.05, self.tempo.rate)
        ioi = (self._ev(nxt).tick - self.anchor_tick) * self.spt / rate
        if now > self.anchor_live + ioi + self.cfg.wait_grace_s + self.cfg.wait_relative * max(0.0, ioi):
            self._set_state(F_WAITING, "no note for %.2f s after the expected event %d" %
                            (now - self.anchor_live - ioi, nxt))

    # ------------------------------------------------------------- search
    def _global_search(self):
        cfg = self.cfg
        use = min(self._search_notes, len(self._history))
        notes = self._history[-use:]
        if len(notes) < cfg.reacquire_min_matches:
            return None
        candidates = self._pitch_index.get(notes[-1][0], [])
        results = []
        for cand in candidates:
            r = self._align_back(cand, notes)
            if r is not None and r[1] >= cfg.reacquire_min_matches:
                results.append((r[0], cand, r))
        if not results:
            return None
        results.sort(key=lambda x: -x[0])
        best_score = results[0][0]
        hint = max(0, self.position)
        chosen = results[0]
        ties = 0
        for score, cand, r in results[1:]:
            if abs(cand - results[0][1]) <= cfg.max_jump:
                continue
            if best_score - score < cfg.reacquire_margin:
                ties += 1
                def dist(c):
                    return c - hint if c >= hint else (hint - c) + 10 ** 6
                if dist(cand) < dist(chosen[1]):
                    chosen = (score, cand, r)
        _score, index, (sc, matches, mask, enter, anchors) = chosen
        cell = _Cell(0.0, mask, enter, self.last_note_live, 0.45 if ties else 0.6, 0, matches,
                     K_REACQUIRED, True)
        return index, cell, anchors

    def _align_back(self, cand, notes):
        """Tolerant backward alignment of ``notes`` ending at event ``cand``.
        Returns (score, matches, mask_in_cand, enter_live_in_cand, anchors)."""
        cfg = self.cfg
        g = cand
        consumed = 0
        matches = extras = skipped = 0
        mask_cand = 0
        enter_cand = notes[-1][1]
        group_first = {}
        for pitch, live in reversed(notes):
            if g < 0:
                break
            ev = self._ev(g)
            if ev.has(pitch) and not (consumed >> pitch) & 1:
                consumed |= 1 << pitch
                matches += 1
                group_first[g] = live
                if g == cand:
                    mask_cand |= 1 << pitch
                    enter_cand = live
                continue
            found = -1
            between = 0
            for gi in range(g - 1, max(-1, g - cfg.max_jump - 1), -1):
                if self._ev(gi).has(pitch):
                    found = gi
                    break
                between += self._ev(gi).size
            if found >= 0:
                skipped += (ev.size - bin(ev.mask & consumed).count("1")) + between
                g = found
                consumed = 1 << pitch
                matches += 1
                group_first[g] = live
                continue
            extras += 1
        score = matches * cfg.match_score - extras * cfg.extra_penalty - skipped * cfg.skip_penalty
        anchors = sorted(group_first.items())
        return score, matches, mask_cand, enter_cand, anchors

    # ------------------------------------------------------------- output
    def snapshot(self):
        hold = self.hold_tick() if self.started else self.anchor_tick
        return FollowerSnapshot(state=self.state, epoch=self.epoch, event_index=self.position,
                                anchor_tick=self.anchor_tick, anchor_live=self.anchor_live, hold_tick=hold,
                                rate=self.tempo.rate, confidence=self.confidence,
                                last_note_live=self.last_note_live, started=self.started)

    def position_ticks(self, now):
        """Estimated performer position (reference ticks) at ``now``."""
        if self.ref.empty:
            return 0.0
        if not self.started:
            return float(self.anchor_tick)
        pos = self.anchor_tick + max(0.0, now - self.anchor_live) * self.tempo.rate / self.spt
        return min(pos, self.hold_tick())

    @property
    def estimated_bpm(self):
        return self.tempo.bpm
