"""
Diagnostics: levelled, size-limited log + in-memory ring buffer + report.

Levels: ``off`` < ``error`` < ``info`` < ``debug``.

* ``info``  - state transitions, arm/disarm, warnings, applied transport actions.
* ``debug`` - additionally one line per live note and throttled status lines
              (MIDI event, matched reference event, confidence, state, BPM,
              song tick, target tick, speed, tempo).

The file is capped at ``max_kb``; when exceeded it is rotated to ``.1``
(one backup), so logs never grow without bound. Nothing is printed to FL
Studio's Script output except through the optional ``echo`` callable.
"""

from . import compat

LEVELS = {"off": 0, "error": 1, "info": 2, "debug": 3}


class Diagnostics(object):
    def __init__(self, path=None, level="info", max_kb=512, echo=None, ring_size=400):
        self.path = path
        self.level_name = level if level in LEVELS else "info"
        self.level = LEVELS[self.level_name]
        self.max_bytes = int(max_kb) * 1024
        self.echo = echo                # e.g. print, to mirror lines in Script output
        self.ring = []
        self.ring_size = ring_size
        self.counts = {"error": 0, "warning": 0, "info": 0, "debug": 0}
        self._file_ok = True
        self._last_status = -1e9

    def set_level(self, level):
        if level in LEVELS:
            self.level_name = level
            self.level = LEVELS[level]

    # ----------------------------------------------------------------- core
    def _emit(self, tag, msg, now=None):
        stamp = compat.wall_clock_iso()
        t = "" if now is None else " t=%.3f" % now
        line = "%s [%s]%s %s" % (stamp, tag, t, msg)
        self.ring.append(line)
        if len(self.ring) > self.ring_size:
            self.ring.pop(0)
        if self.echo is not None:
            try:
                self.echo(line)
            except Exception:
                pass
        if self.path and self._file_ok:
            try:
                if self.max_bytes > 0 and compat.file_size(self.path) > self.max_bytes:
                    backup = self.path + ".1"
                    compat.remove_file(backup)
                    compat.replace_file(self.path, backup)
                compat.append_text(self.path, line + "\n")
            except Exception:
                self._file_ok = False     # read-only folder etc.: keep running, memory log only
        return line

    def error(self, msg, now=None):
        self.counts["error"] += 1
        if self.level >= 1:
            self._emit("ERROR", msg, now)

    def warning(self, msg, now=None):
        self.counts["warning"] += 1
        if self.level >= 1:
            self._emit("WARN", msg, now)

    def info(self, msg, now=None):
        self.counts["info"] += 1
        if self.level >= 2:
            self._emit("INFO", msg, now)

    def debug(self, msg, now=None):
        self.counts["debug"] += 1
        if self.level >= 3:
            self._emit("DEBUG", msg, now)

    @property
    def debug_enabled(self):
        return self.level >= 3

    def status(self, now, msg, every_s=0.5):
        """Throttled debug status line (at most one per ``every_s``)."""
        if self.level < 3 or now - self._last_status < every_s:
            return
        self._last_status = now
        self._emit("STATUS", msg, now)

    # ----------------------------------------------------------------- report
    def report(self, header_lines=()):
        lines = ["Adaptive Follow diagnostic report", "=" * 34]
        lines.extend(header_lines)
        lines.append("log level: %s, log file: %s" % (self.level_name, self.path or "(memory only)"))
        lines.append("counts: %s" % ", ".join("%s=%d" % kv for kv in sorted(self.counts.items())))
        lines.append("")
        lines.append("--- last %d log lines ---" % len(self.ring))
        lines.extend(self.ring)
        return "\n".join(lines)
