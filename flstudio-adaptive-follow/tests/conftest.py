import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "shared"))

import pytest  # noqa: E402

from adaptive_follow.diagnostics import Diagnostics  # noqa: E402
from adaptive_follow.model import Config  # noqa: E402
from adaptive_follow.session import AdaptiveFollowSession  # noqa: E402
from adaptive_follow.simulation import VirtualTransport, synthetic_reference  # noqa: E402
from adaptive_follow.storage import Storage  # noqa: E402


@pytest.fixture
def storage(tmp_path):
    s = Storage(str(tmp_path / "AdaptiveFollow"))
    s.ensure_dirs()
    return s


class Note(object):
    """Stand-in for flpianoroll.Note (same documented attribute names)."""

    def __init__(self, number, time, length=96, velocity=0.8, color=0, group=0, muted=False):
        self.number = number
        self.time = time
        self.length = length
        self.velocity = velocity
        self.color = color
        self.group = group
        self.muted = muted


@pytest.fixture
def make_session(storage):
    def factory(config=None, reference=None, tempo=120.0, ppq=96, **port_kw):
        ref = reference or synthetic_reference(48, ppq=ppq)
        storage.save_reference(ref)
        cfg = Config.from_dict(config or {})
        tr = VirtualTransport(ppq=ppq, tempo=tempo, **port_kw)
        diag = Diagnostics(storage.log_path, "debug")
        s = AdaptiveFollowSession(tr, storage, diag, cfg, clock=lambda: tr.now)
        assert s.load_reference()
        return s, tr, ref
    return factory
