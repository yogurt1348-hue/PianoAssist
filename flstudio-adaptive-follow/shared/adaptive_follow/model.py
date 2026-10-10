"""
Configuration model for Adaptive Follow.

Every behaviour is configurable; nothing is tied to a song, MIDI device or
user. ``Config.from_dict`` never raises on bad input: invalid values fall back
to defaults and are reported in ``Config.warnings`` so the device script can
log them instead of crashing inside FL Studio.
"""

STRATEGY_SPEED_ONLY = "speed_only"
STRATEGY_TEMPO_ONLY = "tempo_only"
STRATEGY_HYBRID = "hybrid"
STRATEGIES = (STRATEGY_SPEED_ONLY, STRATEGY_TEMPO_ONLY, STRATEGY_HYBRID)

WAIT_PAUSE = "pause"        # brake, then pause playback at the next expected event
WAIT_SLOW = "slow"          # brake to the minimum speed, never pause
WAIT_CONTINUE = "continue"  # keep the last speed (not recommended)
WAIT_BEHAVIOURS = (WAIT_PAUSE, WAIT_SLOW, WAIT_CONTINUE)

LOST_CONTINUE = "continue"  # glide back to normal speed and keep playing
LOST_PAUSE = "pause"        # pause playback until the performer is found again
LOST_BEHAVIOURS = (LOST_CONTINUE, LOST_PAUSE)

OFFSET_FIXED = "fixed"        # reference tick 0 == song/pattern tick `offset_ticks`
OFFSET_PLAYHEAD = "playhead"  # playhead position at Arm == first reference event
OFFSET_MODES = (OFFSET_FIXED, OFFSET_PLAYHEAD)

LOG_LEVELS = ("off", "error", "info", "debug")

# FL Studio's documented playback-speed range (transport.setPlaybackSpeed).
FL_SPEED_MIN = 0.25
FL_SPEED_MAX = 4.0

DEFAULTS = {
    # --- reference / input ---
    "reference_name": "",            # "" = most recently exported reference
    "midi_channel": 0,               # 0 = any, 1..16 = only that channel
    "min_velocity": 1,               # ignore softer note-ons (1..127)
    "chord_window_ms": None,         # None = use the window stored in the reference
    "timing_tolerance_ms": 180.0,    # absolute part of the timing tolerance
    # --- limits ---
    "tempo_min_bpm": 40.0,
    "tempo_max_bpm": 240.0,
    "speed_min": 0.75,
    "speed_max": 1.6,
    # --- control ---
    "transport_control": True,       # False = never touch transport (like dry_run)
    "tempo_control": False,          # must be True for tempo_only / hybrid to change tempo
    "follow_strategy": STRATEGY_SPEED_ONLY,
    "dry_run": True,                 # safe default: compute decisions, do not apply them
    "auto_arm": False,
    "start_on_first_note": True,     # start playback with the first confident match
    "wait_behavior": WAIT_SLOW,        # "stable": never auto-pause (see PROFILES)
    "lost_behavior": LOST_CONTINUE,
    "allow_seek": False,             # setSongPos only after a confident re-acquisition
    "seek_min_confidence": 0.75,
    "seek_threshold_beats": 2.0,
    "offset_mode": OFFSET_FIXED,
    "offset_ticks": 0,
    # --- loop tuning ---
    "phase_correction_time_s": 1.0,
    "max_phase_correction": 0.08,
    "catchup_max_correction": 0.15,   # when the backing is > 0.25 s behind (missed note, after a pause)
    "speed_slew_per_s": 0.8,
    "hold_tolerance_beats": 0.25,
    "pause_grace_s": 1.0,             # wait_behavior=pause: brake first, pause only if this late
    "reacquire_radius_beats": 8.0,    # without seek: look for a lost performer only this close to FL's playhead (0 = anywhere)
    "lost_after_misses": 8,           # consecutive unmatched notes before LOST
    "control_rate_hz": 25.0,          # at most this many transport control steps per second
    "tempo_smoothing_s": 2.0,         # hybrid/tempo: time constant of tempo changes
    "tempo_update_min_bpm": 0.5,      # do not send tempo changes smaller than this
    # --- optional MIDI controls (None = disabled; any CC number 0..127) ---
    "control_cc_arm_toggle": None,
    "control_cc_dry_run_toggle": None,
    "control_cc_reset": None,
    # --- diagnostics ---
    "log_level": "info",
    "log_max_kb": 512,
    "hint_messages": True,
}

# Named groups of settings (console: af_profile("stable") / af_profile("responsive")).
PROFILES = {
    # Default. No auto-pause, gentle corrections, tolerant timing, local re-acquisition.
    "stable": {"wait_behavior": WAIT_SLOW, "speed_min": 0.75, "timing_tolerance_ms": 180.0,
               "max_phase_correction": 0.08, "catchup_max_correction": 0.15, "pause_grace_s": 1.0,
               "lost_after_misses": 8, "reacquire_radius_beats": 8.0},
    # Follows closely and pauses at the next expected note when the performer stops.
    "responsive": {"wait_behavior": WAIT_PAUSE, "speed_min": 0.5, "timing_tolerance_ms": 100.0,
                   "max_phase_correction": 0.15, "catchup_max_correction": 0.35, "pause_grace_s": 0.3,
                   "lost_after_misses": 5, "reacquire_radius_beats": 8.0},
}

