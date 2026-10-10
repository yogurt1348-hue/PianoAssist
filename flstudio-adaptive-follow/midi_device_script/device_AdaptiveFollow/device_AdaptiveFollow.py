# name=AdaptiveFollow
"""
Adaptive Follow - FL Studio MIDI device script (MVP).

Assign this script ("AdaptiveFollow") to your MIDI keyboard's input in
FL Studio's MIDI settings. Your notes keep playing the selected instrument
exactly as before (this script never marks notes as handled); in parallel
they are compared with the exported reference part and FL Studio's transport
follows you.

Official MIDI Controller Scripting API used (FL Studio API stubs, IL-Group):
  callbacks : OnInit, OnDeInit, OnMidiMsg, OnIdle, OnUpdateBeatIndicator, OnProjectLoad
  transport : isPlaying, start (play/pause), getSongPos(SONGLENGTH_ABSTICKS),
              setSongPos(ticks, SONGLENGTH_ABSTICKS) [API >= 4], setPlaybackSpeed
  mixer     : getCurrentTempo
  general   : getVersion, getRecPPQ [API >= 8],
              processRECEvent(REC_Tempo, bpm*1000, REC_Control|REC_UpdateControl)
  ui        : setHintMsg
Every call is wrapped; unavailable features are disabled with a log message.

Console commands (View > Script output, select this device, type):
  af_help()  af_status()  af_arm()  af_disarm()  af_toggle()
  af_dry_run(True/False)  af_reload()  af_report()  af_set("key", value)  af_probe()
"""

import general
import midi
import mixer
import transport
import ui

_state = {"session": None, "storage": None, "error": "", "next_poll": 0.0,
          "idle_calls": 0, "steps": 0, "last_step": -1.0, "step_sources": {}}  # type: dict


# ------------------------------------------------------------------ bootstrap

def _env(name):
    """Environment variable without depending on a full standard library."""
    for mod in ("os", "nt"):
        try:
            return str(__import__(mod).environ.get(name, "") or "")
        except Exception:
            continue
    return ""


def _file_exists(path):
    try:
        with open(path, "rb"):
            return True
    except Exception:
        return False


def _bootstrap():
    """Import the shared ``adaptive_follow`` package. Returns (data_dir, problem).

    Search order:
      1. this script's own folder: FL Studio adds it to the module search path
         (official docs), and the installer puts a copy of adaptive_follow there;
      2. LIB_DIR from adaptive_follow_paths.py (written next to this script by the installer);
      3. %APPDATA%\\AdaptiveFollow\\lib.
    ``problem`` is "" on success, otherwise a description of every attempt.
    """
    import sys
    notes = []
    data = ""
    lib = ""
    try:
        import adaptive_follow_paths as paths
        lib = str(getattr(paths, "LIB_DIR", "") or "")
        data = str(getattr(paths, "DATA_DIR", "") or "")
    except Exception as exc:
        notes.append("adaptive_follow_paths.py not loaded: %r" % (exc,))
    appdata = _env("APPDATA").rstrip("\\/")
    if appdata:
        data = data or appdata + "\\AdaptiveFollow"
    else:
        notes.append("APPDATA environment variable not available")
    candidates = [("script folder", "")]
    if lib:
        candidates.append(("LIB_DIR", lib))
    if appdata:
        candidates.append(("APPDATA", appdata + "\\AdaptiveFollow\\lib"))
    for why, folder in candidates:
        if folder and folder not in sys.path:
            sys.path.insert(0, folder)
        try:
            import importlib
            importlib.invalidate_caches()
        except Exception:
            pass
        try:
            import adaptive_follow  # noqa: F401
            return data, ""
        except ImportError as exc:
            where = (" %s (adaptive_follow\\__init__.py %s)"
                     % (folder, "found" if _file_exists(folder + "\\adaptive_follow\\__init__.py") else "MISSING")
                     if folder else "")
            notes.append("%s%s: %r" % (why, where, exc))
        except Exception as exc:
            notes.append("%s: the package is broken: %r" % (why, exc))
            break
    notes.append("Python %s; sys.path: %s" % (sys.version.split()[0], "; ".join(str(p) for p in sys.path[:6])))
    return data, "\n  ".join(notes)


def _api_version():
    try:
        return int(general.getVersion())
    except Exception:
        return 0


