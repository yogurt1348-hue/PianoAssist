"""
Performer tempo estimation, as a *rate* relative to the project tempo
(1.0 = playing at the project tempo).

Python adaptation of Cadenza's ``Source/Adaptive/TempoEstimator`` (C++):
recency-weighted least squares over the last anchors, pause segmentation,
outlier rejection unless persistent, EMA smoothing, BPM limits, slew limit.
"""


class TempoEstimator(object):
    def __init__(self, min_bpm=40.0, max_bpm=240.0, smoothing=0.5, max_change_bpm_per_s=25.0,
                 regression_window=4, min_span_s=0.2, pause_factor=2.5, pause_min_s=1.0,
                 outlier_ratio=1.6, outliers_to_accept=3, base_bpm=120.0):
        self.min_bpm = float(min_bpm)
        self.max_bpm = max(float(max_bpm), self.min_bpm)
        self.smoothing = min(1.0, max(0.01, float(smoothing)))
        self.max_change_bpm_per_s = float(max_change_bpm_per_s)
        self.regression_window = max(2, min(16, int(regression_window)))
        self.min_span_s = float(min_span_s)
        self.pause_factor = float(pause_factor)
        self.pause_min_s = float(pause_min_s)
        self.outlier_ratio = max(1.0, float(outlier_ratio))
        self.outliers_to_accept = max(1, int(outliers_to_accept))
        self.base_bpm = float(base_bpm) if base_bpm > 0 else 120.0
        self.reset()

    # ----------------------------------------------------------------- state
    def reset(self, initial_rate=1.0):
        self._anchors = []          # (nominal_s, live_s) of the current segment
        self.rate = initial_rate if initial_rate > 0 else 1.0
        self.last_measured = self.rate
        self._last_update_live = 0.0
        self.measurements = 0
        self._outlier_run = 0

    def break_segment(self):
        """Keep the rate; start a new segment (pause, seek, re-acquisition)."""
        self._anchors = []
        self._outlier_run = 0

    def clamp_rate(self, rate):
        return min(self.max_bpm / self.base_bpm, max(self.min_bpm / self.base_bpm, rate))

    @property
    def bpm(self):
        return self.rate * self.base_bpm

    # ----------------------------------------------------------------- input
    def add_anchor(self, nominal_s, live_s):
        if self._anchors:
            last_nom, last_live = self._anchors[-1]
            nominal_gap = nominal_s - last_nom
            live_gap = live_s - last_live
            if nominal_gap <= 0.0 or live_gap <= 0.0:
                self.break_segment()
            else:
                # Before the tempo is known only a gap too long even for the
                # slowest allowed tempo counts as a pause.
                rate_for_gap = self.clamp_rate(0.0) if self.measurements == 0 else self.rate
                expected = nominal_gap / max(1e-6, rate_for_gap)
                if live_gap > self.pause_min_s and live_gap > self.pause_factor * expected:
                    self.break_segment()
        self._anchors.append((nominal_s, live_s))
        if len(self._anchors) > 16:
            self._anchors.pop(0)

        measured = self._measure()
        if measured is None:
            return
        measured = self.clamp_rate(measured)
        self.last_measured = measured

        acquiring = self.measurements < 2
        if not acquiring:
            k = self.outlier_ratio
            side = 1 if measured > self.rate * k else (-1 if measured < self.rate / k else 0)
            if side != 0:
                self._outlier_run = self._outlier_run + side if self._outlier_run * side > 0 else side
                if abs(self._outlier_run) < self.outliers_to_accept:
                    return
                self.rate = measured        # persistent: a genuine big change
                self._outlier_run = 0
                self._last_update_live = live_s
                self.measurements += 1
                return
            self._outlier_run = 0

        target = measured if acquiring else self.rate + self.smoothing * (measured - self.rate)
        if not acquiring and self.max_change_bpm_per_s > 0:
            dt = max(0.0, live_s - self._last_update_live)
            max_delta = self.max_change_bpm_per_s / max(1.0, self.base_bpm) * dt
            target = min(self.rate + max_delta, max(self.rate - max_delta, target))
        self.rate = self.clamp_rate(target)
        self._last_update_live = live_s
        self.measurements += 1

    def _measure(self):
        pts = self._anchors[-self.regression_window:]
        n = len(pts)
        if n < 2:
            return None
        ys = [p[0] for p in pts]
        if max(ys) - min(ys) < self.min_span_s:
            return None
        sw = sx = sy = 0.0
        for k, (y, x) in enumerate(pts):
            w = float(k + 1)            # newer anchors weigh more
            sw += w
            sx += w * x
            sy += w * y
        mx, my = sx / sw, sy / sw
        sxx = sxy = 0.0
        for k, (y, x) in enumerate(pts):
            w = float(k + 1)
            sxx += w * (x - mx) * (x - mx)
            sxy += w * (x - mx) * (y - my)
        if sxx <= 1e-12:
            return None
        slope = sxy / sxx
        if slope <= 0 or slope != slope:
            return None
        return slope
