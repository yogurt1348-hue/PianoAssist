#include "ui/KeyboardStrip.h"

#include "ui/RollColours.h"

#include <algorithm>

namespace pianoassist::ui
{
namespace
{
constexpr int lowest = 21;  // A0
constexpr int highest = 108; // C8
const juce::Colour heldColour (0xff6fcf97);
const juce::Colour requiredColour (0xfff2c94c);
const juce::Colour wrongColour (0xffeb5757);

bool hasPitch (const std::vector<std::uint8_t>& pitches, int pitch)
{
    return std::find (pitches.begin(), pitches.end(), pitch) != pitches.end();
}
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

void KeyboardStrip::paint (juce::Graphics& g)
{
    int whiteCount = 0;
    for (int p = lowest; p <= highest; ++p)
        if (! isBlackKey (p))
            ++whiteCount;

    const auto bounds = getLocalBounds().toFloat();
    const float whiteWidth = bounds.getWidth() / static_cast<float> (whiteCount);
    // Wrong beats held beats required, so a press always shows what it did.
    const auto colourOf = [this] (int p, juce::Colour normal) {
        if (hasPitch (wrong_, p))
            return wrongColour;
        if (hasPitch (held_, p))
            return heldColour;
        if (hasPitch (required_, p))
            return requiredColour;
        return normal;
    };

    float x = bounds.getX();
    for (int p = lowest; p <= highest; ++p)
    {
        if (isBlackKey (p))
            continue;
        const auto key = juce::Rectangle<float> (x, bounds.getY(), whiteWidth, bounds.getHeight());
        g.setColour (colourOf (p, juce::Colours::white));
        g.fillRect (key.reduced (0.5f, 0.0f));
        if (p == 60)
        {
            g.setColour (juce::Colours::grey);
            g.setFont (juce::FontOptions (10.0f));
            g.drawText ("C4", key.withTrimmedTop (key.getHeight() - 14.0f), juce::Justification::centred);
        }
        x += whiteWidth;
    }

    x = bounds.getX();
    for (int p = lowest; p <= highest; ++p)
    {
        if (! isBlackKey (p))
        {
            x += whiteWidth;
            continue;
        }
        const float blackWidth = whiteWidth * 0.6f;
        const auto key = juce::Rectangle<float> (x - blackWidth / 2.0f, bounds.getY(), blackWidth, bounds.getHeight() * 0.62f);
        const auto colour = colourOf (p, juce::Colours::black);
        g.setColour (colour == juce::Colours::black ? colour : colour.darker (0.2f));
        g.fillRect (key);
    }
}

} // namespace pianoassist::ui