class FLTransportPort(object):
    """Adaptive Follow transport port implemented with the official FL Studio API."""

    def __init__(self, reference_ppq_fallback=96):
        self.api = _api_version()
        self.fallback_ppq = reference_ppq_fallback

    def is_playing(self):
        return bool(transport.isPlaying())

    def song_tick(self):
        return int(transport.getSongPos(2))      # 2 == midi.SONGLENGTH_ABSTICKS (absolute ticks)

    def project_ppq(self):
        if self.api >= 8:
            return int(general.getRecPPQ())
        return self.fallback_ppq        # older FL: the Piano Roll PPQ is the project PPQ

    def tempo(self):
        value = float(mixer.getCurrentTempo())
        # Some versions report tempo * 1000 (the REC_Tempo unit); normalise.
        if value > 1000.0:
            value = value / 1000.0
        return value

    def play_pause(self):
        transport.start()               # documented as play/pause

    def set_speed(self, multiplier):
        transport.setPlaybackSpeed(float(multiplier))

    def can_set_tempo(self):
        return hasattr(general, "processRECEvent") and hasattr(midi, "REC_Tempo")

    def set_tempo(self, bpm):
        general.processRECEvent(midi.REC_Tempo, int(round(float(bpm) * 1000)),
                                midi.REC_Control | midi.REC_UpdateControl)

    def can_seek(self):
        return self.api >= 4            # setSongPos(position, mode) since API 4

    def set_song_tick(self, ticks):
        transport.setSongPos(int(ticks), 2)      # 2 == midi.SONGLENGTH_ABSTICKS, API >= 4

    def hint(self, text):
        ui.setHintMsg(text)


def _start():
    data_dir, problem = _bootstrap()
    try:
        if problem:
            raise ImportError("adaptive_follow not found")
        from adaptive_follow import __version__
        from adaptive_follow.diagnostics import Diagnostics
        from adaptive_follow.session import AdaptiveFollowSession
        from adaptive_follow.storage import Storage, candidate_data_dirs, fl_settings_from_sys_path
    except Exception as exc:
        _state["error"] = ("cannot import the shared adaptive_follow module (%r).\n"
                           "Fix: re-run tools\\install_windows.ps1, or copy the folder shared\\adaptive_follow "
                           "into this script's folder (next to device_AdaptiveFollow.py) and click Reload script.\n"
                           "Details:\n  %s" % (exc, problem or "-"))
        print("Adaptive Follow ERROR: " + _state["error"])
        return
    # FL Studio may block open() in some folders: use the first folder where writing works.
    storage = Storage.select(candidate_data_dirs(data_dir, fl_settings_from_sys_path()))
    config = storage.load_config()
    memory_only = not storage.writable
    diag = Diagnostics(storage.log_path if not memory_only else None, config.log_level, config.log_max_kb,
                       echo=print if (memory_only or config.log_level == "debug") else None)
    if memory_only:
        print("Adaptive Follow: FL Studio does not allow this script to write files, so it works in memory:\n"
              "  settings = defaults (+ af_set, not saved), log = this window. Details: af_probe()")
    port = FLTransportPort()
    session = AdaptiveFollowSession(port, storage, diag, config)
    session.load_config()
    diag.info("Adaptive Follow %s started (MIDI scripting API %d, data %s)" % (__version__, port.api, storage.data_dir))
    if port.api < 4:
        diag.warning("API < 4: setSongPos(ticks) unavailable; seeking disabled")
    if port.api < 8:
        diag.warning("API < 8: general.getRecPPQ unavailable; the reference PPQ is assumed to be the project PPQ")
    if session.load_reference():
        port.fallback_ppq = session.reference.ppq
    _state["session"] = session
    _state["storage"] = storage
    print("Adaptive Follow %s ready. Reference: %s. Dry-run: %s. Type af_help() for commands."
          % (__version__, session.reference.name if session.reference else "NONE (convert a MIDI export with tools\\midi_to_reference.bat, then af_reload())",
             config.dry_run))
    if config.auto_arm and session.reference is not None:
        session.arm()


# ------------------------------------------------------------------ callbacks

def OnInit():
    try:
        _start()
    except Exception as exc:
        _state["error"] = repr(exc)
        print("Adaptive Follow failed to start: %r" % (exc,))


def OnDeInit():
    session = _state.get("session")
    if session is not None:
        try:
            session.shutdown()          # restores speed 1.0 / original tempo
        except Exception as exc:
            print("Adaptive Follow shutdown error: %r" % (exc,))


def OnProjectLoad(status):
    # A different project is being loaded: stop following, keep FL Studio normal.
    session = _state.get("session")
    if session is not None and status == getattr(midi, "PL_Start", 0):
        try:
            session.disarm("project load")
        except Exception:
            pass


def _drive(session, source):
    """Run one control step (speed/tempo/pause). Called from OnIdle and, in case FL Studio
    calls OnIdle rarely or not at all, also from OnMidiMsg and OnUpdateBeatIndicator."""
    now = session.clock()
    if now - _state["last_step"] < 0.01:
        return
    _state["last_step"] = now
    _state["steps"] += 1
    src = _state["step_sources"]
    src[source] = src.get(source, 0) + 1
    session.tick(now)


