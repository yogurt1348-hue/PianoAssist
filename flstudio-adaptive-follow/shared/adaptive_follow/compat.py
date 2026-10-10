"""
Compatibility layer for FL Studio's embedded Python.

FL Studio ships its own Python interpreter. Depending on the FL Studio version,
parts of the standard library (pure-Python modules such as ``json`` or ``os``)
may be missing. Everything in ``adaptive_follow`` imports optional modules only
through this file, which provides small, tested fallbacks:

* ``json_dumps`` / ``json_loads`` - stdlib ``json`` or a minimal codec,
* ``monotonic`` - ``time.perf_counter`` / ``time.monotonic`` / ``time.time``,
* ``path_join`` / ``file_exists`` / ``make_dirs`` / ``replace_file`` /
  ``list_dir`` / ``file_mtime`` / ``file_size`` - ``os`` or safe fallbacks,
* ``env`` - environment variables (empty when ``os`` is unavailable).

Python 3.6 syntax only (no annotations that need newer interpreters).
"""

try:  # pragma: no cover - availability depends on the interpreter
    import json as _json
except Exception:  # pragma: no cover
    _json = None

try:  # pragma: no cover
    import os as _os
except Exception:  # pragma: no cover
    _os = None

try:  # pragma: no cover
    import time as _time
except Exception:  # pragma: no cover
    _time = None

HAVE_JSON = _json is not None
HAVE_OS = _os is not None
HAVE_TIME = _time is not None


# --------------------------------------------------------------------- time

def monotonic():
    """Seconds from an arbitrary origin, never decreasing (best available clock)."""
    if _time is not None:
        for name in ("perf_counter", "monotonic", "time"):
            fn = getattr(_time, name, None)
            if fn is not None:
                try:
                    return float(fn())
                except Exception:
                    continue
    return 0.0


def wall_clock_iso():
    """Local wall-clock time as 'YYYY-MM-DD HH:MM:SS' ('' if unavailable)."""
    if _time is None:
        return ""
    try:
        return _time.strftime("%Y-%m-%d %H:%M:%S")
    except Exception:
        return ""


# --------------------------------------------------------------------- json

class JsonError(ValueError):
    """Raised for malformed JSON (by both the stdlib and fallback codecs)."""


def json_dumps(value, indent=2):
    if _json is not None:
        return _json.dumps(value, indent=indent, sort_keys=False, ensure_ascii=False)
    return _MiniJson.dumps(value, indent, 0)


def json_loads(text):
    if _json is not None:
        try:
            return _json.loads(text)
        except ValueError as exc:
            raise JsonError(str(exc))
    return _MiniJson.loads(text)


class _MiniJson(object):
    """Minimal JSON codec (objects, arrays, strings, numbers, true/false/null)."""

    _ESC = {'"': '"', "\\": "\\", "/": "/", "b": "\b", "f": "\f", "n": "\n", "r": "\r", "t": "\t"}

    @staticmethod
    def _quote(s):
        out = ['"']
        for ch in s:
            if ch == '"':
                out.append('\\"')
            elif ch == "\\":
                out.append("\\\\")
            elif ch == "\n":
                out.append("\\n")
            elif ch == "\r":
                out.append("\\r")
            elif ch == "\t":
                out.append("\\t")
            elif ord(ch) < 0x20:
                out.append("\\u%04x" % ord(ch))
            else:
                out.append(ch)
        out.append('"')
        return "".join(out)

    @classmethod
    def dumps(cls, value, indent, level):
        pad = "" if not indent else "\n" + " " * (indent * (level + 1))
        end = "" if not indent else "\n" + " " * (indent * level)
        sep = "," if not indent else ","
        if value is None:
            return "null"
        if value is True:
            return "true"
        if value is False:
            return "false"
        if isinstance(value, (int,)):
            return str(int(value))
        if isinstance(value, float):
            if value != value or value in (float("inf"), float("-inf")):
                return "null"
            return repr(value)
        if isinstance(value, str):
            return cls._quote(value)
        if isinstance(value, (list, tuple)):
            if not value:
                return "[]"
            return "[" + sep.join(pad + cls.dumps(v, indent, level + 1) for v in value) + end + "]"
        if isinstance(value, dict):
            if not value:
                return "{}"
            items = []
            for k, v in value.items():
                items.append(pad + cls._quote(str(k)) + ": " + cls.dumps(v, indent, level + 1))
            return "{" + sep.join(items) + end + "}"
        raise JsonError("cannot serialise %r" % (type(value),))

    @classmethod
    def loads(cls, text):
        parser = _MiniJsonParser(text)
        value = parser.value()
        parser.ws()
        if parser.i != len(text):
            raise JsonError("extra data at offset %d" % parser.i)
        return value


