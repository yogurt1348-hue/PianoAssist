"""
In-memory hand-off of the exported reference from the Piano Roll script to the
MIDI device script.

FL Studio's embedded Python may refuse ``open()`` for scripts (observed as
``SystemError: <class '_io.FileIO'> returned NULL without setting an
exception``), so a reference file cannot always be written. If both scripts
run in the same interpreter they share ``sys.modules``; the exporter then
publishes the reference here and the device script picks it up while
disarmed. Lives until FL Studio is closed. ``visible()`` / ``status()`` let
``af_probe()`` report whether the channel works.
"""

import sys

NAME = "adaptive_follow_shared"


def _box():
    box = sys.modules.get(NAME)
    if box is None:
        box = type(sys)(NAME)
        box.serial = 0
        box.reference_json = None
        box.name = ""
        box.source = ""
        sys.modules[NAME] = box
    return box


def publish(reference, source="Piano Roll"):
    """Make ``reference`` available to the device script. Returns the serial number."""
    box = _box()
    text = reference.to_json()
    if text == box.reference_json:
        return box.serial           # same export again (Piano Roll preview): nothing new
    box.reference_json = text
    box.name = reference.name
    box.source = source
    box.serial += 1
    return box.serial


def latest():
    """(serial, reference_json or None, name)."""
    box = sys.modules.get(NAME)
    if box is None:
        return 0, None, ""
    return getattr(box, "serial", 0), getattr(box, "reference_json", None), getattr(box, "name", "")


def status():
    serial, text, name = latest()
    if not serial:
        return "in-memory hand-off: nothing published in this interpreter yet"
    return "in-memory hand-off: reference %r (#%d, %d bytes)" % (name, serial, len(text or ""))


def clear():
    sys.modules.pop(NAME, None)
