#pragma once

#include "timeline/Note.h"

#include <string_view>

namespace pianoassist::lesson
{

enum class PracticeMode
{
    bothHands,
    rightHand,
    leftHand,
};

constexpr std::string_view toString (PracticeMode mode) noexcept
{
    switch (mode)
    {
        case PracticeMode::bothHands: return "both hands";
        case PracticeMode::rightHand: return "right hand";
        case PracticeMode::leftHand:  return "left hand";
    }
    return "both hands";
}

/** Whether notes of this role must be played by the learner in this mode. */
constexpr bool isRequired (timeline::NoteRole role, PracticeMode mode) noexcept
{
    switch (mode)
    {
        case PracticeMode::bothHands: return role == timeline::NoteRole::rightHand || role == timeline::NoteRole::leftHand;
        case PracticeMode::rightHand: return role == timeline::NoteRole::rightHand;
        case PracticeMode::leftHand:  return role == timeline::NoteRole::leftHand;
    }
    return false;
}

/** What the app plays besides the learner. Backing follows its own switch in
    every mode; the other piano hand is optional only while practising one
    hand; required notes are never played for the learner. */
struct Accompaniment
{
    bool playOtherPianoPart = true;
    bool playBacking = true;

    friend bool operator== (const Accompaniment&, const Accompaniment&) = default;
};

constexpr bool isPlayedByApp (timeline::NoteRole role, bool practising, PracticeMode mode,
                              const Accompaniment& accompaniment) noexcept
{
    const bool pianoRole = role == timeline::NoteRole::rightHand || role == timeline::NoteRole::leftHand;
    if (! pianoRole)
        return accompaniment.playBacking; // backing, and unknown never becomes required
    if (! practising)
        return true;
    if (isRequired (role, mode))
        return false;
    return accompaniment.playOtherPianoPart;
}

} // namespace pianoassist::lesson
