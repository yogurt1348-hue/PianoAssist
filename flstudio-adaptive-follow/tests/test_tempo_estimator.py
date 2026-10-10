from adaptive_follow.tempo_estimator import TempoEstimator


def feed(est, pairs):
    for nom, live in pairs:
        est.add_anchor(nom, live)


def test_exact_tempo():
    est = TempoEstimator(base_bpm=120)
    feed(est, [(i * 0.5, i * 0.5) for i in range(8)])
    assert abs(est.rate - 1.0) < 1e-9 and abs(est.bpm - 120) < 1e-6


def test_slow_starter_is_acquired_quickly():
    est = TempoEstimator(base_bpm=120, min_bpm=40)
    feed(est, [(i * 0.5, i * 1.0) for i in range(4)])      # 60 BPM from the first notes
    assert abs(est.bpm - 60.0) < 1.0


def test_limits_clamp_bpm():
    est = TempoEstimator(base_bpm=120, min_bpm=60, max_bpm=180)
    feed(est, [(i * 0.5, i * 0.1) for i in range(6)])      # 600 BPM
    assert est.bpm <= 180 + 1e-6
    est2 = TempoEstimator(base_bpm=120, min_bpm=60, max_bpm=180)
    feed(est2, [(i * 0.5, i * 5.0) for i in range(3)])     # 12 BPM
    assert est2.bpm >= 60 - 1e-6


def test_slew_limit_and_smoothing():
    est = TempoEstimator(base_bpm=120, max_change_bpm_per_s=10)
    feed(est, [(i * 0.5, i * 0.5) for i in range(6)])
    feed(est, [(3.0 + i * 0.5, 2.5 + 0.42 * (i + 1)) for i in range(2)])   # sudden ~1.2x
    assert est.rate <= 1.0 + 10.0 / 120.0 * 1.0 + 1e-9


def test_pause_starts_new_segment():
    est = TempoEstimator(base_bpm=120)
    feed(est, [(i * 0.5, i * 0.5) for i in range(6)])
    est.add_anchor(3.0, 2.5 + 8.0)       # 8 s pause
    est.add_anchor(3.5, 11.0)
    assert abs(est.rate - 1.0) < 0.02


def test_single_outlier_is_ignored_persistent_change_accepted():
    est = TempoEstimator(base_bpm=120)
    feed(est, [(i * 0.5, i * 0.5) for i in range(8)])
    est.add_anchor(4.0, 3.5 + 0.1)        # one very early anchor (mismatch)
    assert abs(est.rate - 1.0) < 0.15
    est2 = TempoEstimator(base_bpm=120, max_change_bpm_per_s=1000)
    feed(est2, [(i * 0.5, i * 0.5) for i in range(8)])
    t = 3.5
    for i in range(8):                     # genuinely twice as fast from now on
        t += 0.25
        est2.add_anchor(4.0 + i * 0.5, t)
    assert est2.rate > 1.6


def test_reset_and_break_segment():
    est = TempoEstimator(base_bpm=100)
    feed(est, [(i * 0.6, i * 0.75) for i in range(6)])
    assert abs(est.rate - 0.8) < 0.02
    est.break_segment()
    assert abs(est.rate - 0.8) < 0.02 and est._anchors == []
    est.reset(1.0)
    assert est.rate == 1.0 and est.measurements == 0
