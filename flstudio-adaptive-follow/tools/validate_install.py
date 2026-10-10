"""
Check an Adaptive Follow installation (run after install_windows.ps1).

    python tools/validate_install.py                         # default locations
    python tools/validate_install.py --fl-settings "<...>\\Image-Line\\FL Studio\\Settings" --data-dir "<...>"

Checks: the scripts are in FL Studio's user folders, adaptive_follow_paths.py
points to existing folders, the installed shared module imports and has the
same version as the scripts, config.json is valid, references load. Nothing
is modified (except Python's own __pycache__ when importing).
"""

import argparse
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
DEVICE = "device_AdaptiveFollow.py"
PYSCRIPT = "Adaptive Follow - Export Reference.pyscript"

_results = []


def check(ok, what, hint=""):
    _results.append(bool(ok))
    print("  [%s] %s%s" % ("OK" if ok else "FAIL", what, "" if ok or not hint else "\n         -> " + hint))
    return ok


def info(text):
    print("  [..] " + text)


def default_fl_settings():
    docs = os.path.join(os.path.expanduser("~"), "Documents")
    candidates = [os.path.join(docs, "Image-Line", "FL Studio", "Settings")]
    if os.environ.get("OneDrive"):
        candidates.append(os.path.join(os.environ["OneDrive"], "Documents", "Image-Line", "FL Studio", "Settings"))
    for c in candidates:
        if os.path.isdir(c):
            return c
    return candidates[0]


def default_data_dir():
    if os.environ.get("ADAPTIVE_FOLLOW_HOME"):
        return os.environ["ADAPTIVE_FOLLOW_HOME"]
    if os.environ.get("APPDATA"):
        return os.path.join(os.environ["APPDATA"], "AdaptiveFollow")
    return os.path.join(os.path.expanduser("~"), ".adaptive_follow")


