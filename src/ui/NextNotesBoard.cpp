#include "ui/NextNotesBoard.h"

#include "core/NoteName.h"
#include "ui/RollColours.h"

#include <algorithm>

namespace pianoassist::ui
{
namespace
{
const juce::Colour boardColour (0xff1f2329);
const juce::Colour waitingColour (0xfff2c94c);

juce::String text (const std::string& utf8)
{
    return juce::String::fromUTF8 (utf8.c_str());
}

bool hasPitch (const std::vector<std::uint8_t>& pitches, std::uint8_t pitch)
{
    return std::find (pitches.begin(), pitches.end(), pitch) != pitches.end();
}

/** Draws one note chip; returns its width. */
float drawChip (juce::Graphics& g, float x, float y, float height, std::uint8_t pitch, juce::Colour colour, bool done,
                bool large)
{
    const float width = large ? height * 1.25f : height * 1.4f;
    const auto box = juce::Rectangle<float> (x, y, width, height);
    g.setColour (done ? colour.withAlpha (0.25f) : colour);
    g.fillRoundedRectangle (box, 6.0f);
    g.setColour (done ? juce::Colours::white.withAlpha (0.5f) : juce::Colours::white);
    if (large)
    {
        g.setFont (juce::FontOptions (height * 0.42f, juce::Font::bold));
        g.drawText (text (noteName (pitch)), box.withTrimmedBottom (height * 0.32f), juce::Justification::centredBottom);
        g.setFont (juce::FontOptions (height * 0.24f));
        g.drawText (text (solfegeName (pitch)), box.withTrimmedTop (height * 0.66f).withTrimmedBottom (height * 0.06f),
                    juce::Justification::centredTop);
    }
    else
    {
        g.setFont (juce::FontOptions (height * 0.5f, juce::Font::bold));
        g.drawText (text (noteName (pitch)), box, juce::Justification::centred);
    }
    return width;
}
} // namespace

void NextNotesBoard::setView (const PracticeView& view)
{
    view_ = view;
    repaint();
}

void NextNotesBoard::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    g.setColour (view_.waiting ? waitingColour.withAlpha (0.18f) : boardColour);
    g.fillRoundedRectangle (area, 8.0f);
    if (view_.waiting)
    {
        g.setColour (waitingColour);
        g.drawRoundedRectangle (area.reduced (1.0f), 8.0f, 2.0f);
    }
    area = area.reduced (12.0f, 8.0f);

    g.setColour (view_.waiting ? waitingColour : juce::Colours::lightgrey);
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    juce::String caption;
    if (! view_.practising)
        caption = "Tick \"Practice\" to play along: the song will wait for your notes.";
    else if (view_.gateCount == 0)
        caption = "This hand has no notes in the song.";
    else if (view_.gateNumber == 0)
        caption = "All your notes are played.";
    else if (view_.waiting)
        caption = "WAITING FOR YOU";
    else if (view_.accompanimentOnly)
        caption = "Accompaniment only. Get ready:";
    else
        caption = "PLAY NEXT";
    const auto captionArea = area.removeFromLeft (150.0f);
    g.drawFittedText (caption, captionArea.toNearestInt(), juce::Justification::centredLeft, 3);
    if (! view_.practising || view_.gateNumber == 0)
        return;

    const float big = std::min (area.getHeight(), 76.0f);
    const float y = area.getCentreY() - big / 2.0f;
    float x = area.getX();
    const auto drawHand = [&] (const std::vector<std::uint8_t>& pitches, RollRole role, const char* label) {
        if (pitches.empty())
            return;
        g.setColour (roleColour (role));
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawFittedText (label, juce::Rectangle<float> (x, y, 64.0f, big).toNearestInt(), juce::Justification::centredLeft, 2);
        x += 66.0f;
        for (const auto pitch : pitches)
            x += drawChip (g, x, y, big, pitch, roleColour (role), hasPitch (view_.matched, pitch)
                                                                       && ! hasPitch (view_.required, pitch),
                           true)
                 + 6.0f;
        x += 18.0f;
    };
    drawHand (view_.current.left, RollRole::leftHand, "LEFT\nhand");
    drawHand (view_.current.right, RollRole::rightHand, "RIGHT\nhand");

    if (view_.upcoming.empty() || x > area.getRight() - 120.0f)
        return;
    g.setColour (juce::Colours::lightgrey);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText ("then", juce::Rectangle<float> (x, y, 40.0f, big), juce::Justification::centredLeft);
    x += 44.0f;
    const float small = 26.0f;
    for (const auto& step : view_.upcoming)
    {
        float stepX = x;
        float rowY = area.getCentreY() - small - 2.0f;
        for (const auto pitch : step.right)
            stepX += drawChip (g, stepX, rowY, small, pitch, roleColour (RollRole::rightHand), false, false) + 3.0f;
        float leftX = x;
        rowY = area.getCentreY() + 2.0f;
        for (const auto pitch : step.left)
            leftX += drawChip (g, leftX, rowY, small, pitch, roleColour (RollRole::leftHand), false, false) + 3.0f;
        x = std::max (stepX, leftX) + 14.0f;
        if (x > area.getRight() - 40.0f)
            break;
    }
}

} // namespace pianoassist::ui
