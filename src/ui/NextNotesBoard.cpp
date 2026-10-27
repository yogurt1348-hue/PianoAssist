#include "ui/NextNotesBoard.h"

#include "core/NoteName.h"
#include "ui/RollColours.h"

#include <algorithm>

namespace pianoassist::ui
{
namespace
{
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
    g.setColour (done ? theme::text.withAlpha (0.6f) : theme::onHand);
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

juce::Colour toneColour (NextNotesBoard::Tone tone)
{
    switch (tone)
    {
        case NextNotesBoard::Tone::waiting: return theme::waiting;
        case NextNotesBoard::Tone::warning: return theme::warning;
        case NextNotesBoard::Tone::error:   return theme::danger;
        case NextNotesBoard::Tone::good:    return theme::good;
        case NextNotesBoard::Tone::normal:  break;
    }
    return theme::text;
}
} // namespace

void NextNotesBoard::setView (const PracticeView& view)
{
    // Screen readers hear when the song starts waiting and what for.
    const bool startedWaiting = view.waiting && ! view_.waiting;
    view_ = view;
    juce::String notes;
    for (const auto pitch : view.required)
        notes << (notes.isEmpty() ? "" : ", ") << text (noteName (pitch));
    setDescription (notes.isEmpty() ? headline_ : TRANS ("Play") + " " + notes);
    if (startedWaiting && notes.isNotEmpty())
        if (auto* handler = getAccessibilityHandler())
            handler->postAnnouncement (TRANS ("Waiting for") + " " + notes, juce::AccessibilityHandler::AnnouncementPriority::high);
    repaint();
}

void NextNotesBoard::setMessage (const juce::String& headline, const juce::String& detail, Tone tone, bool showNotes)
{
    if (headline == headline_ && detail == detail_ && tone == tone_ && showNotes == showNotes_)
        return;
    headline_ = headline;
    detail_ = detail;
    tone_ = tone;
    showNotes_ = showNotes;
    setTitle (headline);
    repaint();
}

void NextNotesBoard::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    const auto accent = toneColour (tone_);
    g.setColour (tone_ == Tone::normal ? theme::panel : accent.withAlpha (0.14f).overlaidWith (theme::panel.withAlpha (0.0f)));
    g.fillRoundedRectangle (area, 10.0f);
    if (tone_ == Tone::normal)
        g.setColour (theme::panelEdge);
    else
        g.setColour (accent.withAlpha (0.8f));
    g.drawRoundedRectangle (area.reduced (0.5f), 10.0f, tone_ == Tone::normal ? 1.0f : 1.5f);
    area = area.reduced (16.0f, 8.0f);

    // Legend at the right edge.
    const auto legendArea = area.removeFromRight (std::min (190.0f, area.getWidth() * 0.25f));
    {
        g.setFont (juce::FontOptions (12.5f));
        auto row = legendArea.withSizeKeepingCentre (legendArea.getWidth(), 36.0f);
        const auto line = [&] (juce::Colour colour, const juce::String& label) {
            auto r = row.removeFromTop (18.0f);
            g.setColour (colour);
            g.fillRoundedRectangle (r.removeFromLeft (14.0f).withSizeKeepingCentre (14.0f, 8.0f), 3.0f);
            r.removeFromLeft (6.0f);
            g.setColour (theme::mutedText);
            g.drawText (label, r, juce::Justification::centredLeft);
        };
        line (theme::leftHand, TRANS ("Left hand"));
        line (theme::rightHand, TRANS ("Right hand"));
    }

    const bool notes = showNotes_ && view_.practising && view_.gateNumber != 0 && ! view_.current.empty();
    auto textArea = notes ? area.removeFromLeft (std::min (260.0f, area.getWidth() * 0.4f)) : area;
    g.setColour (accent);
    g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
    if (detail_.isEmpty())
        g.drawFittedText (headline_, textArea.toNearestInt(), juce::Justification::centredLeft, 2);
    else
    {
        auto top = textArea.withHeight (textArea.getHeight() / 2.0f + 4.0f);
        g.drawFittedText (headline_, top.toNearestInt(), juce::Justification::bottomLeft, 1);
        g.setColour (theme::mutedText);
        g.setFont (juce::FontOptions (13.5f));
        g.drawFittedText (detail_, textArea.withTrimmedTop (top.getHeight() + 2.0f).toNearestInt(),
                          juce::Justification::topLeft, 2);
    }
    if (! notes)
        return;

    const float big = std::min (area.getHeight(), 64.0f);
    const float y = area.getCentreY() - big / 2.0f;
    float x = area.getX();
    const auto drawHand = [&] (const std::vector<std::uint8_t>& pitches, RollRole role) {
        if (pitches.empty())
            return;
        for (const auto pitch : pitches)
            x += drawChip (g, x, y, big, pitch, roleColour (role),
                           hasPitch (view_.matched, pitch) && ! hasPitch (view_.required, pitch), true)
                 + 6.0f;
        x += 14.0f;
    };
    drawHand (view_.current.left, RollRole::leftHand);
    drawHand (view_.current.right, RollRole::rightHand);

    if (view_.upcoming.empty() || x > area.getRight() - 100.0f)
        return;
    g.setColour (theme::mutedText);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (TRANS ("then"), juce::Rectangle<float> (x, y, 44.0f, big), juce::Justification::centredLeft);
    x += 46.0f;
    const float small = 24.0f;
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
        x = std::max (stepX, leftX) + 12.0f;
        if (x > area.getRight() - 40.0f)
            break;
    }
}

} // namespace pianoassist::ui