class _MiniJsonParser(object):
    def __init__(self, text):
        if not isinstance(text, str):
            raise JsonError("JSON input must be text")
        self.s = text
        self.i = 0

    def ws(self):
        s, i = self.s, self.i
        while i < len(s) and s[i] in " \t\r\n":
            i += 1
        self.i = i

    def fail(self, msg):
        raise JsonError("%s at offset %d" % (msg, self.i))

    def value(self):
        self.ws()
        if self.i >= len(self.s):
            self.fail("unexpected end of input")
        ch = self.s[self.i]
        if ch == "{":
            return self.obj()
        if ch == "[":
            return self.arr()
        if ch == '"':
            return self.string()
        if self.s.startswith("true", self.i):
            self.i += 4
            return True
        if self.s.startswith("false", self.i):
            self.i += 5
            return False
        if self.s.startswith("null", self.i):
            self.i += 4
            return None
        return self.number()

    def obj(self):
        self.i += 1
        out = {}
        self.ws()
        if self.i < len(self.s) and self.s[self.i] == "}":
            self.i += 1
            return out
        while True:
            self.ws()
            if self.i >= len(self.s) or self.s[self.i] != '"':
                self.fail("expected string key")
            key = self.string()
            self.ws()
            if self.i >= len(self.s) or self.s[self.i] != ":":
                self.fail("expected ':'")
            self.i += 1
            out[key] = self.value()
            self.ws()
            if self.i >= len(self.s):
                self.fail("unterminated object")
            if self.s[self.i] == ",":
                self.i += 1
                continue
            if self.s[self.i] == "}":
                self.i += 1
                return out
            self.fail("expected ',' or '}'")

    def arr(self):
        self.i += 1
        out = []
        self.ws()
        if self.i < len(self.s) and self.s[self.i] == "]":
            self.i += 1
            return out
        while True:
            out.append(self.value())
            self.ws()
            if self.i >= len(self.s):
                self.fail("unterminated array")
            if self.s[self.i] == ",":
                self.i += 1
                continue
            if self.s[self.i] == "]":
                self.i += 1
                return out
            self.fail("expected ',' or ']'")

    def string(self):
        self.i += 1
        out = []
        s = self.s
        while True:
            if self.i >= len(s):
                self.fail("unterminated string")
            ch = s[self.i]
            if ch == '"':
                self.i += 1
                return "".join(out)
            if ch == "\\":
                self.i += 1
                if self.i >= len(s):
                    self.fail("bad escape")
                esc = s[self.i]
                if esc == "u":
                    hexpart = s[self.i + 1:self.i + 5]
                    if len(hexpart) != 4:
                        self.fail("bad \\u escape")
                    try:
                        out.append(chr(int(hexpart, 16)))
                    except ValueError:
                        self.fail("bad \\u escape")
                    self.i += 5
                    continue
                if esc not in _MiniJson._ESC:
                    self.fail("bad escape")
                out.append(_MiniJson._ESC[esc])
                self.i += 1
                continue
            out.append(ch)
            self.i += 1

    def number(self):
        s = self.s
        start = self.i
        if self.i < len(s) and s[self.i] == "-":
            self.i += 1
        while self.i < len(s) and s[self.i] in "0123456789.eE+-":
            self.i += 1
        token = s[start:self.i]
        if not token or token == "-":
            self.fail("unexpected character")
        try:
            if any(c in token for c in ".eE"):
                return float(token)
            return int(token)
        except ValueError:
            self.fail("bad number %r" % token)


