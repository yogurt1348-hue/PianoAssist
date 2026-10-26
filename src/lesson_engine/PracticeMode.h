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

} // namespace pianoassist::lesson