def read_paths_module(path):
    """Evaluate adaptive_follow_paths.py without importing it."""
    scope = {}
    with open(path, encoding="utf-8-sig") as f:
        exec(compile(f.read(), path, "exec"), {}, scope)
    return scope.get("LIB_DIR"), scope.get("DATA_DIR")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--fl-settings", default=None, help="FL Studio ...\\Image-Line\\FL Studio\\Settings folder")
    ap.add_argument("--data-dir", default=None, help="Adaptive Follow data folder (default %%APPDATA%%\\AdaptiveFollow)")
    args = ap.parse_args()
    fl = args.fl_settings or default_fl_settings()
    data = args.data_dir or default_data_dir()
    print("Adaptive Follow installation check")
    print("  FL Studio Settings: %s" % fl)
    print("  data folder       : %s" % data)

    print("\nFL Studio scripts")
    hw = os.path.join(fl, "Hardware", "AdaptiveFollow")
    dev = os.path.join(hw, DEVICE)
    if check(os.path.isfile(dev), "device script %s" % dev, "run tools\\install_windows.ps1"):
        with open(dev, encoding="utf-8") as f:
            first = f.readline().strip()
        check(first == "# name=AdaptiveFollow", "device script header '# name=AdaptiveFollow'",
              "the first line must be the FL Studio script name comment")
    pr = os.path.join(fl, "Piano roll scripts", PYSCRIPT)
    check(os.path.isfile(pr), "Piano Roll script %s" % pr, "run tools\\install_windows.ps1")

    print("\nShared module next to the scripts (FL Studio adds each script's folder to its module path)")
    for folder in (hw, os.path.join(fl, "Piano roll scripts")):
        init = os.path.join(folder, "adaptive_follow", "__init__.py")
        if check(os.path.isfile(init), "package %s" % os.path.dirname(init), "run tools\\install_windows.ps1"):
            sys.path.insert(0, os.path.join(ROOT, "shared"))
            try:
                from adaptive_follow import __version__ as src_version
            finally:
                sys.path.pop(0)
            with open(init, encoding="utf-8") as f:
                text = f.read()
            check('__version__ = "%s"' % src_version in text, "version %s" % src_version, "re-run the installer to update")

    print("\nPaths file")
    lib = None
    for folder in (hw, os.path.join(fl, "Piano roll scripts")):
        p = os.path.join(folder, "adaptive_follow_paths.py")
        if not check(os.path.isfile(p), "adaptive_follow_paths.py in %s" % folder,
                     "without it the scripts fall back to %APPDATA%\\AdaptiveFollow"):
            continue
        try:
            lib_dir, data_dir = read_paths_module(p)
        except Exception as exc:
            check(False, "adaptive_follow_paths.py is valid Python (%s)" % exc)
            continue
        check(lib_dir and os.path.isdir(lib_dir), "LIB_DIR exists: %s" % lib_dir)
        check(data_dir and os.path.normcase(os.path.abspath(data_dir)) == os.path.normcase(os.path.abspath(data)),
              "DATA_DIR matches the data folder: %s" % data_dir, "pass --data-dir or re-run the installer")
        lib = lib_dir or lib

    print("\nShared module")
    lib = lib or os.path.join(data, "lib")
    pkg = os.path.join(lib, "adaptive_follow")
    if check(os.path.isfile(os.path.join(pkg, "__init__.py")), "package %s" % pkg):
        # Import in a clean, isolated interpreter (-I) so the repository copy is not picked up.
        code = ("import sys; sys.path.insert(0, %r); import adaptive_follow as a; "
                "from adaptive_follow.session import AdaptiveFollowSession; "
                "print(a.__version__ + '|' + a.__file__)") % lib
        r = subprocess.run([sys.executable, "-I", "-c", code], capture_output=True, text=True)
        out = (r.stdout or "").strip()
        if check(r.returncode == 0 and "|" in out, "module imports", (r.stderr or "").strip()[-400:]):
            ver, where = out.split("|", 1)
            check(os.path.normcase(where).startswith(os.path.normcase(lib)), "imported from %s" % where)
            sys.path.insert(0, os.path.join(ROOT, "shared"))
            try:
                from adaptive_follow import __version__ as src_version
                check(ver == src_version, "installed version %s == source version %s" % (ver, src_version),
                      "re-run the installer to update")
            finally:
                sys.path.pop(0)

    print("\nData")
    for sub in ("references", "logs", "reports"):
        check(os.path.isdir(os.path.join(data, sub)), "folder %s" % os.path.join(data, sub))
    cfg_path = os.path.join(data, "config.json")
    if os.path.isfile(cfg_path):
        sys.path.insert(0, lib)
        try:
            from adaptive_follow.model import Config
            with open(cfg_path, encoding="utf-8-sig") as f:
                cfg = Config.from_dict(json.load(f))
            check(not cfg.warnings, "config.json is valid", "; ".join(cfg.warnings))
            info("dry_run=%s transport_control=%s tempo_control=%s strategy=%s channel=%s"
                 % (cfg.dry_run, cfg.transport_control, cfg.tempo_control, cfg.follow_strategy,
                    cfg.midi_channel or "any"))
        except ValueError as exc:
            check(False, "config.json is valid JSON (%s)" % exc, "fix it or delete it (defaults are recreated)")
        finally:
            sys.path.pop(0)
    else:
        info("config.json not created yet (the device script creates it with defaults on first start)")
    refs = os.path.join(data, "references")
    names = sorted(n for n in os.listdir(refs) if n.endswith(".json")) if os.path.isdir(refs) else []
    if not names:
        info("no references yet: export one from the Piano Roll")
    else:
        sys.path.insert(0, lib)
        try:
            from adaptive_follow.reference import Reference
            for n in names:
                try:
                    with open(os.path.join(refs, n), encoding="utf-8-sig") as f:
                        ref = Reference.from_json(f.read())
                    check(True, "reference %s: %d notes, %d events, PPQ %d" % (n, len(ref.notes), ref.event_count, ref.ppq))
                except Exception as exc:
                    check(False, "reference %s loads (%s)" % (n, exc), "export it again from the Piano Roll")
        finally:
            sys.path.pop(0)
    manifest = os.path.join(data, "install-manifest.json")
    check(os.path.isfile(manifest), "install manifest %s" % manifest, "uninstall_windows.ps1 needs it")

    failed = _results.count(False)
    print("\n%s: %d checks, %d failed" % ("OK" if not failed else "PROBLEMS", len(_results), failed))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