def OnMidiMsg(msg):
    session = _state.get("session")
    if session is None:
        return
    try:
        status = msg.status & 0xF0
        channel = (msg.status & 0x0F) + 1
        if status == midi.MIDI_NOTEON:
            session.on_note(msg.data1, msg.data2, channel, msg.data2 > 0)
        elif status == midi.MIDI_NOTEOFF:
            session.on_note(msg.data1, 0, channel, False)
        elif status == midi.MIDI_CONTROLCHANGE:
            if session.on_cc(msg.data1, msg.data2, channel):
                msg.handled = True      # only explicitly configured control CCs are consumed
        if session.sm.active:
            _drive(session, "OnMidiMsg")
    except Exception as exc:
        try:
            session.diag.error("OnMidiMsg error: %r" % (exc,))
        except Exception:
            pass
    # Notes are never marked handled: FL Studio plays them as usual.


def OnUpdateBeatIndicator(value):
    session = _state.get("session")
    if session is None or not session.sm.active:
        return
    try:
        _drive(session, "OnUpdateBeatIndicator")
    except Exception as exc:
        try:
            session.diag.error("OnUpdateBeatIndicator error: %r" % (exc,))
        except Exception:
            pass


def OnIdle():
    _state["idle_calls"] += 1
    session = _state.get("session")
    if session is None:
        return
    try:
        if session.sm.active:
            _drive(session, "OnIdle")
        now = session.clock()
        if now >= _state["next_poll"]:
            _state["next_poll"] = now + 0.5
            cmd = _state["storage"].take_command()
            if cmd:
                session.diag.info("command file: %s" % cmd)
                result = session.command(cmd)
                if result:
                    session.diag.info("command result: %s" % result)
            session.maybe_reload_config()
    except Exception as exc:
        try:
            session.diag.error("OnIdle error: %r" % (exc,))
        except Exception:
            pass


# ------------------------------------------------------------------ console helpers

def _s():
    session = _state.get("session")
    if session is None:
        print("Adaptive Follow is not running: %s" % (_state.get("error") or "unknown error"))
    return session


def af_help():
    print(__doc__)


def af_status():
    s = _s()
    if s:
        print(s.status_line())
        ago = s.clock() - _state["last_step"] if _state["last_step"] >= 0 else -1
        print("OnIdle calls: %d; control steps: %d %s; last step %s; playing=%s"
              % (_state["idle_calls"], _state["steps"], _state["step_sources"] or "",
                 ("%.2f s ago" % ago) if ago >= 0 else "never", s._safe(s.port.is_playing, "?")))


def af_arm():
    s = _s()
    if s:
        print("armed" if s.arm() else "arm failed (see af_report())")


def af_disarm():
    s = _s()
    if s:
        s.disarm("disarmed from console")
        print("disarmed; normal playback restored")


def af_toggle():
    s = _s()
    if s:
        s.toggle_arm()
        print(s.sm.state)


def af_dry_run(on=True):
    s = _s()
    if s:
        s.set_dry_run(bool(on))
        print("dry-run %s" % ("on" if on else "off"))


def af_reload():
    s = _s()
    if s:
        s.command("reload")
        print("reference: %s" % (s.reference.name if s.reference else "NONE"))


def af_report():
    s = _s()
    if s:
        print("report written to %s" % s.write_report())


def af_probe():
    """What this FL Studio allows: file access per data folder, in-memory hand-off."""
    import sys
    print("Python %s" % sys.version.split()[0])
    st = _state.get("storage")
    try:
        from adaptive_follow.storage import Storage, candidate_data_dirs, fl_settings_from_sys_path
        fl = fl_settings_from_sys_path()
        print("FL Settings folder: %s" % (fl or "not found on sys.path"))
        probe = Storage.select(candidate_data_dirs(st.data_dir if st else "", fl))
        for line in probe.probe_summary():
            print("  " + line)
        print("references found: %s" % (", ".join(probe.list_references()) or "none"))
    except Exception as exc:
        print("probe failed: %r (%s)" % (exc, _state.get("error") or ""))


def af_set(key, value):
    """Change one setting (saved to config.json). Allowed only while disarmed."""
    s = _s()
    if not s:
        return
    if s.sm.active:
        print("disarm first (af_disarm())")
        return
    try:
        cfg = s.config.copy()
        setattr(cfg, key, value)
        if s.storage.save_config(cfg):
            s.load_config()
            print("%s = %r" % (key, getattr(s.config, key)))
        else:
            s.config = cfg              # file access blocked: keep it for this FL session
            s.diag.set_level(cfg.log_level)
            print("%s = %r (in memory only: FL Studio blocked saving config.json)" % (key, getattr(cfg, key)))
    except Exception as exc:
        print("cannot set %s: %s" % (key, exc))
