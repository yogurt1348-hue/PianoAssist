"""
Adaptive Follow - shared core.

FL Studio independent: reference model, score follower, tempo estimator,
adaptive transport controller, session state machine, diagnostics and
storage. Used by the FL Studio Piano Roll exporter and MIDI device script,
the session simulator and the tests.

The algorithms are adapted from the Cadenza project's C++ Adaptive Song Mode
(``Source/Adaptive``); Cadenza itself is not modified or required.
"""

__version__ = "0.1.0"

from .model import Config  # noqa: F401
from .reference import Reference, ReferenceFormatError, build_reference_from_notes  # noqa: F401
from .session import AdaptiveFollowSession  # noqa: F401
from .storage import ReferenceMissingError, Storage  # noqa: F401
