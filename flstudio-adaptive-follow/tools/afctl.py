"""
Control a running Adaptive Follow session from outside FL Studio.

The MIDI device script reads %APPDATA%\\AdaptiveFollow\\command.txt every 0.5 s
(OnIdle) and writes its answers to logs\\adaptive_follow.log. This tool writes
that file and shows the answer; it also lists references and edits config.json.

    python tools/afctl.py status
    python tools/afctl.py arm | disarm | toggle | reload | report
    python tools/afctl.py dry-run on|off
    python tools/afctl.py list                  # exported references
    python tools/afctl.py config                # current settings
    python tools/afctl.py set midi_channel 2    # validated; applied while disarmed
    python tools/afctl.py log [N]               # last N log lines
"""

import argparse
import json
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))


def _import_core(data_dir):
    for lib in (os.path.join(data_dir, "lib"), os.path.join(os.path.dirname(HERE), "shared")):
        if os.path.isfile(os.path.join(lib, "adaptive_follow", "__init__.py")):
            sys.path.insert(0, lib)
            break
    from adaptive_follow.storage import Storage, default_data_dir  # noqa: F401
    return Storage


def _tail(path, n):
    if not os.path.isfile(path):
        return []
    with open(path, encoding="utf-8", errors="replace") as f:
        return f.read().splitlines()[-n:]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("command", choices=["status", "arm", "disarm", "toggle", "reload", "report", "dry-run",
                                        "list", "config", "set", "log"])
    ap.add_argument("args", nargs="*")
    ap.add_argument("--data-dir", default=None, help="default: %%APPDATA%%\\AdaptiveFollow")
    ap.add_argument("--wait", type=float, default=3.0, help="seconds to wait for FL Studio to answer")
    a = ap.parse_args()
    data_dir = a.data_dir or os.environ.get("ADAPTIVE_FOLLOW_HOME") or (
        os.path.join(os.environ["APPDATA"], "AdaptiveFollow") if os.environ.get("APPDATA")
        else os.path.join(os.path.expanduser("~"), ".adaptive_follow"))
    Storage = _import_core(data_dir)
    from adaptive_follow.model import Config
    st = Storage(data_dir)

    if a.command == "list":
        names = st.list_references()
        last = st.last_reference_name()
        for n in names:
            print(("* " if n == last else "  ") + n)
        print("(none - export one from the Piano Roll)" if not names else "* = last exported")
        return 0
    if a.command == "config":
        cfg = st.load_config(create_if_missing=False)
        for k, v in sorted(cfg.to_dict().items()):
            print("%-28s %s" % (k, json.dumps(v)))
        for w in cfg.warnings:
            print("warning: " + w)
        return 0
    if a.command == "set":
        if len(a.args) != 2:
            print("usage: afctl.py set KEY VALUE   (VALUE is JSON: 2, 1.5, true, \"hybrid\", null)")
            return 2
        key, raw = a.args
        try:
            value = json.loads(raw)
        except ValueError:
            value = raw                     # bare word, e.g. hybrid
        cfg = st.load_config(create_if_missing=False)
        try:
            setattr(cfg, key, value)
        except (AttributeError, ValueError) as exc:
            print("cannot set %s: %s" % (key, exc))
            return 1
        st.save_config(cfg)
        print("%s = %s (applied by FL Studio while follow is disarmed)" % (key, json.dumps(getattr(cfg, key))))
        return 0
    if a.command == "log":
        n = int(a.args[0]) if a.args else 40
        print("\n".join(_tail(st.log_path, n)))
        return 0

    text = a.command.replace("dry-run", "dry_run")
    if a.command == "dry-run":
        if not a.args or a.args[0] not in ("on", "off"):
            print("usage: afctl.py dry-run on|off")
            return 2
        text += " " + a.args[0]
    before = len(_tail(st.log_path, 100000))
    st.write_command(text)
    cmd_path = st.path("command.txt")
    deadline = time.time() + a.wait
    while time.time() < deadline and os.path.exists(cmd_path):
        time.sleep(0.1)
    if os.path.exists(cmd_path):
        print("FL Studio did not pick up the command within %.1f s. Is FL Studio running with the "
              "AdaptiveFollow controller enabled? (The command stays queued.)" % a.wait)
        return 1
    time.sleep(0.2)
    new = _tail(st.log_path, 100000)[before:]
    print("\n".join(new[-15:]) if new else "sent")
    return 0


if __name__ == "__main__":
    sys.exit(main())