_NUMERIC_RANGES = {
    "midi_channel": (0, 16, int),
    "min_velocity": (1, 127, int),
    "timing_tolerance_ms": (5.0, 1000.0, float),
    "tempo_min_bpm": (10.0, 999.0, float),
    "tempo_max_bpm": (10.0, 999.0, float),
    "speed_min": (FL_SPEED_MIN, FL_SPEED_MAX, float),
    "speed_max": (FL_SPEED_MIN, FL_SPEED_MAX, float),
    "seek_min_confidence": (0.0, 1.0, float),
    "seek_threshold_beats": (0.25, 64.0, float),
    "offset_ticks": (-10 ** 9, 10 ** 9, int),
    "phase_correction_time_s": (0.1, 10.0, float),
    "max_phase_correction": (0.0, 1.0, float),
    "catchup_max_correction": (0.0, 1.0, float),
    "speed_slew_per_s": (0.05, 10.0, float),
    "hold_tolerance_beats": (0.0, 4.0, float),
    "pause_grace_s": (0.0, 10.0, float),
    "reacquire_radius_beats": (0.0, 1000.0, float),
    "lost_after_misses": (3, 50, int),
    "control_rate_hz": (2.0, 100.0, float),
    "tempo_smoothing_s": (0.1, 30.0, float),
    "tempo_update_min_bpm": (0.05, 20.0, float),
    "log_max_kb": (16, 65536, int),
}

_CHOICES = {
    "follow_strategy": STRATEGIES,
    "wait_behavior": WAIT_BEHAVIOURS,
    "lost_behavior": LOST_BEHAVIOURS,
    "offset_mode": OFFSET_MODES,
    "log_level": LOG_LEVELS,
}

_BOOLS = ("transport_control", "tempo_control", "dry_run", "auto_arm", "start_on_first_note",
          "allow_seek", "hint_messages")

_OPTIONAL_CC = ("control_cc_arm_toggle", "control_cc_dry_run_toggle", "control_cc_reset")


class Config(object):
    """Validated settings. Access values as attributes (``cfg.speed_min``)."""

    def __init__(self, values=None, warnings=None):
        self.__dict__["_values"] = dict(DEFAULTS)
        self.__dict__["warnings"] = list(warnings or [])
        if values:
            self._values.update(values)

    def __getattr__(self, name):
        values = self.__dict__.get("_values", {})
        if name in values:
            return values[name]
        raise AttributeError(name)

    def __setattr__(self, name, value):
        if name not in DEFAULTS:
            raise AttributeError("unknown setting %r" % name)
        checked, warning = _check(name, value)
        if warning:
            raise ValueError(warning)
        self._values[name] = checked

    def to_dict(self):
        return dict(self._values)

    def copy(self):
        return Config(dict(self._values), list(self.warnings))

    @classmethod
    def from_dict(cls, data):
        warnings = []
        values = dict(DEFAULTS)
        if data is None:
            data = {}
        if not isinstance(data, dict):
            return cls(values, ["configuration is not a JSON object; defaults used"])
        for key, raw in data.items():
            if key.startswith("_"):
                continue  # comments such as "_comment"
            if key not in DEFAULTS:
                warnings.append("unknown setting %r ignored" % key)
                continue
            checked, warning = _check(key, raw)
            if warning:
                warnings.append(warning + "; default %r used" % (DEFAULTS[key],))
                continue
            values[key] = checked
        # Cross-field consistency.
        if values["tempo_min_bpm"] > values["tempo_max_bpm"]:
            warnings.append("tempo_min_bpm > tempo_max_bpm; swapped")
            values["tempo_min_bpm"], values["tempo_max_bpm"] = values["tempo_max_bpm"], values["tempo_min_bpm"]
        if values["speed_min"] > values["speed_max"]:
            warnings.append("speed_min > speed_max; swapped")
            values["speed_min"], values["speed_max"] = values["speed_max"], values["speed_min"]
        if values["follow_strategy"] != STRATEGY_SPEED_ONLY and not values["tempo_control"]:
            warnings.append("follow_strategy %r changes the project tempo only when tempo_control is true; "
                            "using speed only" % values["follow_strategy"])
        return cls(values, warnings)

    @property
    def effective_strategy(self):
        """The strategy actually applied (tempo strategies need tempo_control)."""
        if not self.tempo_control:
            return STRATEGY_SPEED_ONLY
        return self.follow_strategy

    @property
    def controls_transport(self):
        return bool(self.transport_control) and not bool(self.dry_run)


def _check(key, value):
    """Return (checked_value, warning_or_None)."""
    if key in _BOOLS:
        if isinstance(value, bool):
            return value, None
        return None, "%s must be true or false" % key
    if key in _CHOICES:
        if isinstance(value, str) and value in _CHOICES[key]:
            return value, None
        return None, "%s must be one of %s" % (key, ", ".join(_CHOICES[key]))
    if key in _OPTIONAL_CC:
        if value is None:
            return None, None
        if isinstance(value, bool) or not isinstance(value, (int, float)) or not 0 <= int(value) <= 127:
            return None, "%s must be null or a CC number 0..127" % key
        return int(value), None
    if key == "reference_name":
        if isinstance(value, str):
            return value, None
        return None, "reference_name must be text"
    if key == "chord_window_ms":
        if value is None:
            return None, None
        if isinstance(value, bool) or not isinstance(value, (int, float)) or not 0 <= float(value) <= 500:
            return None, "chord_window_ms must be null or 0..500"
        return float(value), None
    if key in _NUMERIC_RANGES:
        lo, hi, typ = _NUMERIC_RANGES[key]
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            return None, "%s must be a number" % key
        if value != value or not lo <= value <= hi:
            return None, "%s must be within %s..%s" % (key, lo, hi)
        return typ(value), None
    return value, None
