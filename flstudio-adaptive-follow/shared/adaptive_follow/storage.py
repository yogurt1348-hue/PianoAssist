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
    def __init__(self, data_dir=None):
        self.data_dir = data_dir or default_data_dir()

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
        self.ensure_dirs()
        path = self.reference_path(reference.name)
        compat.write_text_atomic(path, reference.to_json())
        try:
            compat.write_text_atomic(self.path(LAST_REFERENCE_FILE), self.safe_name(reference.name))
        except Exception:
            pass
        return path

    def list_references(self):
        """Reference names, most recently modified first."""
        folder = self.path(REFERENCES)
        names = [f[:-5] for f in compat.list_dir(folder) if f.lower().endswith(".json")]
        names.sort(key=lambda n: -compat.file_mtime(compat.path_join(folder, n + ".json")))
        return names

    def last_reference_name(self):
        p = self.path(LAST_REFERENCE_FILE)
        if compat.file_exists(p):
            try:
                return compat.read_text(p).strip()
            except Exception:
                return ""
        return ""

    def load_reference(self, name=""):
        """Load ``name`` (or the last exported / newest reference).
        Raises ReferenceMissingError or ReferenceFormatError with a clear message."""
        chosen = name or self.last_reference_name()
        if not chosen:
            names = self.list_references()
            if not names:
                raise ReferenceMissingError(
                    "no reference found in %s - export one from the Piano Roll first "
                    "(Tools > Scripting > Adaptive Follow - Export Reference)" % self.path(REFERENCES))
            chosen = names[0]
        path = self.reference_path(chosen)
        if not compat.file_exists(path):
            raise ReferenceMissingError("reference %r not found (%s)" % (chosen, path))
        try:
            text = compat.read_text(path)
        except Exception as exc:
            raise ReferenceMissingError("cannot read %s: %s" % (path, exc))
        try:
            ref = Reference.from_json(text)
        except ReferenceFormatError as exc:
            raise ReferenceFormatError("%s: %s" % (path, exc))
        return ref, path

    # ------------------------------------------------------------ config
    def load_config(self, create_if_missing=True):
        path = self.path(CONFIG_FILE)
        if not compat.file_exists(path):
            cfg = Config.from_dict({})
            if create_if_missing:
                self.write_default_config()
            return cfg
        try:
            data = compat.json_loads(compat.read_text(path))
        except Exception as exc:
            cfg = Config.from_dict({})
            cfg.warnings.append("config.json is corrupted (%s); defaults used" % exc)
            return cfg
        return Config.from_dict(data)

    def write_default_config(self, overwrite=False):
        path = self.path(CONFIG_FILE)
        if compat.file_exists(path) and not overwrite:
            return path
        self.ensure_dirs()
        data = {"_comment": "Adaptive Follow settings - see README.md. Edit while FL Studio follow is disarmed."}
        data.update(DEFAULTS)
        compat.write_text_atomic(path, compat.json_dumps(data, indent=2))
        return path

    def save_config(self, config):
        data = {"_comment": "Adaptive Follow settings - see README.md."}
        data.update(config.to_dict())
        self.ensure_dirs()
        compat.write_text_atomic(self.path(CONFIG_FILE), compat.json_dumps(data, indent=2))

    def config_mtime(self):
        return compat.file_mtime(self.path(CONFIG_FILE))

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
            text = ""
        compat.remove_file(p)
        return text.splitlines()[0].strip() if text else ""

    # ------------------------------------------------------------ reports
    def write_report(self, text, stamp="latest"):
        self.ensure_dirs()
        path = self.path(REPORTS, "report-%s.txt" % self.safe_name(stamp))
        compat.write_text_atomic(path, text)
        return path
