#include "ui/KeyboardStrip.h"

#include "core/NoteName.h"
#include "ui/RollColours.h"

#include <algorithm>

namespace pianoassist::ui
{
namespace
{
constexpr int lowest = 21;  // A0
constexpr int highest = 108; // C8
constexpr float blackHeightRatio = 0.62f;
constexpr float blackWidthRatio = 0.6f;
const juce::Colour heldColour (0xff6fcf97);
const juce::Colour requiredColour (0xfff2c94c);
const juce::Colour wrongColour (0xffeb5757);
const juce::Colour clickedColour (0xff9ecbff);

bool hasPitch (const std::vector<std::uint8_t>& pitches, int pitch)
{
    return std::find (pitches.begin(), pitches.end(), pitch) != pitches.end();
}

int whiteKeyCount()
{
    int count = 0;
    for (int p = lowest; p <= highest; ++p)
        if (! isBlackKey (p))
            ++count;
    return count;
}

/** Key rectangles in component coordinates, white keys first. */
struct Layout
{
    explicit Layout (juce::Rectangle<float> bounds)
        : area (bounds), whiteWidth (bounds.getWidth() / static_cast<float> (whiteKeyCount()))
    {
    }

    juce::Rectangle<float> key (int pitch) const
    {
        int whitesBefore = 0;
        for (int p = lowest; p < pitch; ++p)
            if (! isBlackKey (p))
                ++whitesBefore;
        const float x = area.getX() + static_cast<float> (whitesBefore) * whiteWidth;
        if (! isBlackKey (pitch))
            return { x, area.getY(), whiteWidth, area.getHeight() };
        const float w = whiteWidth * blackWidthRatio;
        return { x - w / 2.0f, area.getY(), w, area.getHeight() * blackHeightRatio };
    }

    juce::Rectangle<float> area;
    float whiteWidth;
};
} // namespace

void KeyboardStrip::setHeldPitches (std::vector<std::uint8_t> pitches)
{
    if (pitches == held_)
        return;
    held_ = std::move (pitches);
    repaint();
}

void KeyboardStrip::setPracticePitches (std::vector<std::uint8_t> required, std::vector<std::uint8_t> wrong)
{
    if (required == required_ && wrong == wrong_)
        return;
    required_ = std::move (required);
    wrong_ = std::move (wrong);
    repaint();
}

std::optional<std::uint8_t> KeyboardStrip::pitchAt (juce::Point<float> point) const
{
    const Layout layout (getLocalBounds().toFloat());
    for (int p = lowest; p <= highest; ++p) // black keys sit on top
        if (isBlackKey (p) && layout.key (p).contains (point))
            return static_cast<std::uint8_t> (p);
    for (int p = lowest; p <= highest; ++p)
        if (! isBlackKey (p) && layout.key (p).contains (point))
            return static_cast<std::uint8_t> (p);
    return std::nullopt;
}

void KeyboardStrip::press (std::optional<std::uint8_t> pitch)
{
    if (pitch == clicked_)
        return;
    if (clicked_ && onKey)
        onKey (*clicked_, false);
    clicked_ = pitch;
    if (clicked_ && onKey)
        onKey (*clicked_, true);
    repaint();
}

void KeyboardStrip::mouseDown (const juce::MouseEvent& event) { press (pitchAt (event.position)); }
void KeyboardStrip::mouseDrag (const juce::MouseEvent& event) { press (pitchAt (event.position)); }
void KeyboardStrip::mouseUp (const juce::MouseEvent&) { press (std::nullopt); }

void KeyboardStrip::paint (juce::Graphics& g)
{
    const Layout layout (getLocalBounds().toFloat());
    // Wrong beats held beats required, so a press always shows what it did.
    const auto colourOf = [this] (int p, juce::Colour normal) {
        if (hasPitch (wrong_, p))
            return wrongColour;
        if (hasPitch (held_, p))
            return heldColour;
        if (clicked_ && *clicked_ == p)
            return clickedColour;
        if (hasPitch (required_, p))
            return requiredColour;
        return normal;
    };

    const bool roomForSolfege = layout.whiteWidth >= 17.0f;
    for (int p = lowest; p <= highest; ++p)
    {
        if (isBlackKey (p))
            continue;
        const auto key = layout.key (p);
        g.setColour (colourOf (p, juce::Colours::white));
        g.fillRect (key.reduced (0.5f, 0.0f));

        // Letter on every white key, octave number on C, solfège underneath.
        const bool isC = p % 12 == 0;
        const auto name = noteName (p);
        g.setColour (isC ? juce::Colours::black : juce::Colours::darkgrey);
        g.setFont (juce::FontOptions (isC ? 11.0f : 10.0f, isC ? juce::Font::bold : juce::Font::plain));
        const auto label = juce::String (isC ? name : name.substr (0, 1));
        g.drawText (label, key.withTrimmedTop (key.getHeight() - (roomForSolfege ? 28.0f : 15.0f)).withHeight (14.0f),
                    juce::Justification::centred);
        if (roomForSolfege)
        {
            g.setColour (juce::Colours::grey);
            g.setFont (juce::FontOptions (9.0f));
            g.drawText (juce::String::fromUTF8 (solfegeName (p).c_str()), key.withTrimmedTop (key.getHeight() - 14.0f),
                        juce::Justification::centred);
        }
    }

    for (int p = lowest; p <= highest; ++p)
    {
        if (! isBlackKey (p))
            continue;
        const auto colour = colourOf (p, juce::Colours::black);
        g.setColour (colour == juce::Colours::black ? colour : colour.darker (0.2f));
        g.fillRect (layout.key (p));
    }
}

} // namespace pianoassist::ui
