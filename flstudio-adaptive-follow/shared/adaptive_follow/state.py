"""
Public Adaptive Follow session states and the transition log.

    IDLE        loaded, not armed (nothing is controlled)
    ARMED       armed, waiting for the first confident match
    TRACKING    following the performer (confidence may be lower: "uncertain")
    WAITING     the performer stopped advancing: playback brakes / pauses
    LOST        position unknown: speed returns to 1.0 (or pause, configurable)
    RECOVERING  a new position was found and is being confirmed
    STOPPED     session ended: speed and tempo restored

The score follower has its own finer states (see score_follower.F_*);
``from_follower`` maps them onto the public ones.
"""

from . import score_follower as sf

IDLE = "IDLE"
ARMED = "ARMED"
TRACKING = "TRACKING"
WAITING = "WAITING"
LOST = "LOST"
RECOVERING = "RECOVERING"
STOPPED = "STOPPED"

ALL_STATES = (IDLE, ARMED, TRACKING, WAITING, LOST, RECOVERING, STOPPED)

_FROM_FOLLOWER = {
    sf.F_IDLE: ARMED,
    sf.F_TRACKING: TRACKING,
    sf.F_UNCERTAIN: TRACKING,
    sf.F_WAITING: WAITING,
    sf.F_LOST: LOST,
    sf.F_REACQUIRING: RECOVERING,
}


def from_follower(follower_state):
    return _FROM_FOLLOWER.get(follower_state, LOST)


class Transition(object):
    __slots__ = ("time", "old", "new", "reason")

    def __init__(self, time, old, new, reason):
        self.time = time
        self.old = old
        self.new = new
        self.reason = reason

    def __repr__(self):
        return "%s -> %s (%s)" % (self.old, self.new, self.reason)


class StateMachine(object):
    def __init__(self, max_history=200):
        self.state = IDLE
        self.history = []
        self.max_history = max_history
        self.listeners = []

    def set(self, new, reason, now=0.0):
        if new not in ALL_STATES:
            raise ValueError("unknown state %r" % new)
        if new == self.state:
            return False
        t = Transition(now, self.state, new, reason)
        self.state = new
        self.history.append(t)
        if len(self.history) > self.max_history:
            self.history.pop(0)
        for cb in list(self.listeners):
            try:
                cb(t)
            except Exception:
                pass
        return True

    @property
    def active(self):
        """True while the session is following (or waiting to follow)."""
        return self.state in (ARMED, TRACKING, WAITING, LOST, RECOVERING)
