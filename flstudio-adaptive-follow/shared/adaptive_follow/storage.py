"""
Reference / configuration storage in the Adaptive Follow user data folder.

Default location (never inside FL Studio's own folders):

* Windows: ``%APPDATA%\\AdaptiveFollow``
* elsewhere (tests, simulator): ``~/.adaptive_follow``
* overridable with the ``ADAPTIVE_FOLLOW_HOME`` environment variable or an
  explicit path (the installer writes it into ``adaptive_follow_paths.py``).

Layout::

    <data>/config.json            settings (created with defaults if missing)
    <data>/references/<name>.json exported references
    <data>/last_reference.txt     name of the most recently exported reference
    <data>/logs/adaptive_follow.log (+ .1)
    <data>/reports/               diagnostic reports
    <data>/command.txt            one-shot commands for the device script

FL Studio's embedded Python may block ``open()`` in some folders. ``Storage.select``
probes candidate folders and writes to the first one that works; reads look in
all of them. When nothing is writable the scripts keep working in memory
(see ``handoff``) and every write reports ``StorageUnavailableError``.
"""

from . import compat
from .model import Config, DEFAULTS
from .reference import Reference, ReferenceFormatError

REFERENCES = "references"
LOGS = "logs"
REPORTS = "reports"
CONFIG_FILE = "config.json"
LAST_REFERENCE_FILE = "last_reference.txt"
COMMAND_FILE = "command.txt"
LOG_FILE = "adaptive_follow.log"

_WINDOWS_RESERVED = set(["CON", "PRN", "AUX", "NUL"] + ["COM%d" % i for i in range(1, 10)]
                        + ["LPT%d" % i for i in range(1, 10)])
_INVALID_CHARS = '<>:"/\\|?*'


class ReferenceMissingError(IOError):
    """No reference file found (nothing exported yet, or wrong name)."""


class StorageUnavailableError(IOError):
    """No folder can be written (e.g. FL Studio blocks file access)."""


def fl_settings_from_sys_path():
    """FL Studio's ``...\\FL Studio\\Settings`` folder, derived from the script folders
    FL Studio puts on ``sys.path`` (``Settings\\Hardware\\<script>``, ``Settings\\Piano roll scripts``)."""
    import sys
    for entry in sys.path:
        e = str(entry).replace("/", "\\").rstrip("\\")
        low = e.lower()
        for tail in ("\\hardware\\adaptivefollow", "\\piano roll scripts"):
            if low.endswith(tail) and low[:-len(tail)].endswith("\\settings"):
                return e[:-len(tail)]
    return ""


def candidate_data_dirs(preferred="", fl_settings=""):
    """Folders the FL Studio scripts may use for their data, in priority order.
    Both scripts compute the same list, so they agree on where references live."""
    out = []
    for d in (preferred, default_data_dir()):
        if d and d not in out:
            out.append(d)
    if fl_settings:
        norm = fl_settings.replace("/", "\\").rstrip("\\")
        image_line = norm.rsplit("\\", 2)[0] if norm.count("\\") >= 2 else ""   # ...\\Image-Line
        for d in (compat.path_join(fl_settings, "Hardware", "AdaptiveFollow", "data"),
                  compat.path_join(image_line, "AdaptiveFollow") if image_line else ""):
            if not d:
                continue
            if d not in out:
                out.append(d)
    return out


def default_data_dir():
    explicit = compat.env("ADAPTIVE_FOLLOW_HOME")
    if explicit:
        return explicit
    appdata = compat.env("APPDATA")
    if appdata:
        return compat.path_join(appdata, "AdaptiveFollow")
    home = compat.env("USERPROFILE") or compat.env("HOME")
    if home:
        return compat.path_join(home, ".adaptive_follow")
    return "AdaptiveFollowData"


