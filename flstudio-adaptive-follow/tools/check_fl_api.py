"""
Static check: do the FL Studio entry scripts use only documented API?

Runs mypy on the MIDI device script and the Piano Roll script against
Image-Line's official API stubs (IL-Group/FL-Studio-API-Stubs). Any call to a
function, constant or attribute that is not in the official API is an error.

    python tools/check_fl_api.py                     # pip package fl-studio-api-stubs
    python tools/check_fl_api.py --stubs-src <repo>  # a clone of IL-Group/FL-Studio-API-Stubs

Note: the PyPI release 37.0.1 predates the Piano Roll ``ScriptDialog`` stubs
(added to the official repository in Sept 2024), so the Piano Roll script is
only checked when ``--stubs-src`` points to a current clone.
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEVICE = os.path.join(ROOT, "midi_device_script", "device_AdaptiveFollow", "device_AdaptiveFollow.py")
PIANO_ROLL = os.path.join(ROOT, "piano_roll_script", "Adaptive Follow - Export Reference.pyscript")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--stubs-src", help="path to a clone of IL-Group/FL-Studio-API-Stubs")
    args = ap.parse_args()
    try:
        import mypy  # noqa: F401
    except ImportError:
        print("mypy is not installed: pip install -r requirements-dev.txt")
        return 2
    tmp = tempfile.mkdtemp(prefix="af_api_")
    try:
        files = [shutil.copy(DEVICE, os.path.join(tmp, "device_AdaptiveFollow.py"))]
        mypypath = [os.path.join(ROOT, "shared")]
        extra = []
        if args.stubs_src:
            src = os.path.join(args.stubs_src, "src")
            mypypath += [os.path.join(src, d) for d in ("midi_controller_scripting", "piano_roll_scripting",
                                                         "edison_scripting")]
            extra = ["--no-site-packages"]
            files.append(shutil.copy(PIANO_ROLL, os.path.join(tmp, "export_reference.py")))
        else:
            print("note: Piano Roll script skipped (pip stubs 37.0.1 lack flpianoroll.ScriptDialog); "
                  "use --stubs-src for a full check")
        env = dict(os.environ, MYPYPATH=os.pathsep.join(mypypath))
        cmd = [sys.executable, "-m", "mypy", "--config-file", os.path.join(ROOT, "mypy.ini")] + extra + files
        result = subprocess.run(cmd, env=env, capture_output=True, text=True)
        print(result.stdout.replace(tmp + os.sep, "").strip())
        if result.returncode == 0:
            print("OK: only documented FL Studio API is used.")
        return result.returncode
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