# --------------------------------------------------------------------- files

def env(name, default=""):
    if _os is None:
        return default
    try:
        return _os.environ.get(name, default)
    except Exception:
        return default


def path_join(*parts):
    if _os is not None:
        return _os.path.join(*parts)
    sep = "\\" if any("\\" in p for p in parts) else "/"
    out = ""
    for p in parts:
        if not p:
            continue
        if out and not out.endswith(("/", "\\")):
            out += sep
        out += p
    return out


def file_exists(path):
    if _os is not None:
        return _os.path.isfile(path)
    try:
        with open(path, "rb"):
            return True
    except Exception:
        return False


def dir_exists(path):
    if _os is not None:
        return _os.path.isdir(path)
    return False


def make_dirs(path):
    if _os is None:
        return False
    try:
        _os.makedirs(path, exist_ok=True)
        return True
    except Exception:
        return False


def list_dir(path):
    if _os is None:
        return []
    try:
        return sorted(_os.listdir(path))
    except Exception:
        return []


def file_mtime(path):
    if _os is None:
        return 0.0
    try:
        return float(_os.path.getmtime(path))
    except Exception:
        return 0.0


def file_size(path):
    if _os is None:
        return 0
    try:
        return int(_os.path.getsize(path))
    except Exception:
        return 0


def remove_file(path):
    if _os is None:
        return False
    try:
        _os.remove(path)
        return True
    except Exception:
        return False


def replace_file(src, dst):
    """Atomically move ``src`` over ``dst`` when possible."""
    if _os is not None:
        try:
            _os.replace(src, dst)
            return True
        except Exception:
            pass
    # Fallback: copy content.
    try:
        with open(src, "rb") as f:
            data = f.read()
        with open(dst, "wb") as f:
            f.write(data)
        remove_file(src)
        return True
    except Exception:
        return False


def read_text(path):
    with open(path, "r", encoding="utf-8-sig") as f:
        return f.read()


def write_text_atomic(path, text):
    """Write ``text`` to ``path`` through a temporary file (never a half file)."""
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        f.write(text)
    if not replace_file(tmp, path):
        raise IOError("cannot replace %s" % path)


def append_text(path, text):
    with open(path, "a", encoding="utf-8") as f:
        f.write(text)


def probe_dir(path):
    """Which file operations work in ``path``? Returns (writable, readable, text).

    FL Studio's embedded Python can block ``open()`` (SystemError from
    ``_io.FileIO``); this tells the scripts where they may keep their data.
    """
    notes = []
    made = make_dirs(path)
    if not made and not dir_exists(path):
        notes.append("mkdir failed")
    probe = path_join(path, ".af_probe.tmp")
    writable = readable = False
    try:
        with open(probe, "w", encoding="utf-8") as f:
            f.write("ok")
        writable = True
    except Exception as exc:
        notes.append("write: %r" % (exc,))
    if writable:
        try:
            with open(probe, "r", encoding="utf-8") as f:
                readable = f.read() == "ok"
        except Exception as exc:
            notes.append("read: %r" % (exc,))
        remove_file(probe)
    else:
        # can existing files be read?
        for name in list_dir(path):
            p = path_join(path, name)
            if file_exists(p):
                try:
                    with open(p, "rb") as f:
                        f.read(1)
                    readable = True
                except Exception as exc:
                    notes.append("read: %r" % (exc,))
                break
    text = "write %s, read %s" % ("OK" if writable else "BLOCKED",
                                 "OK" if readable else ("BLOCKED" if notes and not writable else "?"))
    if notes:
        text += " (" + "; ".join(notes) + ")"
    return writable, readable, text