class Storage(object):
    def __init__(self, data_dir=None, search_dirs=None):
        self.data_dir = data_dir or default_data_dir()
        self.search_dirs = [self.data_dir] + [d for d in (search_dirs or []) if d and d != self.data_dir]
        self.writable = True
        self.probes = []            # [(folder, "write OK, read OK"), ...]

    @classmethod
    def select(cls, candidates):
        """Storage in the first candidate folder where FL Studio lets us write."""
        candidates = [c for c in candidates if c] or [default_data_dir()]
        probes = []
        chosen = None
        for c in candidates:
            writable, _readable, text = compat.probe_dir(c)
            probes.append((c, text))
            if writable and chosen is None:
                chosen = c
                break
        st = cls(chosen or candidates[0], candidates)
        st.writable = chosen is not None
        st.probes = probes
        return st

    def probe_summary(self):
        lines = ["%s: %s" % (d, t) for d, t in self.probes]
        lines.append("data folder: %s (%s)" % (self.data_dir, "writable" if self.writable else "NOT writable"))
        return lines

    def _require_writable(self):
        if not self.writable:
            raise StorageUnavailableError(
                "FL Studio does not allow writing files in any data folder (%s)"
                % "; ".join("%s: %s" % (d, t) for d, t in self.probes))

    # ------------------------------------------------------------ paths
    def path(self, *parts):
        return compat.path_join(self.data_dir, *parts)

    def ensure_dirs(self):
        ok = compat.make_dirs(self.data_dir)
        for sub in (REFERENCES, LOGS, REPORTS):
            ok = compat.make_dirs(self.path(sub)) and ok
        return ok

    @property
    def log_path(self):
        return self.path(LOGS, LOG_FILE)

    # ------------------------------------------------------------ names
    @staticmethod
    def safe_name(name):
        """A file-name-safe version of ``name`` (Windows rules)."""
        name = (name or "").strip()
        out = []
        for ch in name:
            if ch in _INVALID_CHARS or ord(ch) < 32:
                out.append("_")
            else:
                out.append(ch)
        safe = "".join(out).strip(" .")
        if not safe:
            safe = "reference"
        if safe.split(".")[0].upper() in _WINDOWS_RESERVED:
            safe = "_" + safe
        return safe[:80]

    def reference_path(self, name):
        return self.path(REFERENCES, self.safe_name(name) + ".json")

    # ------------------------------------------------------------ references
    def save_reference(self, reference):
        self._require_writable()
        self.ensure_dirs()
        path = self.reference_path(reference.name)
        try:
            compat.write_text_atomic(path, reference.to_json())
        except Exception as exc:
            raise StorageUnavailableError("cannot write %s: %r" % (path, exc))
        try:
            compat.write_text_atomic(self.path(LAST_REFERENCE_FILE), self.safe_name(reference.name))
        except Exception:
            pass
        return path

    def _refs_dir(self, base):
        return compat.path_join(base, REFERENCES)

    def list_references(self):
        """Reference names (all data folders), most recently modified first."""
        found = {}
        for base in self.search_dirs:
            folder = self._refs_dir(base)
            for f in compat.list_dir(folder):
                if f.lower().endswith(".json") and f[:-5] not in found:
                    found[f[:-5]] = compat.file_mtime(compat.path_join(folder, f))
        return sorted(found, key=lambda n: -found[n])

    def last_reference_name(self):
        for base in self.search_dirs:
            p = compat.path_join(base, LAST_REFERENCE_FILE)
            if compat.file_exists(p):
                try:
                    return compat.read_text(p).strip()
                except Exception:
                    continue
        return ""

    def load_reference(self, name=""):
        """Load ``name`` (or the last exported / newest reference) from any data folder.
        Raises ReferenceMissingError or ReferenceFormatError with a clear message."""
        chosen = name or self.last_reference_name()
        if not chosen:
            names = self.list_references()
            if not names:
                raise ReferenceMissingError(
                    "no reference found in %s - export one from the Piano Roll first "
                    "(Tools > Scripting > Adaptive Follow - Export Reference)"
                    % " / ".join(self._refs_dir(b) for b in self.search_dirs))
            chosen = names[0]
        fname = self.safe_name(chosen) + ".json"
        paths = [compat.path_join(self._refs_dir(b), fname) for b in self.search_dirs]
        existing = [p for p in paths if compat.file_exists(p)]
        if not existing:
            raise ReferenceMissingError("reference %r not found (%s)" % (chosen, " / ".join(paths)))
        existing.sort(key=lambda p: -compat.file_mtime(p))
        errors = []
        for path in existing:
            try:
                text = compat.read_text(path)
            except Exception as exc:
                errors.append("cannot read %s: %r" % (path, exc))
                continue
            try:
                ref = Reference.from_json(text)
            except ReferenceFormatError as exc:
                raise ReferenceFormatError("%s: %s" % (path, exc))
            return ref, path
        raise ReferenceMissingError("; ".join(errors))

    # ------------------------------------------------------------ config
    def _config_path(self):
        for base in self.search_dirs:
            p = compat.path_join(base, CONFIG_FILE)
            if compat.file_exists(p):
                return p
        return self.path(CONFIG_FILE)

    def load_config(self, create_if_missing=True):
        path = self._config_path()
        if not compat.file_exists(path):
            cfg = Config.from_dict({})
            if create_if_missing:
                self.write_default_config()
            return cfg
        try:
            data = compat.json_loads(compat.read_text(path))
        except ValueError as exc:
            cfg = Config.from_dict({})
            cfg.warnings.append("config.json is corrupted (%s); defaults used" % exc)
            return cfg
        except Exception as exc:
            cfg = Config.from_dict({})
            cfg.warnings.append("cannot read %s (%r); defaults used" % (path, exc))
            return cfg
        return Config.from_dict(data)

    def write_default_config(self, overwrite=False):
        """Create config.json with defaults. Returns the path, or "" if files cannot be written."""
        path = self.path(CONFIG_FILE)
        if compat.file_exists(path) and not overwrite:
            return path
        if not self.writable:
            return ""
        self.ensure_dirs()
        data = {"_comment": "Adaptive Follow settings - see README.md. Edit while FL Studio follow is disarmed."}
        data.update(DEFAULTS)
        try:
            compat.write_text_atomic(path, compat.json_dumps(data, indent=2))
        except Exception:
            return ""
        return path

    def save_config(self, config):
        """Save settings. Returns False if files cannot be written (settings stay in memory)."""
        data = {"_comment": "Adaptive Follow settings - see README.md."}
        data.update(config.to_dict())
        if not self.writable:
            return False
        self.ensure_dirs()
        try:
            compat.write_text_atomic(self.path(CONFIG_FILE), compat.json_dumps(data, indent=2))
        except Exception:
            return False
        return True

    def config_mtime(self):
        return compat.file_mtime(self._config_path())

    # ------------------------------------------------------------ commands
    def write_command(self, command):
        self.ensure_dirs()
        compat.write_text_atomic(self.path(COMMAND_FILE), command.strip() + "\n")

    def take_command(self):
        """Read and delete a pending command ('' if none)."""
        p = self.path(COMMAND_FILE)
        if not compat.file_exists(p):
            return ""
        try:
            text = compat.read_text(p).strip()
        except Exception:
            return ""                   # unreadable (blocked): ignore, never loop on it
        compat.remove_file(p)
        return text.splitlines()[0].strip() if text else ""

    # ------------------------------------------------------------ reports
    def write_report(self, text, stamp="latest"):
        self._require_writable()
        self.ensure_dirs()
        path = self.path(REPORTS, "report-%s.txt" % self.safe_name(stamp))
        compat.write_text_atomic(path, text)
        return path
